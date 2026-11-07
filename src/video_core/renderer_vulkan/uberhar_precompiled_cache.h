// CodexAstraUlt-2: Copyright 2026 Uberhar contributors
// CodexAstraUlt-2: Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraUlt-2: Prepare known CPU-fragment pipelines on the renderer's device
// before gameplay; bounded resident owners and transient driver warming differ.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/uberhar_precompile_recipes.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

// CodexAstraUlt-2: The enclosing renderer freezes profile/CPU interface and drains
// command, compiler and GPU users before clearing this bank or replacing its
// device/cache/layout. No worker job borrows a transient preparation owner.
class PrecompiledCache {
public:
    static constexpr std::size_t MaxResidentPipelines = 128;
    static constexpr std::size_t MaxResidentModules = 64;

    struct ReadyPipeline {
        GraphicsPipeline* pipeline;
        Shader* fragment;
        u64 fragment_hash;
    };

    struct Statistics {
        std::size_t input{}, eligible{}, rejected{}, duplicates{}, capped{};
        std::size_t shader_attempts{}, ready_modules{}, pipeline_attempts{};
        std::size_t resident_ready{}, warmed_only{}, failed{}, unattempted{}, budget_deferred{};
        bool cancelled{}, aborted{};
    };

    PrecompiledCache(const Instance& instance_, RenderManager& renderpass_cache_,
                     Shader& trivial_vertex_, vk::PipelineCache driver_cache_,
                     vk::PipelineLayout pipeline_layout_)
        : instance{instance_}, renderpass_cache{renderpass_cache_},
          trivial_vertex{trivial_vertex_}, driver_cache{driver_cache_},
          pipeline_layout{pipeline_layout_},
          dynamic{instance.IsExtendedDynamicStateSupported()} {
        lookup.fill(-1);
    }

    ~PrecompiledCache() {
        Clear();
    }

    PrecompiledCache(const PrecompiledCache&) = delete;
    PrecompiledCache& operator=(const PrecompiledCache&) = delete;

    // CodexAstraUlt-2: Compile returns a terminal module owned by this call, using
    // the same device and frozen profile. Direct Build never queues dependencies
    // behind itself, and cancellation is observed between uninterruptible driver
    // calls. Repeated initialization requires an explicit, externally drained Clear.
    template <typename Compile>
    void Prepare(std::span<const Precompile::Recipe> recipes,
                 const std::atomic_bool& stop_loading,
                 const VideoCore::DiskResourceLoadCallback& callback, Compile&& compile,
                 std::size_t available_modules = MaxResidentModules + 1) {
        if (started) return;
        started = true;
        statistics = {};
        failure_reason.fill(0);
        statistics.input = recipes.size();
        const auto count = std::min(recipes.size(), Precompile::MaxRecipes);
        statistics.capped = recipes.size() - count;
        // CodexAstraUlt-2: Retained optional modules from a title switch consume
        // the same global budget. Reserve one slot for streaming excess groups;
        // zero available slots defers preparation without reporting a compiler error.
        const auto resident_module_limit = std::min(MaxResidentModules,
            available_modules ? available_modules - 1 : 0);

        try {
            // CodexAstraUlt-2: Rank exact observed recipes before grouping so a
            // prolific low-value fragment cannot consume the resident PSO budget.
            std::vector<const Precompile::Recipe*> ranked;
            ranked.reserve(count);
            for (const auto& recipe : recipes.first(count)) {
                if (stop_loading.load(std::memory_order_relaxed)) {
                    statistics.cancelled = true;
                    break;
                }
                if (Precompile::IsEligible(recipe.config, recipe.pipeline)) {
                    ranked.push_back(&recipe);
                } else {
                    ++statistics.rejected;
                }
            }
            if (statistics.cancelled) return;
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto* a, const auto* b) {
                return a->uses > b->uses;
            });

            struct Item { const Precompile::Recipe* recipe; bool retain; };
            struct Group {
                const Pica::Shader::FSConfig* config;
                std::vector<Item> items;
                bool retain{};
            };
            std::vector<Group> groups;
            std::unordered_map<u64, std::vector<std::size_t>> shader_groups;
            std::unordered_map<u64, std::vector<const Precompile::Recipe*>> seen;
            std::size_t planned_pipelines{}, planned_modules{};
            for (const auto* recipe : ranked) {
                const auto hash = SemanticHash(recipe->config, recipe->pipeline);
                auto& bucket = seen[hash];
                const bool duplicate = std::any_of(bucket.begin(), bucket.end(), [&](const auto* old) {
                    return old->config == recipe->config &&
                           old->pipeline.ExecutionEquals(recipe->pipeline, dynamic);
                });
                if (duplicate) { ++statistics.duplicates; continue; }
                bucket.push_back(recipe);
                ++statistics.eligible;
                auto& candidates = shader_groups[recipe->config.Hash()];
                auto found = std::find_if(candidates.begin(), candidates.end(), [&](std::size_t i) {
                    return *groups[i].config == recipe->config;
                });
                std::size_t group_index;
                if (found == candidates.end()) {
                    group_index = groups.size();
                    groups.push_back({&recipe->config, {}});
                    candidates.push_back(group_index);
                } else {
                    group_index = *found;
                }
                auto& group = groups[group_index];
                const bool retain = planned_pipelines < MaxResidentPipelines &&
                    (group.retain || planned_modules < resident_module_limit);
                if (retain) {
                    ++planned_pipelines;
                    if (!group.retain) { group.retain = true; ++planned_modules; }
                }
                group.items.push_back({recipe, retain});
            }

            if (!statistics.eligible) return;
            if (!available_modules) {
                statistics.budget_deferred = statistics.eligible;
                FinishCounts();
                return;
            }
            if (!trivial_vertex.IsDone() || trivial_vertex.HasFailed() || !trivial_vertex.Handle()) {
                Fail("CPU vertex module is not ready");
                FinishCounts();
                return;
            }

            // CodexAstraUlt-2: One exact fragment is compiled once per preparation
            // pass. Excess PSOs are built and immediately released; this attempts to warm
            // opaque driver data but never claims a drawable resident handle.
            for (const auto& group : groups) {
                // CodexAstraUlt-2: Publish initial progress before potentially
                // expensive compilation and let cancellation prevent that call.
                if (callback) callback(VideoCore::LoadCallbackStage::Build,
                    statistics.resident_ready + statistics.warmed_only,
                    statistics.eligible, "Specialized CPU shaders/pipelines");
                if (stop_loading.load(std::memory_order_relaxed)) {
                    statistics.cancelled = true;
                    break;
                }
                ++statistics.shader_attempts;
                auto module = compile(*group.config);
                if (!module || !module->IsDone() || module->HasFailed() || !module->Handle()) {
                    Fail("fragment preparation failed or returned an unfinished module");
                    break;
                }
                ++statistics.ready_modules;
                if (stop_loading.load(std::memory_order_relaxed)) {
                    statistics.cancelled = true;
                    break;
                }
                Shader* fragment = module.get();
                if (group.retain) resident_modules[module_count++] = std::move(module);
                for (const auto& item : group.items) {
                    if (stop_loading.load(std::memory_order_relaxed)) {
                        statistics.cancelled = true;
                        break;
                    }
                    ++statistics.pipeline_attempts;
                    PipelineInfo info{};
                    info.state = item.recipe->pipeline;
                    info.state.shader_ids = {0, group.config->Hash(), 0};
                    const std::array<Shader*, MAX_SHADER_STAGES> stages{
                        &trivial_vertex, fragment, nullptr};
                    auto pipeline = std::make_unique<GraphicsPipeline>(
                        instance, renderpass_cache, info, driver_cache, pipeline_layout,
                        stages, nullptr, PipelineBuildOptions{nullptr, nullptr, false, true});
                    if (!pipeline->Build() || !pipeline->IsDone() || pipeline->HasFailed() ||
                        !pipeline->Handle()) {
                        // CodexAstraUlt-2: The pipeline wrapper preserves this
                        // terminal driver result instead of treating it as an
                        // optional miss that could resume gameplay on a lost device.
                        if (pipeline->DeviceLost())
                            throw OptionalCompileError(OptionalCompileFailure::DeviceLost,
                                "device lost while preparing a graphics pipeline");
                        Fail("pipeline preparation failed (see Vulkan compiler log)");
                        break;
                    }
                    if (item.retain) {
                        const auto hash = SemanticHash(*group.config, info.state);
                        resident_pipelines[pipeline_count].emplace(Entry{
                            *group.config, info.state, fragment, std::move(pipeline)});
                        Insert(hash, pipeline_count++);
                        ++statistics.resident_ready;
                    } else {
                        ++statistics.warmed_only;
                    }
                    if (callback) callback(VideoCore::LoadCallbackStage::Build,
                        statistics.resident_ready + statistics.warmed_only,
                        statistics.eligible, "Known CPU pipelines");
                }
                if (statistics.cancelled || statistics.aborted) break;
            }
        } catch (const OptionalCompileError& error) {
            Fail(error.what());
            FinishCounts();
            // CodexAstraUlt-2: A lost device cannot render a generic recovery draw.
            // Preserve the caller's typed terminal recovery boundary.
            if (error.kind == OptionalCompileFailure::DeviceLost) throw;
        } catch (const std::exception& error) {
            Fail(error.what());
        } catch (...) {
            Fail("unknown preparation failure");
        }
        FinishCounts();
    }

    // CodexAstraUlt-2: Hashes locate candidates; full fragment and active pipeline
    // equality prove reuse. No generic creation, compiler work or state mutation
    // occurs here. Live descriptors and dynamic values remain the caller's draw.
    std::optional<ReadyPipeline> Lookup(const Pica::Shader::FSConfig& config,
                                        const StaticPipelineInfo& state) const noexcept {
        if (!Precompile::IsEligible(config, state)) return {};
        const auto hash = SemanticHash(config, state);
        auto slot = static_cast<std::size_t>(hash) & (lookup.size() - 1);
        for (std::size_t n = 0; n < lookup.size(); ++n) {
            const auto index = lookup[slot];
            if (index < 0) return {};
            const auto& entry = *resident_pipelines[static_cast<std::size_t>(index)];
            if (entry.config == config && entry.state.ExecutionEquals(state, dynamic) &&
                entry.pipeline->IsDone() && !entry.pipeline->HasFailed() && entry.pipeline->Handle()) {
                return ReadyPipeline{entry.pipeline.get(), entry.fragment, entry.config.Hash()};
            }
            slot = (slot + 1) & (lookup.size() - 1);
        }
        return {};
    }

    std::size_t ResidentPipelineCount() const noexcept { return pipeline_count; }
    std::size_t ResidentModuleCount() const noexcept { return module_count; }
    const Statistics& Stats() const noexcept { return statistics; }
    const char* FailureReason() const noexcept { return failure_reason.data(); }

    // CodexAstraUlt-2: Call only after the renderer's existing lifetime drains.
    // Pipeline wrappers retain shader pointers even after Vulkan compilation.
    void Clear() noexcept {
        for (auto& pipeline : resident_pipelines) pipeline.reset();
        for (auto& module : resident_modules) module.reset();
        lookup.fill(-1);
        pipeline_count = module_count = 0;
        started = false;
    }

private:
    struct Entry {
        Pica::Shader::FSConfig config;
        StaticPipelineInfo state;
        Shader* fragment;
        std::unique_ptr<GraphicsPipeline> pipeline;
    };

    u64 SemanticHash(const Pica::Shader::FSConfig& config,
                     const StaticPipelineInfo& state) const noexcept {
        return Common::HashCombine(config.Hash(), state.ExecutionHash(dynamic, {}));
    }

    void Insert(u64 hash, std::size_t index) noexcept {
        auto slot = static_cast<std::size_t>(hash) & (lookup.size() - 1);
        while (lookup[slot] >= 0) slot = (slot + 1) & (lookup.size() - 1);
        lookup[slot] = static_cast<int>(index);
    }

    void Fail(const char* reason) noexcept {
        ++statistics.failed;
        statistics.aborted = true;
        const auto length = std::min(std::strlen(reason), failure_reason.size() - 1);
        std::memcpy(failure_reason.data(), reason, length);
        failure_reason[length] = '\0';
    }

    void FinishCounts() noexcept {
        statistics.unattempted = statistics.eligible -
            std::min(statistics.eligible, statistics.pipeline_attempts);
    }

    const Instance& instance;
    RenderManager& renderpass_cache;
    Shader& trivial_vertex;
    vk::PipelineCache driver_cache;
    vk::PipelineLayout pipeline_layout;
    const bool dynamic;
    bool started{};
    Statistics statistics;
    std::array<char, 192> failure_reason{};
    // CodexAstraUlt-2: Fixed resident storage cannot grow during gameplay. The
    // double-sized index uses bounded linear probing with exact collision checks.
    std::array<std::unique_ptr<Shader>, MaxResidentModules> resident_modules{};
    std::array<std::optional<Entry>, MaxResidentPipelines> resident_pipelines{};
    std::array<int, MaxResidentPipelines * 2> lookup{};
    std::size_t pipeline_count{}, module_count{};
};

} // CodexAstraUlt-2: namespace Vulkan
