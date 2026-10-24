// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring> // CodexAstraUlt: Classify periodic delivery without allocating strings.
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/static_lru_cache.h"
#include "video_core/renderer_vulkan/uberhar_optional_disk_cache.h"
#include "common/zstd_compression.h"
#include "video_core/renderer_vulkan/uberhar_shader_compile_policy.h" // CodexAstraLocal: Freeze explicit worker compile options.
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/generator/spv_fs_shader_gen.h"
#include "video_core/shader_build_failure.h" // CodexAstraUlt-2: Publish all compiler outcomes.

#define MALFORMED_DISK_CACHE                                                                       \
    do {                                                                                           \
        LOG_ERROR(Render_Vulkan, "Malformed disk shader cache");                                   \
        cleanup_on_error();                                                                        \
        return false;                                                                              \
    } while (0)

// Enable to debug when new cache objects are created.
// #define ENABLE_LOG_NEW_OBJECT

#ifdef ENABLE_LOG_NEW_OBJECT
#define LOG_NEW_OBJECT LOG_DEBUG
#else
#define LOG_NEW_OBJECT(...) ((void)0)
#endif

namespace Vulkan {

using namespace Pica::Shader::Generator;
using namespace Pica::Shader;

namespace {
// CodexAstraLocal: Serialize semantic profile fields explicitly so C++ padding
// cannot cause false warm misses. FSConfig has a unique, disk-versioned layout.
std::vector<u32> OptionalProfileWords(const Profile& p) {
    std::vector<u32> result{p.enable_accurate_mul, p.has_separable_shaders, p.has_clip_planes,
        p.has_geometry_shader, p.has_custom_border_color, p.has_fragment_shader_interlock,
        p.has_fragment_shader_barycentric, p.has_blend_minmax_factor, p.has_minus_one_to_one_range,
        p.has_logic_op, p.has_gl_ext_framebuffer_fetch, p.has_gl_arm_framebuffer_fetch,
        p.has_gl_nv_fragment_shader_interlock, p.has_gl_intel_fragment_shader_ordering,
        p.has_gl_nv_fragment_shader_barycentric, p.vk_disable_spirv_optimizer,
        p.vk_use_spirv_generator, p.is_vulkan};
    for (const auto& t : p.vk_format_traits) {
        const std::array<u32, 9> words{t.transfer_support, t.blit_support, t.attachment_support,
            t.storage_support, t.needs_conversion, t.needs_emulation, t.usage_flags,
            t.aspect_flags, t.native_format};
        result.insert(result.end(), words.begin(), words.end());
    }
    return result;
}
std::vector<u32> OptionalIdentity(const FSConfig& fs, const Profile& profile,
                                  const GLSL::StaticTevPlan* plan) {
    std::vector<u32> result{plan ? 2U : 1U, static_cast<u32>(sizeof(fs)),
        static_cast<u32>(sizeof(GLSL::StaticTevPlan)), StaticTevPolicy::CpuInterface,
        StaticTevPolicy::GeneratorPolicy};
    const auto append = [&result](const auto& object) {
        static_assert(std::is_trivially_copyable_v<std::remove_cvref_t<decltype(object)>>);
        static_assert(sizeof(object) % sizeof(u32) == 0);
        const auto start = result.size();
        result.resize(start + sizeof(object) / sizeof(u32));
        std::memcpy(result.data() + start, &object, sizeof(object));
    };
    append(fs);
    const auto fields = OptionalProfileWords(profile);
    result.insert(result.end(), fields.begin(), fields.end());
    if (plan) append(*plan);
    return result;
}
} // namespace

static VideoCore::DiskResourceLoadCallback MakeThrottledCallback(
    VideoCore::DiskResourceLoadCallback original) {

    if (!original)
        return nullptr;

    auto last_call = std::chrono::steady_clock::now() - std::chrono::milliseconds(10);

    return [original, last_call](VideoCore::LoadCallbackStage stage, std::size_t current,
                                 std::size_t total, const std::string& name) mutable {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_call >= std::chrono::milliseconds(10)) {
            last_call = now;
            original(stage, current, total, name);
        }
    };
}

void ShaderDiskCache::Init(const std::atomic_bool& stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) {

    // CodexAstraLocal: The independent optional cache is demand-loaded. It never
    // preloads Custom's complete guest cache or restores spent admission counters.
    optional_persistent = Settings::values.use_disk_shader_cache.GetValue();
    optional_directory = parent.GetPipelineCacheDir();
    if (optional_persistent) LoadUsefulRecipes();

    // AstraEH: A test profile must not warm or write the user's specialized shader cache.
    // Driver compilation data is managed separately by PipelineCache and is retained.
    if (!Settings::values.use_disk_shader_cache ||
        Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom)
        return;

    auto new_callback = MakeThrottledCallback(callback);

    if (!stop_loading && !InitVSCache(stop_loading, new_callback)) {
        RecreateCache(vs_cache, CacheFileType::VS_CACHE);
    }
    if (!stop_loading && !InitFSCache(stop_loading, new_callback)) {
        RecreateCache(fs_cache, CacheFileType::FS_CACHE);
    }
    if (!stop_loading && !InitGSCache(stop_loading, new_callback)) {
        RecreateCache(gs_cache, CacheFileType::GS_CACHE);
    }
    if (!stop_loading && !InitPLCache(stop_loading, new_callback)) {
        RecreateCache(pl_cache, CacheFileType::PL_CACHE);
    }
    // AstraEH: Snapshot objects reconstructed at load; their builds may still be queued.
    startup_host_pipelines = graphics_pipelines.size();
    startup_guest_records = known_graphic_pipelines.size();
}

u64 ShaderDiskCache::OptionalCompilerIdentity() const {
    // CodexAstraLocal: Include generator ABI/source and pinned compiler revision;
    // exact config/profile/program kind are separately stored in each record.
    const std::string_view source{Common::g_shader_cache_version}, revision{Common::g_scm_rev};
    return Common::HashCombine(OptionalDiskCache::Schema, FSConfig::StructHash(),
        Common::ComputeHash64(source.data(), source.size()),
        Common::ComputeHash64(revision.data(), revision.size()));
}

bool ShaderDiskCache::OptionalWriteAllowed(const std::string& directory,
                                          const std::string& path, u64 bytes) {
    // CodexAstraLocal: Fixed collision slots bound each title to 128 full/32
    // static records. A worker-only global scan additionally caps optional data
    // at 512 MiB; reaching it refuses new writes rather than deleting other titles.
    constexpr u64 limit = 512ULL * 1024 * 1024;
    if (!optional_quota_scanned) {
        optional_disk_bytes = 0;
        u64 visited{};
        const bool complete = FileUtil::ForeachDirectoryEntry(nullptr, directory,
            [&](u64*, const std::string& parent_path, const std::string& name) {
                if (++visited > 65536) return false;
                if (name.find("-uber-opt-") != std::string::npos)
                    optional_disk_bytes += std::min(FileUtil::GetSize(parent_path + name), limit);
                return optional_disk_bytes <= limit;
            });
        if (!complete) optional_disk_bytes = limit;
        optional_quota_scanned = true;
    }
    const u64 old_size = FileUtil::Exists(path) ? std::min(FileUtil::GetSize(path), optional_disk_bytes) : 0;
    if (bytes > limit || optional_disk_bytes - old_size > limit - bytes) {
        ++optional_disk_quota_refusals;
        return false;
    }
    optional_disk_bytes = optional_disk_bytes - old_size + bytes;
    return true;
}

std::vector<u32> ShaderDiskCache::LoadOrCompileOptional(std::span<const u32> identity,
    bool partial, const std::function<std::vector<u32>()>& compile) {
    const auto compiler = OptionalCompilerIdentity();
    const auto identity_hash = Common::ComputeHash64(identity.data(), identity.size_bytes());
    const auto path = fmt::format("{}{:016X}-uber-opt-{}-{:03X}.spv", optional_directory,
        title_id, partial ? "static" : "full", identity_hash % (partial ? 32 : 128));
    if (optional_persistent) try {
        FileUtil::IOFile file(path, "rb");
        if (file) {
            const auto bytes = file.GetSize();
            if (bytes && bytes <= OptionalDiskCache::MaxBytes && bytes % sizeof(u32) == 0) {
                std::vector<u32> words(bytes / sizeof(u32));
                if (file.ReadBytes(words.data(), bytes) == bytes) {
                    auto spirv = OptionalDiskCache::Decode(identity, compiler, words);
                    if (!spirv.empty()) { ++optional_disk_hits; return spirv; }
                }
            }
            ++optional_disk_rejected;
        }
    } catch (...) { ++optional_disk_rejected; }
    ++optional_disk_misses;
    auto spirv = compile();
    if (optional_persistent && !spirv.empty()) try {
        const auto words = OptionalDiskCache::Encode(identity, compiler, spirv);
        if (!words.empty() && OptionalWriteAllowed(optional_directory, path, words.size() * sizeof(u32))) {
            const auto temporary = path + ".tmp";
            FileUtil::IOFile file(temporary, "wb");
            const bool written = file && file.WriteBytes(words.data(), words.size() * sizeof(u32)) ==
                words.size() * sizeof(u32) && file.Flush();
            file.Close();
            if (written && FileUtil::Rename(temporary, path)) ++optional_disk_writes;
            else { FileUtil::Delete(temporary); ++optional_disk_write_failures; }
        }
    } catch (...) { ++optional_disk_write_failures; }
    return spirv;
}

void ShaderDiskCache::ReportOptionalFailure(const char* phase, const char* reason, unsigned kind) noexcept {
    // CodexAstraLocal: Production error detail is independently bounded; throwing
    // log delivery must never prevent failure/completion publication.
    if (optional_error_details.fetch_add(1, std::memory_order_relaxed) >= 8) return;
    try {
        LOG_ERROR(Render_Vulkan, "Uberhar optional compile failure: phase={} kind={} error={} limit=8",
                  phase, kind, reason);
    } catch (...) {}
}

bool ShaderDiskCache::WarmDemand(u64 hash, bool partial) const {
    // CodexAstraLocal: Fixed direct lookup keeps cold misses cheap; collisions
    // can lose advisory replay priority, never alias executable programs.
    const auto& recipe = warm_recipes[hash % warm_recipes.size()];
    return recipe.hash == hash && recipe.partial == partial && recipe.selections >= 16;
}

void ShaderDiskCache::LoadUsefulRecipes() {
    // CodexAstraLocal: Positive demand records merely prioritize an encountered
    // exact current key. They never enqueue startup work or restore old counters.
    try {
        const auto path = fmt::format("{}{:016X}-uber-opt-recipes.bin", optional_directory, title_id);
        FileUtil::IOFile file(path, "rb");
        constexpr std::size_t header = 8, row = 5, count = 64;
        std::array<u32, header + row * count> words{};
        if (!file || file.GetSize() != sizeof(words) || file.ReadBytes(words.data(), sizeof(words)) != sizeof(words)) return;
        const auto profile_words = OptionalProfileWords(parent.profile);
        const u64 profile_hash = Common::ComputeHash64(profile_words.data(), profile_words.size() * sizeof(u32));
        if (words[0] != 0x55524543 || words[1] != 1 ||
            UberharSpirvCache::Join(words[2], words[3]) != OptionalCompilerIdentity() ||
            UberharSpirvCache::Join(words[4], words[5]) != profile_hash ||
            UberharSpirvCache::Join(words[6], words[7]) != Common::ComputeHash64(words.data() + header, row * count * sizeof(u32))) return;
        for (std::size_t i = 0; i < count; ++i) {
            const auto* item = words.data() + header + row * i;
            if (item[4] > 1) { warm_recipes = {}; return; }
            const UsefulRecipe recipe{UberharSpirvCache::Join(item[0], item[1]),
                UberharSpirvCache::Join(item[2], item[3]), item[4] != 0};
            auto& bucket = warm_recipes[recipe.hash % warm_recipes.size()];
            if (recipe.selections > bucket.selections) bucket = recipe;
        }
    } catch (...) { warm_recipes = {}; }
}

void ShaderDiskCache::RecordOptionalSelection(const Shader* shader) {
    // CodexAstraLocal: Actual selected owners carry their own cheap counter.
    if (shader) shader->RecordOptionalSelection();
}

void ShaderDiskCache::SaveUsefulRecipesAfterDrain() {
    if (!optional_persistent) return;
    try {
        std::vector<UsefulRecipe> useful;
        for (const auto& recipe : warm_recipes) if (recipe.selections) useful.push_back(recipe);
        for (const auto& [hash, entry] : ready_fragments)
            if (entry->shader.OptionalSelections() && !entry->shader.HasFailed() && entry->key.profile == parent.profile)
                useful.push_back({hash, entry->shader.OptionalSelections(), false});
        for (const auto& [hash, entry] : static_tev_fragments)
            if (entry->shader.OptionalSelections() && !entry->shader.HasFailed() && entry->key.profile == parent.profile)
                useful.push_back({hash, entry->shader.OptionalSelections(), true});
        std::sort(useful.begin(), useful.end(), [](const auto& a, const auto& b) { return a.selections > b.selections; });
        constexpr std::size_t header = 8, row = 5, count = 64;
        std::array<u32, header + row * count> words{};
        const auto profile_words = OptionalProfileWords(parent.profile);
        const u64 profile_hash = Common::ComputeHash64(profile_words.data(), profile_words.size() * sizeof(u32));
        const auto compiler = OptionalCompilerIdentity();
        words[0] = 0x55524543; words[1] = 1;
        words[2] = static_cast<u32>(compiler); words[3] = compiler >> 32;
        words[4] = static_cast<u32>(profile_hash); words[5] = profile_hash >> 32;
        for (std::size_t i = 0; i < std::min(useful.size(), count); ++i) {
            const auto& item = useful[i]; auto* out = words.data() + header + row * i;
            out[0] = static_cast<u32>(item.hash); out[1] = item.hash >> 32;
            out[2] = static_cast<u32>(item.selections); out[3] = item.selections >> 32; out[4] = item.partial;
        }
        const u64 checksum = Common::ComputeHash64(words.data() + header, row * count * sizeof(u32));
        words[6] = static_cast<u32>(checksum); words[7] = checksum >> 32;
        const auto path = fmt::format("{}{:016X}-uber-opt-recipes.bin", optional_directory, title_id);
        if (!OptionalWriteAllowed(optional_directory, path, sizeof(words))) return;
        const auto temporary = path + ".tmp";
        FileUtil::IOFile file(temporary, "wb");
        const bool written = file && file.WriteBytes(words.data(), sizeof(words)) == sizeof(words) && file.Flush();
        file.Close();
        if (!written || !FileUtil::Rename(temporary, path)) { FileUtil::Delete(temporary); ++optional_disk_write_failures; }
    } catch (...) { ++optional_disk_write_failures; }
}

void ShaderDiskCache::ReclaimOptionalAfterDrain(u64 frame) {
    optional_frame = frame;
    const auto now = CompileBudget::NowMs();
    full_module_credit.Refill(now);
    static_module_credit.Refill(now);
    if (OptionalFragmentPending()) return;
    // CodexAstraLocal: CPU/GPU pipeline maps retain raw stage pointers. Eviction
    // requires no remaining pipeline, current-state or compiler reference; stale
    // execution keys therefore cannot survive allocator-address reuse.
    const auto reclaim = [&](auto& entries, auto& failures, bool& pressure, u64& counter) {
        if (!pressure) return;
        auto victim = entries.end();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            const auto& entry = *it->second;
            if (!entry.compiler_released.load(std::memory_order_acquire) ||
                frame - entry.last_requested < 8 || parent.OptionalShaderReferenced(&entry.shader)) continue;
            bool required = false;
            for (const auto& [key, pipeline] : graphics_pipelines)
                required |= pipeline && pipeline->ReferencesShader(&entry.shader);
            if (required) continue;
            if (victim == entries.end() || entry.last_requested < victim->second->last_requested) victim = it;
        }
        if (victim != entries.end()) {
            if (warming_ready_fragment == &victim->second->shader) warming_ready_fragment = nullptr;
            if (victim->second->shader.HasFailed()) {
                const auto& entry = *victim->second;
                failures.Remember(victim->first, entry.key, entry.retries, entry.retryable, entry.retry_after_ms);
            }
            const auto selections = victim->second->shader.OptionalSelections();
            auto& remembered = warm_recipes[victim->first % warm_recipes.size()];
            if (selections > remembered.selections)
                remembered = {victim->first, selections, &pressure == &static_module_pressure};
            // CodexAstraLocal: Negative GPU keys contain process-local stage
            // addresses. Drop those advisory records before allocator reuse can
            // make an unrelated module inherit a previous failed owner identity.
            parent.ready_gpu_failures = {};
            entries.erase(victim);
            ++counter;
            pressure = false;
        } else {
            // CodexAstraLocal: Modules pinned by old GPU PSOs request safe PSO
            // retirement first; no module is detached from a live dependency.
            parent.ready_gpu_pressure = true;
        }
    };
    reclaim(ready_fragments, full_failure_history, full_module_pressure, optional_full_evictions);
    reclaim(static_tev_fragments, static_failure_history, static_module_pressure, optional_static_evictions);
}

u64 ShaderDiskCache::CaptureVertexSourceHash(const Shader* shader) const noexcept {
    // CodexAstraLocal: Optional vertex maps are capped; never scan Custom's
    // unrestricted map on behalf of this diagnostic.
    if (programmable_vertex_cache.size() > ReadyVertexPolicy::MaxPrograms)
        return 0;
    for (const auto& [source, candidate] : programmable_vertex_cache) {
        if (&candidate == shader)
            return source;
    }
    return 0;
}

std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseProgrammableVertexShader(
    const Pica::RegsInternal& regs, Pica::ShaderSetup& setup, const VertexLayout& layout,
    bool ready_only) {

    PicaVSConfig config{regs, setup};

    // Transfer vertex attributes to the VS config
    config.state.used_input_vertex_attributes = layout.attribute_count;
    for (u32 i = 0; i < layout.attribute_count; i++) {
        auto& dst = config.state.input_vertex_attributes[i];
        const auto& src = layout.attributes[i];

        dst.location = src.location;
        dst.type = static_cast<u8>(src.type.Value());
        dst.size = src.size;
    }

    // CodexAstraLocal: The JIT-dot extra policy is immutable for this renderer
    // and its title maps. Keep the guest-only config identity stable; generated
    // source hashes distinguish precise/legacy modules, and Combo never loads
    // or writes Custom's transferable records (Init). No cross-policy map reuse.
    const auto config_hash = config.Hash();

    // AstraPro: Only absent configs consume admission. Existing successful or
    // failed entries remain stable; a failed translation must not repeat per draw.
    if (ready_only && !programmable_vertex_map.contains(config_hash)) {
        if (programmable_vertex_map.size() >= ReadyVertexPolicy::MaxPrograms) {
            ++ready_vs_capped;
            return {};
        }
        if (warming_ready_vs && !warming_ready_vs->IsDone()) {
            ++ready_vs_deferred;
            return {};
        }
    }
    const auto [iter_config, new_config] = programmable_vertex_map.try_emplace(config_hash);
    if (new_config) {

        LOG_NEW_OBJECT(Render_Vulkan, "New VS config {:016X}", config_hash);

        ExtraVSConfig extra_config = parent.CalcExtraConfig(config);

        // AstraEH: This translation is foreground work; distinguish it from driver compilation.
        const auto codegen_start = std::chrono::steady_clock::now();
        auto program =
            Common::HashableString(GLSL::GenerateVertexShader(setup, config, extra_config));
        const auto codegen_ns =
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - codegen_start)
                                 .count());
        ++live_vs_codegen_count;
        live_vs_codegen_ns += codegen_ns;
        live_vs_codegen_max_ns = std::max(live_vs_codegen_max_ns, codegen_ns);

        if (program.empty()) {
            // AstraPro Log Line: Optional failed configs are retained, bounded
            // by 128 entries, instead of repeatedly decompiling an unsupported VS.
            LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
            if (!ready_only)
                programmable_vertex_map.erase(config_hash);
            return {};
        }

        const u64 spirv_id = program.Hash();

        auto [iter_prog, new_program] =
            programmable_vertex_cache.try_emplace(spirv_id, parent.instance);
        auto& shader = iter_prog->second;

        if (new_program) {
            LOG_NEW_OBJECT(Render_Vulkan, "New VS SPIRV {:016X}", spirv_id);

            shader.program = std::move(program);
            const vk::Device device = parent.instance.GetDevice();
            // CodexAstraUlt: Replace the inherited mutable/global compiler choice for
            // this queued job with a frozen option. Only ready-only VS jobs optimize;
            // their renderer-scoped maps never load/write Custom's vkch files (Init).
            // CodexAstraLocal: Capture the resolved option before queueing so a later
            // settings change cannot alter this module. Required VS jobs still use
            // their requested setting; readiness/failure handling remains below.
            const bool disable_optimizer =
                DisableShaderOptimizer(ready_only, parent.profile.vk_disable_spirv_optimizer != 0);
            if (ready_only)
                warming_ready_vs = &shader;
            try { parent.shader_workers.QueueWork([device, &shader, this, spirv_id, ready_only,
                                             disable_optimizer] {
                const auto compile = [&] {
                    auto spirv = CompileGLSL(shader.program, vk::ShaderStageFlagBits::eVertex,
                                            "", disable_optimizer);
                    if (spirv.empty())
                        throw std::runtime_error("empty vertex module");
                    AppendVSSPIRV(vs_cache, spirv, spirv_id);
                    shader.program.clear();
                    shader.module = CompileSPV(spirv, device);
                    if (!shader.module)
                        throw std::runtime_error("null vertex module");
                };
                if (!ready_only) {
                    // CodexAstraUlt-2: A failed mandatory VS must wake dependent pipelines.
                    VideoCore::CompleteShaderBuild(shader, compile, [this](const char* reason) {
                        if (mandatory_shader_failures.fetch_add(1) < 8) {
                            // CodexAstraUlt-2 Log Line: Eight mandatory shader failures per title.
                            LOG_ERROR(Render_Vulkan, "Uberhar mandatory VS failed: error={} limit=8",
                                      reason);
                        }
                    });
                    return;
                }
                // CodexAstraLocal: Unknown compiler and logger failures obey
                // the same terminal publication rule as mandatory dependencies.
                VideoCore::CompleteShaderBuild(shader, compile, [this](const char* reason) {
                    if (ready_shader_failures.fetch_add(1) < 8)
                        LOG_ERROR(Render_Vulkan, "Uberhar optional GPU VS failed: {}", reason);
                });
            }); } catch (...) {
                // CodexAstraLocal: Enqueue failure cannot leave dependent PSOs waiting forever.
                shader.MarkFailed();
                ready_shader_failures.fetch_add(1);
            }
        }

        AppendVSConfigProgram(vs_cache, config, setup, config_hash, spirv_id);

        iter_config->second = &shader;
    }

    Shader* const shader{iter_config->second};
    if (!shader) {
        if (!ready_only)
            LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
        return {};
    }

    return std::make_pair(config_hash, shader);
}

std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseFragmentShader(
    const FSConfig& fs_config, const Pica::Shader::UserConfig& user) {
    const auto fs_config_hash = fs_config.Hash();
    const auto [it, new_shader] = fragment_shaders.try_emplace(fs_config_hash, parent.instance);
    auto& shader = it->second;

    if (new_shader) {
        LOG_NEW_OBJECT(Render_Vulkan, "New FS config {:016X}", fs_config_hash);

        // CodexAstraUlt-2: Freeze inputs before the compiler worker resumes.
        try { parent.shader_workers.QueueWork([fs_config, user, this, &shader, fs_config_hash,
                                         profile = parent.profile]() {
            // CodexAstraUlt-2: Publish only after generation, module creation and
            // cache scheduling finish; never expose a null module as success.
            VideoCore::CompleteShaderBuild(shader, [&] {
                std::vector<u32> spirv;
                const bool use_spirv = profile.vk_use_spirv_generator;
                if (use_spirv && !fs_config.UsesSpirvIncompatibleConfig()) {
                    spirv = SPIRV::GenerateFragmentShader(fs_config, profile);
                } else {
                    const std::string code =
                        GLSL::GenerateFragmentShader(fs_config, user, profile);
                    spirv = CompileGLSL(code, vk::ShaderStageFlagBits::eFragment, "",
                                        profile.vk_disable_spirv_optimizer != 0);
                }
                shader.module = CompileSPV(spirv, parent.instance.GetDevice());
                if (shader.module && user.IsCacheable()) {
                    AppendFSSPIRV(fs_cache, spirv, fs_config_hash);
                    FSConfigEntry entry{.version = FSConfigEntry::EXPECTED_VERSION,
                                        .fs_config = fs_config};
                    AppendFSConfig(fs_cache, entry, fs_config_hash);
                }
            }, [this](const char* reason) {
                if (mandatory_shader_failures.fetch_add(1) < 8) {
                    // CodexAstraUlt-2 Log Line: Eight mandatory shader failures per title.
                    LOG_ERROR(Render_Vulkan, "Uberhar mandatory FS failed: error={} limit=8", reason);
                }
            });
        }); } catch (...) {
                // CodexAstraLocal: Enqueue failure cannot leave dependent PSOs waiting forever.
                shader.MarkFailed();
                ready_shader_failures.fetch_add(1);
            }
    }

    return std::make_pair(fs_config_hash, &shader);
}

// AstraPro: Compile the inherited specialized fragment implementation, not a
// relaxed approximation. CPU fallback continues until both this module and its
// GPU pipeline are ready. Independent optional records never alter stock vkch.
std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseReadyFragmentShader(
    const FSConfig& config, const Pica::Shader::UserConfig& user, bool allow_build) {
    ++ready_fragment_requests;
    if (!user.IsCacheable()) {
        ++ready_fragment_unsupported;
        return {};
    }
    const u64 hash = config.Hash();
    u32 retry_count{};
    if (const auto it = ready_fragments.find(hash); it != ready_fragments.end()) {
        auto& entry = *it->second;
        // AstraPro: Compare immutable snapshots without copying a full profile
        // on every ready-cache hit. Collision handling still checks both values.
        if (!(entry.key.config == config) || !(entry.key.profile == parent.profile)) {
            ++ready_fragment_mismatches;
            return {}; // AstraPro: Full equality, not a hash, admits reuse.
        }
        if (!entry.shader.HasFailed()) entry.last_requested = optional_frame;
        auto& shader = entry.shader;
        if (!shader.IsDone()) {
            ++ready_fragment_busy;
            return {};
        }
        if (shader.HasFailed() || !shader.Handle()) {
            ++ready_fragment_failed_hits;
            // CodexAstraLocal: Read-only probes never retry. The caller must
            // re-establish downstream capacity and current exact-key demand.
            if (!allow_build || !entry.retryable || entry.retries >= 2 ||
                CompileBudget::NowMs() < optional_circuit_until_ms.load(std::memory_order_relaxed) ||
                CompileBudget::NowMs() < entry.retry_after_ms || !full_module_credit.Available() ||
                (parent.warming_ready_vertex && !parent.warming_ready_vertex->IsDone()) || OptionalFragmentPending() ||
                !entry.compiler_released.load(std::memory_order_acquire) ||
                parent.OptionalShaderReferenced(&shader)) return {};
            retry_count = entry.retries + 1;
            if (warming_ready_fragment == &shader) warming_ready_fragment = nullptr;
            ready_fragments.erase(it);
            ++optional_retry_jobs;
        } else {
            // CodexAstraLocal: Clear a recovered resource failure once, after
            // the final worker access; ordinary ready reuse adds no table scan.
            if (entry.retries && entry.compiler_released.load(std::memory_order_acquire)) {
                full_failure_history.Forget(hash, entry.key);
                entry.retries = 0;
            }
            ++ready_fragment_hits;
            return std::make_pair(hash, &shader);
        }
    }
    // CodexAstraLocal: A read-only probe can reuse a ready module above without
    // creating demand or compilation on a miss. CPU admission starts with this
    // probe before checking the remaining PSO/work capacity.
    if (!allow_build) {
        ++ready_fragment_lookup_only_misses;
        return {};
    }
    if (const auto until = optional_circuit_until_ms.load(std::memory_order_relaxed);
        until && CompileBudget::NowMs() < until) return {};
    const ReadyFragmentKey key{config, parent.profile};
    if (const auto* failure = full_failure_history.Find(hash, key)) {
        if (!failure->retryable || failure->retries >= 2 ||
            CompileBudget::NowMs() < failure->retry_after_ms) return {};
        retry_count = std::max(retry_count, failure->retries + 1);
    }
    if (!retry_count && !WarmDemand(hash, false) && !ready_fragment_demand.Observe(hash, key)) {
        ++ready_fragment_cold;
        return {};
    }
    // CodexAstraLocal: Full and partial optional fragments share the existing
    // one-thread optional pipeline lane. Neither queues behind an unfinished
    // optional PSO; required generic preparation uses its existing separate lane.
    const bool pending = (warming_ready_fragment && !warming_ready_fragment->IsDone()) ||
        (parent.warming_ready_vertex && !parent.warming_ready_vertex->IsDone());
    if (!parent.ready_vertex_worker || !full_module_credit.Available() ||
        !ReadyFragmentPolicy::CanAdmit(ready_fragments.size(), pending)) {
        full_module_pressure |= ready_fragments.size() >= ReadyFragmentPolicy::MaxModules;
        if (pending) ++ready_fragment_busy;
        else ++ready_fragment_capped;
        return {};
    }
    full_module_credit.Spend();
    ReadyFragmentEntry* entry{};
    try {
        auto object = std::make_unique<ReadyFragmentEntry>(parent.instance, key);
        entry = object.get();
        ready_fragments.emplace(hash, std::move(object));
        entry->last_requested = optional_frame;
        entry->retries = retry_count;
    } catch (...) {
        ++optional_resource_failures;
        full_failure_history.Remember(hash, key, retry_count, true, CompileBudget::NowMs() + 2000);
        return {};
    }
    warming_ready_fragment = &entry->shader;
    // AstraPro: All compiler inputs are immutable owned snapshots. A mutable
    // renderer profile must not be read by this worker while emulation resumes.
    const auto device = parent.instance.GetDevice();
    // CodexAstraLocal: Optional queue allocation can fail after owner insertion.
    // Publish failed completion so every route keeps its existing complete draw.
    try {
        // CodexAstraLocal: Reuse the existing optional lane; this ready-only
        // module is never a dependency of an already queued waiting PSO.
        parent.ready_vertex_worker->QueueWork([this, entry, device] {
            const auto start = std::chrono::steady_clock::now();
            bool failed = false;
            try {
                const auto& [fs, profile] = entry->key;
                // CodexAstraLocal: Honor the ordinary full-shader generator choice
                // using the entry's immutable Profile and identical compatibility
                // guard. Both vertex routes retain the same fragment interface;
                // the caller's existing barycentric/custom checks stay in force.
                // The optional lane never blocks the foreground or changes its
                // complete generic/static fallback while this module is pending.
                const auto identity = OptionalIdentity(fs, profile, nullptr);
                auto spirv = LoadOrCompileOptional(identity, false, [&] {
                std::vector<u32> generated;
                if (profile.vk_use_spirv_generator && !fs.UsesSpirvIncompatibleConfig()) {
                    ++ready_fragment_spirv_builds;
                    generated = SPIRV::GenerateFragmentShader(fs, profile);
                } else {
                    ++ready_fragment_glsl_builds;
                    const auto code = GLSL::GenerateFragmentShader(fs, {}, profile);
                    // CodexAstraLocal: Keep the inherited optional GLSL optimizer
                    // policy. Direct SPIR-V has no GLSL frontend/optimizer step;
                    // G1 pruning applies only to this full cacheable GLSL branch.
                    generated = CompileGLSL(code, vk::ShaderStageFlagBits::eFragment, "",
                        DisableShaderOptimizer(true, profile.vk_disable_spirv_optimizer != 0));
                }
                return generated;
                });
                if (spirv.empty()) throw OptionalCompileError(OptionalCompileFailure::Source, "empty optional fragment module");
                entry->shader.module = CompileOptionalSPV(spirv, device);
                if (!entry->shader.module) throw std::runtime_error("null optional fragment module");
            } catch (const OptionalCompileError& error) {
                failed = true;
                ReportOptionalFailure("module", error.what(), static_cast<unsigned>(error.kind));
                ++ready_fragment_failures;
                entry->retryable = error.kind == OptionalCompileFailure::Resource;
                if (entry->retryable) ++optional_resource_failures;
                else if (error.kind == OptionalCompileFailure::Source) ++optional_source_failures;
                else if (error.kind == OptionalCompileFailure::DeviceLost) ++optional_device_failures;
                else ++optional_unknown_failures;
                entry->retry_after_ms = CompileBudget::NowMs() + (2000ULL << entry->retries);
            } catch (const std::bad_alloc&) {
                failed = entry->retryable = true;
                ReportOptionalFailure("allocation", "temporary compiler allocation failure", 1);
                ++ready_fragment_failures;
                ++optional_resource_failures;
                entry->retry_after_ms = CompileBudget::NowMs() + (2000ULL << entry->retries);
            } catch (const std::exception& error) {
                failed = true;
                ++optional_unknown_failures;
                if (ready_fragment_failures.fetch_add(1) < 8) {
                    // AstraPro Log Line: Bound failure details; the CPU still renders.
                    // CodexAstraLocal: Diagnostic allocation failure cannot escape a
                    // compiler worker and prevent terminal completion publication.
                    try {
                        LOG_ERROR(Render_Vulkan, "Uberhar optional GPU fragment failed: {}", error.what());
                    } catch (...) {}
                }
            } catch (...) {
                // CodexAstraLocal: ThreadWorker has no task exception boundary.
                // An opaque optional compiler failure still publishes generic recovery.
                failed = true;
                if (ready_fragment_failures.fetch_add(1) < 8) {
                    try {
                        // CodexAstraLocal Log Line: Share the existing eight-failure budget.
                        LOG_ERROR(Render_Vulkan, "Uberhar optional fragment failed: unknown exception");
                    } catch (...) {}
                }
            }
            const u64 ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - start).count();
            ready_fragment_compile_ns.fetch_add(ns);
            // AstraPro: Admission permits one optional fragment compiler at a time;
            // keep atomic stats so progress reporting never races its completion.
            auto maximum = ready_fragment_max_compile_ns.load();
            while (maximum < ns && !ready_fragment_max_compile_ns.compare_exchange_weak(maximum, ns)) {}
            ready_fragment_builds.fetch_add(1);
            if (failed) {
                if (optional_failure_streak.fetch_add(1) >= 7)
                    optional_circuit_until_ms.store(CompileBudget::NowMs() + 30000);
                entry->shader.MarkFailed();
            } else {
                optional_failure_streak.store(0);
                entry->shader.MarkDone();
            } // Publish the module only after all writes.
            entry->compiler_released.store(true, std::memory_order_release);
        });
    } catch (...) {
        entry->retryable = true;
        entry->retry_after_ms = CompileBudget::NowMs() + (2000ULL << entry->retries);
        entry->shader.MarkFailed();
        ready_fragment_failures.fetch_add(1);
        entry->compiler_released.store(true, std::memory_order_release);
        return {};
    }
    ++ready_fragment_busy;
    return {};
}

// CodexAstraLocal: Optional partial modules retain exact immutable inputs and
// terminal completion. A miss never waits or changes the generic shader.
// Successful programs use independent disk records; the CPU-FS ABI stays exact.
std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseStaticTevFragmentShader(
    const FSConfig& family, const GLSL::StaticTevPlan& plan,
    const UserConfig& user, bool allow_build) {
    ++static_tev_requests;
    if (!user.IsCacheable() || !parent.allow_static_cpu_tev ||
        !parent.ready_vertex_worker || parent.profile.has_fragment_shader_barycentric ||
        GLSL::CheckDynamicTevSupport(family, user) != GLSL::DynamicTevSupport::Ready) {
        ++static_tev_unsupported;
        return {};
    }
    const u64 hash = StaticTevPolicy::Key::Hash(family, plan);
    u32 retry_count{};
    if (const auto found = static_tev_fragments.find(hash); found != static_tev_fragments.end()) {
        auto& entry = *found->second;
        // CodexAstraLocal: Avoid copying the large profile on ready hits, while
        // retaining every equality that a full key comparison would require.
        if (!(entry.key.family == family) || !(entry.key.profile == parent.profile) ||
            !(entry.key.plan == plan) || entry.key.interface_abi != StaticTevPolicy::CpuInterface ||
            entry.key.policy != StaticTevPolicy::GeneratorPolicy) {
            ++static_tev_mismatches;
            return {};
        }
        if (!entry.shader.HasFailed()) entry.last_requested = optional_frame;
        if (!entry.shader.IsDone()) { ++static_tev_busy; return {}; }
        if (entry.shader.HasFailed() || !entry.shader.Handle()) {
            ++static_tev_failed_hits;
            if (!allow_build || !entry.retryable || entry.retries >= 2 ||
                CompileBudget::NowMs() < optional_circuit_until_ms.load(std::memory_order_relaxed) ||
                CompileBudget::NowMs() < entry.retry_after_ms || !static_module_credit.Available() ||
                (parent.warming_ready_vertex && !parent.warming_ready_vertex->IsDone()) || OptionalFragmentPending() ||
                !entry.compiler_released.load(std::memory_order_acquire) ||
                parent.OptionalShaderReferenced(&entry.shader)) return {};
            retry_count = entry.retries + 1;
            if (warming_ready_fragment == &entry.shader) warming_ready_fragment = nullptr;
            static_tev_fragments.erase(found);
            ++optional_retry_jobs;
        } else {
            // CodexAstraLocal: A successful bounded retry supersedes its negative record.
            if (entry.retries && entry.compiler_released.load(std::memory_order_acquire)) {
                static_failure_history.Forget(hash, entry.key);
                entry.retries = 0;
            }
            ++static_tev_hits;
            return std::make_pair(hash, &entry.shader);
        }
    }
    if (!allow_build) { ++static_tev_lookup_misses; return {}; }
    if (const auto until = optional_circuit_until_ms.load(std::memory_order_relaxed);
        until && CompileBudget::NowMs() < until) return {};
    if (static_tev_fragments.size() >= StaticTevPolicy::MaxModules ||
        !static_module_credit.Available()) {
        static_module_pressure |= static_tev_fragments.size() >= StaticTevPolicy::MaxModules;
        ++static_tev_capped;
        return {};
    }
    const StaticTevPolicy::Key key{family, parent.profile, plan};
    if (const auto* failure = static_failure_history.Find(hash, key)) {
        if (!failure->retryable || failure->retries >= 2 ||
            CompileBudget::NowMs() < failure->retry_after_ms) return {};
        retry_count = std::max(retry_count, failure->retries + 1);
    }
    if (!retry_count && !WarmDemand(hash, true) && !static_tev_demand.Observe(hash, key)) { ++static_tev_cold; return {}; }
    if ((warming_ready_fragment && !warming_ready_fragment->IsDone()) ||
        (parent.warming_ready_vertex && !parent.warming_ready_vertex->IsDone())) {
        ++static_tev_busy;
        return {};
    }
    // CodexAstraLocal: Charge before every throwing owner allocation/insertion.
    // Failures spend work credit even without a retained owner. Only classified
    // temporary failures receive bounded demand-triggered retries.
    static_module_credit.Spend();
    ++static_tev_attempts;
    StaticTevEntry* entry{};
    try {
        auto owner = std::make_unique<StaticTevEntry>(parent.instance, key);
        entry = owner.get();
        static_tev_fragments.emplace(hash, std::move(owner));
        entry->last_requested = optional_frame;
        entry->retries = retry_count;
    } catch (...) {
        ++static_tev_allocation_failures;
        static_failure_history.Remember(hash, key, retry_count, true, CompileBudget::NowMs() + 2000);
        return {};
    }
    warming_ready_fragment = &entry->shader;
    const auto device = parent.instance.GetDevice();
    try {
        parent.ready_vertex_worker->QueueWork([this, entry, device] {
            bool failed = false;
            try {
                const auto elapsed = [](auto begin) {
                    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - begin).count());
                };
                // CodexAstraLocal: Every compiler input comes from this stable
                // owner. Lighting, textures and framebuffer values still arrive
                // through the unchanged per-draw 128-byte push transport.
                const auto& key = entry->key;
                auto begin = std::chrono::steady_clock::now();
                const auto identity = OptionalIdentity(key.family, key.profile, &key.plan);
                const auto spirv = LoadOrCompileOptional(identity, true, [&] {
                begin = std::chrono::steady_clock::now();
                const auto source = GLSL::GenerateStaticTevFragmentShader(
                    key.family, {}, key.profile, key.plan);
                entry->generation_ns = elapsed(begin);
                entry->source_bytes = source.size();
                begin = std::chrono::steady_clock::now();
                // CodexAstraLocal: Partial specialization is optional work and
                // explicitly uses the optimized frontend, even though non-TEV
                // generation remains dynamic. Required generic policy is intact.
                const auto generated = CompileGLSL(source, vk::ShaderStageFlagBits::eFragment, "",
                    DisableShaderOptimizer(true, key.profile.vk_disable_spirv_optimizer != 0));
                entry->frontend_ns = elapsed(begin);
                return generated;
                });
                entry->spirv_bytes = spirv.size() * sizeof(u32);
                if (spirv.empty()) throw OptionalCompileError(OptionalCompileFailure::Source, "empty static TEV module");
                begin = std::chrono::steady_clock::now();
                entry->shader.module = CompileOptionalSPV(spirv, device);
                entry->module_ns = elapsed(begin);
                if (!entry->shader.module) throw std::runtime_error("null static TEV module");
            } catch (const OptionalCompileError& error) {
                failed = true;
                ReportOptionalFailure("module", error.what(), static_cast<unsigned>(error.kind));
                entry->retryable = error.kind == OptionalCompileFailure::Resource;
                if (entry->retryable) ++optional_resource_failures;
                else if (error.kind == OptionalCompileFailure::Source) ++optional_source_failures;
                else if (error.kind == OptionalCompileFailure::DeviceLost) ++optional_device_failures;
                else ++optional_unknown_failures;
                entry->retry_after_ms = CompileBudget::NowMs() + (2000ULL << entry->retries);
            } catch (const std::bad_alloc&) {
                failed = entry->retryable = true;
                ReportOptionalFailure("allocation", "temporary compiler allocation failure", 1);
                ++optional_resource_failures;
                entry->retry_after_ms = CompileBudget::NowMs() + (2000ULL << entry->retries);
            } catch (...) {
                failed = true;
                ReportOptionalFailure("module", "unknown compiler failure", 3);
                ++optional_unknown_failures;
            }
            // CodexAstraLocal: Completion is the last owner write; progress reads
            // metrics only after acquiring it, and the parent drains this lane
            // before any shader, layout, pipeline or driver-cache destruction.
            if (failed) {
                if (optional_failure_streak.fetch_add(1) >= 7)
                    optional_circuit_until_ms.store(CompileBudget::NowMs() + 30000);
                entry->shader.MarkFailed();
            } else {
                optional_failure_streak.store(0);
                entry->shader.MarkDone();
            }
            entry->compiler_released.store(true, std::memory_order_release);
        });
    } catch (...) {
        entry->retryable = true;
        entry->retry_after_ms = CompileBudget::NowMs() + (2000ULL << entry->retries);
        entry->shader.MarkFailed();
        entry->compiler_released.store(true, std::memory_order_release);
        return {};
    }
    ++static_tev_busy;
    return {};
}

std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseFixedGeometryShader(
    const Pica::RegsInternal& regs, bool ready_only) {

    const PicaFixedGSConfig gs_config{regs};
    const auto gs_config_hash = gs_config.Hash();

    if (!parent.instance.UseGeometryShaders() ||
        parent.instance.IsFragmentShaderBarycentricSupported()) {
        // Even if we don't support geometry shaders, we still need to cache them
        // so that the shader cache is transferable. There is no need to cache
        // or build the SPIRV.

        if (known_geometry_shaders.emplace(gs_config_hash).second) {
            GSConfigEntry entry{
                .version = GSConfigEntry::EXPECTED_VERSION,
                .gs_config = gs_config,
            };
            AppendGSConfig(gs_cache, entry, gs_config_hash);
        }

        return std::make_pair(gs_config_hash, nullptr);
    } else {
        // AstraPro: Bound optional quaternion-fixup variants independently of VS.
        if (ready_only && !fixed_geometry_shaders.contains(gs_config_hash) &&
            fixed_geometry_shaders.size() >= ReadyVertexPolicy::MaxPrograms) {
            ++ready_gs_capped;
            return {};
        }
        auto [it, new_shader] = fixed_geometry_shaders.try_emplace(gs_config_hash, parent.instance);
        auto& shader = it->second;

        if (new_shader) {
            LOG_NEW_OBJECT(Render_Vulkan, "New GS config {:016X}", gs_config_hash);

            // CodexAstraUlt: Replace mutable worker reads with the same frozen optional
            // policy as VS. Mandatory/Custom geometry jobs retain the requested option.
            // CodexAstraLocal: Freeze clip-distance generation with the compiler
            // option from the same producer-side profile; neither setting is read
            // from the mutable parent when this delayed geometry job executes.
            const bool disable_optimizer =
                DisableShaderOptimizer(ready_only, parent.profile.vk_disable_spirv_optimizer != 0);
            const bool use_clip_planes = parent.profile.has_clip_planes;
            try { parent.shader_workers.QueueWork([gs_config, this, &shader, gs_config_hash, ready_only,
                                             disable_optimizer, use_clip_planes]() {
                const auto compile = [&] {
                    ExtraFixedGSConfig extra;
                    extra.use_clip_planes = use_clip_planes;
                    extra.separable_shader = true;
                    const auto code = GLSL::GenerateFixedGeometryShader(gs_config, extra);
                    const auto spirv = CompileGLSL(code, vk::ShaderStageFlagBits::eGeometry,
                                                   "", disable_optimizer);
                    if (spirv.empty())
                        throw std::runtime_error("empty geometry module");
                    shader.module = CompileSPV(spirv, parent.instance.GetDevice());
                    if (!shader.module)
                        throw std::runtime_error("null geometry module");
                    AppendGSSPIRV(gs_cache, spirv, gs_config_hash);
                    GSConfigEntry entry{.version = GSConfigEntry::EXPECTED_VERSION,
                                        .gs_config = gs_config};
                    AppendGSConfig(gs_cache, entry, gs_config_hash);
                };
                if (!ready_only) {
                    // CodexAstraUlt-2: Geometry dependencies share mandatory completion rules.
                    VideoCore::CompleteShaderBuild(shader, compile, [this](const char* reason) {
                        if (mandatory_shader_failures.fetch_add(1) < 8) {
                            // CodexAstraUlt-2 Log Line: Eight mandatory shader failures per title.
                            LOG_ERROR(Render_Vulkan, "Uberhar mandatory GS failed: error={} limit=8",
                                      reason);
                        }
                    });
                    return;
                }
                // CodexAstraLocal: Unknown compiler and logger failures obey
                // the same terminal publication rule as mandatory dependencies.
                VideoCore::CompleteShaderBuild(shader, compile, [this](const char* reason) {
                    if (ready_shader_failures.fetch_add(1) < 8)
                        LOG_ERROR(Render_Vulkan, "Uberhar optional GPU GS failed: {}", reason);
                });
            }); } catch (...) {
                // CodexAstraLocal: Enqueue failure cannot leave dependent PSOs waiting forever.
                shader.MarkFailed();
                ready_shader_failures.fetch_add(1);
            }
        }

        return std::make_pair(gs_config_hash, &shader);
    }
}

GraphicsPipeline* ShaderDiskCache::GetPipeline(const PipelineInfo& info) {

    u64 hash = info.Hash();
    // AstraEH: Deduplicate equivalent host pipelines without rewriting the guest
    // configuration IDs used by transferable PL/VS/FS/GS records.
    auto shaders = parent.current_shaders;
    if (!parent.instance.UseGeometryShaders() ||
        parent.instance.IsFragmentShaderBarycentricSupported()) {
        shaders[ProgramType::GS] = nullptr;
    }
    const u64 optimized_hash = info.state.ExecutionHash(
        parent.instance.IsExtendedDynamicStateSupported(), HostShaderIds(shaders));

    auto [it, new_pipeline] = graphics_pipelines.try_emplace(optimized_hash);
    if (new_pipeline) {
        // AstraEH: A known record with a new host object warrants identity investigation.
        ++live_host_pipelines;
        live_host_known_record += known_graphic_pipelines.contains(hash);
        it.value() = std::make_unique<GraphicsPipeline>(
            parent.instance, parent.renderpass_cache, info, *parent.driver_pipeline_cache,
            *parent.pipeline_layout, shaders, &parent.pipeline_workers,
            // AstraEH: Connect specialization completion to first-ready waits and diagnostics.
            parent.SpecializedBuildOptions());
    }

    if (known_graphic_pipelines.emplace(hash).second) {
        LOG_NEW_OBJECT(Render_Vulkan, "New Pipeline {:016X}", hash);

        PLConfigEntry entry{
            .version = PLConfigEntry::EXPECTED_VERSION,
            .pl_info = info.state,
        };

        AppendPLConfig(pl_cache, entry, hash);
    }

    return it.value().get();
}

// AstraEH: No new per-draw strings or sets: existing maps provide the census.
void ShaderDiskCache::ReportUberharStats(const char* kind) const {
    // CodexAstraUlt: Only repeated progress is optional; every final report stays reliable.
    const auto delivery = std::strcmp(kind, "progress") == 0
                              ? Common::Log::Delivery::Diagnostic
                              : Common::Log::Delivery::Reliable;
    // CodexAstraLocal TEST-ONLY LOG: Remove after cold/scene-revisit/warm reuse
    // qualification. Counters distinguish useful selections from mere disk files.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
        "Uberhar optional cache {}: schema=1 full_evictions={} static_evictions={} "
        "disk_hits={} disk_misses={} disk_rejected={} disk_writes={} write_failures={} "
        "quota_refusals={} full_credit={} static_credit={} full_refilled={} static_refilled={} "
        "replay=demand_confirmed_positive bounds=128full_32static_512MiB_global",
        kind, optional_full_evictions, optional_static_evictions, optional_disk_hits.load(),
        optional_disk_misses.load(), optional_disk_rejected.load(), optional_disk_writes.load(),
        optional_disk_write_failures.load(), optional_disk_quota_refusals.load(),
        full_module_credit.Credits(), static_module_credit.Credits(),
        full_module_credit.Replenished(), static_module_credit.Replenished());
    // CodexAstraLocal TEST-ONLY LOG: Retain failure classes/retries/circuit until
    // recovery is qualified; remove this summary with the cache experiment logs.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
        "Uberhar optional recovery {}: retries={} resource_failures={} source_failures={} "
        "unknown_failures={} device_failures={} circuit_until_ms={} max_key_retries=2",
        kind, optional_retry_jobs.load(), optional_resource_failures.load(), optional_source_failures.load(),
        optional_unknown_failures.load(), optional_device_failures.load(), optional_circuit_until_ms.load());
    // AstraPro: Existing five-second/final cadence, not per draw.
    // CodexAstraLocal TEST-ONLY LOG: Effective generator attempts distinguish
    // direct SPIR-V from G1-pruned GLSL in .53 owner comparisons. Remove the
    // added policy/count fields after generator selection is qualified; keep
    // production failure handling and its bounded error diagnostics intact.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar ready GPU fragments {}: schema=1 scope=title requests={} ready_hits={} "
             "cold_demand={} busy={} capped={} mismatches={} failed_hits={} unsupported={} lookup_only_misses={} "
             "modules={} builds={} failures={} compile_ms={:.3f} max_compile_ms={:.3f} "
             "demand_replacements={} max_modules={} warmup_draws={} storage=optional_disk_v1 "
             "generator=setting_compatible_v1 requested_spirv={} direct_spirv_build_attempts={} "
             "glsl_build_attempts={} glsl_pruning=full_v1 weighting=lookups",
             kind, ready_fragment_requests, ready_fragment_hits, ready_fragment_cold,
             ready_fragment_busy, ready_fragment_capped, ready_fragment_mismatches,
             ready_fragment_failed_hits, ready_fragment_unsupported, ready_fragment_lookup_only_misses,
             ready_fragments.size(),
             ready_fragment_builds.load(), ready_fragment_failures.load(),
             ready_fragment_compile_ns.load() / 1e6, ready_fragment_max_compile_ns.load() / 1e6,
             ready_fragment_demand.Replacements(), ReadyFragmentPolicy::MaxModules,
             ReadyFragmentPolicy::WarmupDraws, parent.profile.vk_use_spirv_generator != 0,
             ready_fragment_spirv_builds.load(), ready_fragment_glsl_builds.load());
    // CodexAstraLocal Log Line: Bounded title counts and generated byte/time
    // totals identify real partial compilation and failure. Generated bytes are
    // neither retained source storage nor opaque driver/GPU memory consumption.
    if (parent.allow_static_cpu_tev) {
        u64 done{}, failed{}, source_bytes{}, spirv_bytes{}, generation_ns{}, frontend_ns{}, module_ns{};
        for (const auto& [hash, entry] : static_tev_fragments) {
            if (!entry->shader.IsDone()) continue;
            ++done; failed += entry->shader.HasFailed();
            source_bytes += entry->source_bytes; spirv_bytes += entry->spirv_bytes;
            generation_ns += entry->generation_ns; frontend_ns += entry->frontend_ns;
            module_ns += entry->module_ns;
        }
        // CodexAstraLocal Log Line: Consume the partial tier's existing demand
        // replacement count at this bounded cadence. It explains probation
        // turnover, not module eviction, driver memory or GPU execution cost.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
            "Uberhar static CPU TEV modules {}: schema=1 scope=current_title requests={} ready_hits={} "
            "cold={} busy={} capped={} mismatches={} failed_hits={} unsupported={} lookup_misses={} "
            "modules={} attempts={} allocation_failures={} done={} failed={} "
            "generated_glsl_bytes={} generated_spirv_bytes={} generation_ms={:.3f} "
            "frontend_ms={:.3f} module_ms={:.3f} max_modules=8 initial_attempt_credit=16 renewable=true "
            "storage=optional_disk_v1 generator=static_tev_runtime_lighting optimizer=enabled "
            "cpu_vertex_abi=88 push_bytes=128 weighting=lookups demand_replacements={}",
            kind, static_tev_requests, static_tev_hits, static_tev_cold, static_tev_busy,
            static_tev_capped, static_tev_mismatches, static_tev_failed_hits,
            static_tev_unsupported, static_tev_lookup_misses, static_tev_fragments.size(),
            static_tev_attempts, static_tev_allocation_failures, done, failed, source_bytes,
            spirv_bytes, generation_ns / 1e6, frontend_ns / 1e6, module_ns / 1e6,
            static_tev_demand.Replacements());
    }
    // AstraPro: Existing bounded cadence; counts don't imply GPU timings.
    // CodexAstraUlt Log Line: Retain bounded reporting and identify the effective optional
    // compiler policy separately from the generic/Custom requested optimizer setting.
    // CodexAstraLocal Log Line: This fixed policy label describes optional jobs,
    // not successful compilation, GPU memory savings or the global optimizer toggle.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar GPU shader admission {}: schema=1 vs_deferred={} vs_capped={} gs_capped={} "
             "failures={} max_vs_configs=128 max_gs_configs=128 "
             "ready_compiler_policy=optimized_glsl_or_direct_spirv_v2 "
             "ready_optimizer_disabled=false ready_optimizer_scope=glsl_only",
             kind, ready_vs_deferred, ready_vs_capped, ready_gs_capped,
             ready_shader_failures.load());
    // AstraEH Log Line: Same bounded report cadence; counts objects, not completed compiles.
    LOG_INFO(Render_Vulkan,
             "Uberhar execution origins {}: startup_host={} startup_guest={} live_host={} "
             "live_host_known_record={} fragment_modules={}",
             kind, startup_host_pipelines, startup_guest_records, live_host_pipelines,
             live_host_known_record, fragment_shaders.size());
    // AstraEH Log Line: bounded aggregate identifying module/pipeline reuse and foreground work.
    LOG_INFO(Render_Vulkan,
             "Uberhar execution cache {}: guest_pipeline_records={} host_pipelines={} "
             "vertex_configs={} vertex_modules={} live_vs_codegen={} vs_codegen_ms={:.3f} "
             "vs_codegen_max_ms={:.3f}",
             kind, known_graphic_pipelines.size(), graphics_pipelines.size(),
             programmable_vertex_map.size(), programmable_vertex_cache.size(),
             live_vs_codegen_count, live_vs_codegen_ns / 1000000.0,
             live_vs_codegen_max_ns / 1000000.0);
}

ShaderDiskCache::SourceFileCacheVersionHash ShaderDiskCache::GetSourceFileCacheVersionHash() {
    SourceFileCacheVersionHash hash{};
    const std::size_t length = std::min(std::strlen(Common::g_shader_cache_version), hash.size());
    std::memcpy(hash.data(), Common::g_shader_cache_version, length);
    return hash;
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadFirst() {
    return ReadAt(0);
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadNext(const CacheEntry& previous) {
    if (!previous.valid)
        return CacheEntry();

    return ReadAt(previous.position + previous.header.entry_size);
}

std::pair<size_t, ShaderDiskCache::CacheEntry::CacheEntryHeader>
ShaderDiskCache::CacheFile::ReadNextHeader(
    const ShaderDiskCache::CacheEntry::CacheEntryHeader& previous, size_t previous_position) {

    size_t new_pos = previous_position + previous.entry_size;

    return {new_pos, ReadAtHeader(new_pos)};
}

ShaderDiskCache::CacheEntry::CacheEntryHeader ShaderDiskCache::CacheFile::ReadAtHeader(
    size_t position) {

    CacheEntry::CacheEntryHeader header;

    if (!ReadFromFileCached(&header, position, sizeof(header))) {
        return CacheEntry::CacheEntryHeader();
    }

    return header;
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadAt(size_t position) {
    CacheEntry res{};
    res.position = position;

    constexpr u32 headers_size =
        sizeof(CacheEntry::CacheEntryHeader) + sizeof(CacheEntry::CacheEntryFooter);

    res.header = ReadAtHeader(position);

    if (res.header.Valid()) {

        // We have everything validated, read the data.
        u32 payload_size = res.header.entry_size - headers_size;
        std::vector<u8> payload(payload_size);

        if (ReadFromFileCached(payload.data(), (position + sizeof(CacheEntry::CacheEntryHeader)),
                               payload_size)) {
            // Decompress data if needed
            if (res.header.zstd_compressed) {
                if (Common::Compression::GetDecompressedSize(payload) <
                    CacheEntry::MAX_ENTRY_SIZE) {
                    res.data = Common::Compression::DecompressDataZSTD(payload);
                    res.valid = true;
                }
            } else {
                res.data = std::move(payload);
                res.valid = true;
            }
        }
    }
    return res;
}

size_t ShaderDiskCache::CacheFile::GetTotalEntries() {
    if (!file.IsGood()) {
        next_entry_id = SIZE_MAX;
        file_size = 0;
        return next_entry_id;
    }

    if (next_entry_id != SIZE_MAX) {
        return next_entry_id;
    }

    file_size = file.GetSize();
    if (file_size == 0) {
        next_entry_id = 0;
        return next_entry_id;
    }

    CacheEntry::CacheEntryFooter footer{};

    if (file.ReadAtArray(&footer, 1, file_size - sizeof(footer)) == sizeof(footer) &&
        footer.version == CacheEntry::CacheEntryFooter::ENTRY_VERSION) {
        next_entry_id = footer.entry_id + 1;
    } else {
        file_size = 0;
        return SIZE_MAX;
    }

    return next_entry_id;
}

void ShaderDiskCache::CacheFile::Append(CacheEntryType type, u64 id, std::span<const u8> data,
                                        bool compress) {
    if (curr_mode != CacheOpMode::APPEND) {
        return;
    }

    std::vector<u8> copy_data(data.begin(), data.end());

    append_worker.QueueWork([this, type, id, compress, data = std::move(copy_data)]() {
        if (next_entry_id == SIZE_MAX || !file.IsGood()) {
            return;
        }

        std::span<const u8> data_final;
        std::vector<u8> data_compress;

        CacheEntry::CacheEntryHeader header{};
        CacheEntry::CacheEntryFooter footer{};

        constexpr u32 headers_size =
            sizeof(CacheEntry::CacheEntryHeader) + sizeof(CacheEntry::CacheEntryFooter);

        if (compress) {
            data_compress = Common::Compression::CompressDataZSTDDefault(data);
            data_final = data_compress;
            header.zstd_compressed.Assign(true);
        } else {
            data_final = data;
        }
        header.entry_version = CacheEntry::CacheEntryHeader::ENTRY_VERSION;
        footer.version.Assign(CacheEntry::CacheEntryFooter::ENTRY_VERSION);
        header.entry_size = footer.entry_size = data_final.size() + headers_size;
        footer.entry_id.Assign(next_entry_id++);

        header.type = type;
        header.id = id;

        std::vector<u8> out_data(data_final.size() + headers_size);
        memcpy(out_data.data(), &header, sizeof(header));
        memcpy(out_data.data() + sizeof(header), data_final.data(), data_final.size());
        memcpy(out_data.data() + sizeof(header) + data_final.size(), &footer, sizeof(footer));

        file.WriteBytes(out_data.data(), out_data.size());
        if (file.IsGood()) {
            file.Flush();
        }
    });
}

bool ShaderDiskCache::CacheFile::SwitchMode(CacheOpMode mode) {
    if (curr_mode == mode) {
        return true;
    }
    if (curr_mode == CacheOpMode::APPEND) {
        append_worker.WaitForRequests();
    }

    switch (mode) {
    case CacheOpMode::READ: {
        next_entry_id = SIZE_MAX; // Force reading entries again
        cached_file_data_start = 0;
        cached_file_data.clear();
        file = FileUtil::IOFile(filepath, "rb");
        bool is_open = file.IsGood();
        if (is_open) {
            GetTotalEntries();
        }
        curr_mode = mode;
        return is_open;
    }
    case CacheOpMode::APPEND: {
        if (!SwitchMode(CacheOpMode::READ)) {
            curr_mode = mode;
            return false;
        }
        file.Close();
        cached_file_data_start = 0;
        cached_file_data.clear();
        curr_mode = mode;
        if (next_entry_id == SIZE_MAX) {
            // Cannot append if getting total items fails
            return false;
        }

        file = FileUtil::IOFile(filepath, "ab");
        return file.IsGood();
    }
    case CacheOpMode::DELETE: {
        next_entry_id = SIZE_MAX;
        cached_file_data_start = 0;
        cached_file_data.clear();
        file.Close();
        curr_mode = mode;
        return FileUtil::Delete(filepath);
    }
    case CacheOpMode::RECREATE: {
        if (!SwitchMode(CacheOpMode::DELETE)) {
            return false;
        }
        if (!FileUtil::CreateEmptyFile(filepath)) {
            return false;
        }
        return SwitchMode(CacheOpMode::APPEND);
    }
    default:
        UNREACHABLE();
    }
    return false;
}

bool ShaderDiskCache::CacheFile::ReadFromFileCached(void* dst, size_t position, size_t size) {
    if (!dst || position + size > file_size) {
        return false;
    }

    size_t offset = position - cached_file_data_start;
    if (position < cached_file_data_start || offset > cached_file_data.size() ||
        size > cached_file_data.size() - offset) {
        if (size > CacheEntry::MAX_ENTRY_SIZE) {
            return false;
        }

        size_t to_read = std::min<size_t>(CacheEntry::MAX_ENTRY_SIZE, file_size - position);

        cached_file_data_start = position;
        cached_file_data.resize(to_read);

        if (file.ReadAtBytes(cached_file_data.data(), to_read, position) != to_read) {
            return false;
        }

        offset = 0;
    }

    std::memcpy(dst, cached_file_data.data() + offset, size);
    return true;
}

std::string ShaderDiskCache::GetVSFile(u64 title_id, bool is_temp) const {
    return parent.GetTransferableDir() + DIR_SEP + fmt::format("{:016X}_vs", title_id) +
           (is_temp ? "_temp" : "") + ".vkch";
}

std::string ShaderDiskCache::GetFSFile(u64 title_id, bool is_temp) const {
    return parent.GetTransferableDir() + DIR_SEP + fmt::format("{:016X}_fs", title_id) +
           (is_temp ? "_temp" : "") + ".vkch";
}

std::string ShaderDiskCache::GetGSFile(u64 title_id, bool is_temp) const {
    return parent.GetTransferableDir() + DIR_SEP + fmt::format("{:016X}_gs", title_id) +
           (is_temp ? "_temp" : "") + ".vkch";
}

std::string ShaderDiskCache::GetPLFile(u64 title_id, bool is_temp) const {
    return parent.GetTransferableDir() + DIR_SEP + fmt::format("{:016X}_pl", title_id) +
           (is_temp ? "_temp" : "") + ".vkch";
}

bool ShaderDiskCache::RecreateCache(CacheFile& file, CacheFileType type) {
    file.SwitchMode(CacheFile::CacheOpMode::RECREATE);

    std::array<char, 0x20> build_name{};
    size_t name_len = std::strlen(Common::g_build_fullname);
    memcpy(build_name.data(), Common::g_build_fullname, std::min(name_len, build_name.size()));

    auto get_hash = [](CacheFileType type) {
        switch (type) {
        case CacheFileType::VS_CACHE:
            return PicaVSConfigState::StructHash();
        case CacheFileType::FS_CACHE:
            return FSConfig::StructHash();
        case CacheFileType::GS_CACHE:
            return PicaGSConfigState::StructHash();
        case CacheFileType::PL_CACHE:
            return StaticPipelineInfo::StructHash();
        default:
            UNREACHABLE();
            return u64{};
        };
    };

    FileInfoEntry entry{
        .cache_magic = FileInfoEntry::CACHE_FILE_MAGIC,
        .file_version = FileInfoEntry::CACHE_FILE_VERSION,
        .config_struct_hash = get_hash(type),
        .file_type = type,
        .source_hash = GetSourceFileCacheVersionHash(),
        .build_name = build_name,
        .profile = parent.profile,
    };

    file.Append(CacheEntryType::FILE_INFO, 0, entry, false);
    return true;
}

bool ShaderDiskCache::InitVSCache(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {
    std::vector<size_t> pending_configs;
    std::unordered_map<u64, size_t> pending_programs;
    std::unique_ptr<Pica::ShaderSetup> shader_setup;
    std::unique_ptr<CacheFile> regenerate_file;

    auto cleanup_on_error = [&]() {
        programmable_vertex_cache.clear();
        programmable_vertex_map.clear();
        known_vertex_programs.clear();
        if (regenerate_file) {
            regenerate_file->SwitchMode(CacheFile::CacheOpMode::DELETE);
        }
    };

    LOG_INFO(Render_Vulkan, "Loading VS disk shader cache for title {:016X}", title_id);

    vs_cache.SetFilePath(GetVSFile(title_id, false));

    if (!vs_cache.SwitchMode(CacheFile::CacheOpMode::READ)) {
        LOG_INFO(Render_Vulkan, "Missing VS disk shader cache for title {:016X}", title_id);
        cleanup_on_error();
        return false;
    }

    u32 tot_entries = vs_cache.GetTotalEntries();
    auto curr = vs_cache.ReadFirst();
    if (!curr.Valid() || curr.Type() != CacheEntryType::FILE_INFO) {
        MALFORMED_DISK_CACHE;
    }

    const FileInfoEntry* file_info = curr.Payload<FileInfoEntry>();
    if (!file_info || file_info->cache_magic != FileInfoEntry::CACHE_FILE_MAGIC ||
        file_info->file_version != FileInfoEntry::CACHE_FILE_VERSION ||
        file_info->file_type != CacheFileType::VS_CACHE) {
        MALFORMED_DISK_CACHE;
    }

    if (file_info->config_struct_hash != PicaVSConfigState::StructHash()) {
        LOG_ERROR(Render_Vulkan,
                  "Cache was created for a different PicaVSConfigState, resetting...");
        cleanup_on_error();
        return false;
    }

    if (file_info->source_hash != GetSourceFileCacheVersionHash()) {
        LOG_INFO(Render_Vulkan, "Cache contains old vertex program, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetVSFile(title_id, true));
    }

    if (file_info->profile != parent.profile && !regenerate_file) {
        LOG_INFO(Render_Vulkan,
                 "Cache has driver and user settings mismatch, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetVSFile(title_id, true));
    }

    if (regenerate_file) {
        RecreateCache(*regenerate_file, CacheFileType::VS_CACHE);
    }

    CacheEntry::CacheEntryHeader curr_header = curr.Header();
    size_t curr_offset = curr.Position();

    size_t current_callback_index = 0;
    size_t tot_callback_index = tot_entries - 1;

    // Scan the entire file first, while keeping track of configs and programs.
    // SPIRV can be compiled directly and will be linked to the proper config entries
    // later.
    for (int i = 1; i < tot_entries; i++) {

        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        std::tie(curr_offset, curr_header) = vs_cache.ReadNextHeader(curr_header, curr_offset);

        if (!curr_header.Valid()) {
            MALFORMED_DISK_CACHE;
        }

        LOG_DEBUG(Render_Vulkan, "Processing ID: {:016X} (type {})", curr_header.Id(),
                  curr_header.Type());

        if (curr_header.Type() == CacheEntryType::VS_CONFIG) {
            pending_configs.push_back(curr_offset);
        } else if (curr_header.Type() == CacheEntryType::VS_PROGRAM) {
            pending_programs.try_emplace(curr_header.Id(), curr_offset);

            // We won't use this entry sequentially again, so report progress.
            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Vertex Shader");
            }
        } else if (curr_header.Type() == CacheEntryType::VS_SPIRV) {

            // Only use SPIRV entries if we are not regenerating the cache, as the driver or
            // user settings do not match, which could lead to different SPIRV.
            // These will be re-created from the cached config and programs later.
            if (!regenerate_file) {
                LOG_DEBUG(Render_Vulkan, "    processing SPIRV.");

                curr = vs_cache.ReadAt(curr_offset);
                if (!curr.Valid() || curr.Type() != CacheEntryType::VS_SPIRV) {
                    MALFORMED_DISK_CACHE;
                }

                const u8* spirv_data = curr.Data().data();
                const size_t spirv_size = curr.Data().size();

                auto [iter_prog, new_program] =
                    programmable_vertex_cache.try_emplace(curr.Id(), parent.instance);
                if (new_program) {
                    LOG_DEBUG(Render_Vulkan, "    compiling SPIRV.");

                    const auto spirv = std::span<const u32>(
                        reinterpret_cast<const u32*>(spirv_data), spirv_size / sizeof(u32));

                    iter_prog->second.module = CompileSPV(spirv, parent.instance.GetDevice());
                    // CodexAstraUlt-2: A failed cached module is complete but never usable.
                    iter_prog->second.PublishModule();

                    if (!iter_prog->second.module) {
                        // Compilation failed for some reason, remove from cache to let it
                        // be re-generated at runtime or during config and program processing.
                        LOG_ERROR(Render_Vulkan, "Unexpected program compilation failure");
                        programmable_vertex_cache.erase(iter_prog);
                    }
                }
            }

            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Vertex Shader");
            }
        } else {
            MALFORMED_DISK_CACHE;
        }
    }

    // Once we have all the shader instances created from SPIRV, we can link them to the VS configs.
    LOG_DEBUG(Render_Vulkan, "Linking with config entries.");

    // Mmultiple config entries may point to the same program entry. We could load all program
    // entries to memory to prevent having to read them from disk on every config entry, but program
    // entries are pretty big (around 50KB each). A LRU cache is a middle point between disk access
    // and memory usage.
    std::unique_ptr<Common::StaticLRUCache<u64, VSProgramEntry, 10>> program_lru =
        std::make_unique<Common::StaticLRUCache<u64, VSProgramEntry, 10>>();

    for (auto& offset : pending_configs) {
        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        if (callback) {
            callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                     tot_callback_index, "Vertex Shader");
        }

        curr = vs_cache.ReadAt(offset);
        const VSConfigEntry* entry;

        if (!curr.Valid() || curr.Type() != CacheEntryType::VS_CONFIG ||
            !(entry = curr.Payload<VSConfigEntry>()) ||
            entry->version != VSConfigEntry::EXPECTED_VERSION) {
            MALFORMED_DISK_CACHE;
        }

        if (curr.Id() != entry->vs_config.Hash()) {
            LOG_ERROR(Render_Vulkan, "Unexpected PicaVSConfig hash mismatch");
            continue;
        }

        LOG_DEBUG(Render_Vulkan, "Linking {:016X}.", curr.Id());

        auto [iter_config, new_config] = programmable_vertex_map.try_emplace(curr.Id());
        if (new_config) {
            // New config entry, usually always taken unless there is duplicate entries on the cache
            // for some reason.

            // We cannot trust the SPIRV entry ID anymore if we are regenerating.
            auto shader_it = regenerate_file
                                 ? programmable_vertex_cache.end()
                                 : programmable_vertex_cache.find(entry->spirv_entry_id);
            if (shader_it != programmable_vertex_cache.end()) {
                // The config entry uses a SPIRV entry that was already compiled (this is the usual
                // path when the cache doesn't need to be re-generated).

                LOG_DEBUG(Render_Vulkan, "    linked with existing SPIRV {:016X}.",
                          entry->spirv_entry_id);

                iter_config->second = &shader_it->second;
                known_vertex_programs.emplace(entry->program_entry_id).second;
            } else {
                // Cached SPIRV not found, need to recompile.

                // Search program entry in a LRU first, to prevent having to read from the cache
                // file on each separate config entry.
                auto [found, program_lru_entry] = program_lru->request(entry->program_entry_id);
                if (!found) {
                    LOG_DEBUG(Render_Vulkan, "    reading program {:016X}.",
                              entry->program_entry_id);

                    // Program not on the LRU, need to read it from cache file
                    auto program_it = pending_programs.find(entry->program_entry_id);
                    if (program_it == pending_programs.end()) {
                        // Program code not in disk cache, should never happen.
                        LOG_ERROR(Render_Vulkan, "Missing program code for config entry");
                        programmable_vertex_map.erase(iter_config);
                        continue;
                    }

                    auto program_cache_entry = vs_cache.ReadAt(program_it->second);
                    const VSProgramEntry* program_entry;

                    if (!program_cache_entry.Valid() ||
                        program_cache_entry.Type() != CacheEntryType::VS_PROGRAM ||
                        !(program_entry = program_cache_entry.Payload<VSProgramEntry>()) ||
                        program_entry->version != VSProgramEntry::EXPECTED_VERSION) {
                        MALFORMED_DISK_CACHE;
                    }

                    program_lru_entry = *program_entry;

                    bool new_program =
                        known_vertex_programs.emplace(entry->program_entry_id).second;

                    if (new_program && regenerate_file) {
                        // When regenerating, only append if it's a new program entry not seen
                        // before.
                        AppendVSProgram(*regenerate_file, program_lru_entry,
                                        entry->program_entry_id);
                    }
                }

                // Recompile SPIRV from config and program now.
                LOG_DEBUG(Render_Vulkan, "    using program {:016X}.", entry->program_entry_id);

                shader_setup = std::make_unique<Pica::ShaderSetup>();
                shader_setup->UpdateProgramCode(program_lru_entry.program_code,
                                                program_lru_entry.program_len);
                shader_setup->UpdateSwizzleData(program_lru_entry.swizzle_code,
                                                program_lru_entry.swizzle_len);
                shader_setup->DoProgramCodeFixup();

                if (entry->vs_config.state.program_hash != shader_setup->GetProgramCodeHash() ||
                    entry->vs_config.state.swizzle_hash != shader_setup->GetSwizzleDataHash()) {
                    LOG_ERROR(Render_Vulkan, "Unexpected ShaderSetup hash mismatch");
                    programmable_vertex_map.erase(iter_config);
                    continue;
                }

                ExtraVSConfig extra_config = parent.CalcExtraConfig(entry->vs_config);

                auto program_glsl = Common::HashableString(
                    GLSL::GenerateVertexShader(*shader_setup, entry->vs_config, extra_config));
                if (program_glsl.empty()) {
                    LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
                    programmable_vertex_map.erase(iter_config);
                    continue;
                }

                const u64 spirv_id = program_glsl.Hash();

                auto [iter_prog, new_spirv] =
                    programmable_vertex_cache.try_emplace(spirv_id, parent.instance);

                LOG_DEBUG(Render_Vulkan, "    processing SPIRV.");

                if (new_spirv) {
                    LOG_DEBUG(Render_Vulkan, "    compiling SPIRV.");

                    auto spirv = CompileGLSL(program_glsl, vk::ShaderStageFlagBits::eVertex);

                    iter_prog->second.module = CompileSPV(spirv, parent.instance.GetDevice());
                    // CodexAstraUlt-2: Regenerated dependencies also publish failed completion.
                    iter_prog->second.PublishModule();

                    if (regenerate_file) {
                        // If we are regenerating, save the new spirv to disk.
                        AppendVSSPIRV(*regenerate_file, spirv, spirv_id);
                    }
                }

                if (regenerate_file) {
                    // If we are regenerating, save the config entry to the cache. We need to make a
                    // copy first because it's possible the SPIRV id has changed and we need to
                    // adjust it.
                    std::unique_ptr<VSConfigEntry> entry_copy =
                        std::make_unique<VSConfigEntry>(*entry);
                    entry_copy->spirv_entry_id = spirv_id;
                    AppendVSConfig(*regenerate_file, *entry_copy, curr.Id());
                }

                // Asign the SPIRV shader to the config
                iter_config->second = &iter_prog->second;

                LOG_DEBUG(Render_Vulkan, "    linked with SPIRV {:016X}.", entry->spirv_entry_id);
            }
        }
    }

    if (regenerate_file) {
        // If we are regenerating, replace the old file with the new one.
        vs_cache.SwitchMode(CacheFile::CacheOpMode::DELETE);
        regenerate_file.reset();
        FileUtil::Rename(GetVSFile(title_id, true), GetVSFile(title_id, false));
    }

    // Switch to append mode to receive new entries.
    return vs_cache.SwitchMode(CacheFile::CacheOpMode::APPEND);
}

bool ShaderDiskCache::InitFSCache(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {
    std::vector<std::pair<u64, size_t>> pending_configs;
    std::unique_ptr<CacheFile> regenerate_file;

    auto cleanup_on_error = [&]() {
        fragment_shaders.clear();
        if (regenerate_file) {
            regenerate_file->SwitchMode(CacheFile::CacheOpMode::DELETE);
        }
    };

    LOG_INFO(Render_Vulkan, "Loading FS disk shader cache for title {:016X}", title_id);

    fs_cache.SetFilePath(GetFSFile(title_id, false));

    if (!fs_cache.SwitchMode(CacheFile::CacheOpMode::READ)) {
        LOG_INFO(Render_Vulkan, "Missing FS disk shader cache for title {:016X}", title_id);
        cleanup_on_error();
        return false;
    }

    u32 tot_entries = fs_cache.GetTotalEntries();
    auto curr = fs_cache.ReadFirst();
    if (!curr.Valid() || curr.Type() != CacheEntryType::FILE_INFO) {
        MALFORMED_DISK_CACHE;
    }

    const FileInfoEntry* file_info = curr.Payload<FileInfoEntry>();
    if (!file_info || file_info->cache_magic != FileInfoEntry::CACHE_FILE_MAGIC ||
        file_info->file_version != FileInfoEntry::CACHE_FILE_VERSION ||
        file_info->file_type != CacheFileType::FS_CACHE) {
        MALFORMED_DISK_CACHE;
    }

    if (file_info->config_struct_hash != FSConfig::StructHash()) {
        LOG_ERROR(Render_Vulkan, "Cache was created for a different FSConfig, resetting...");
        cleanup_on_error();
        return false;
    }

    if (file_info->source_hash != GetSourceFileCacheVersionHash()) {
        LOG_INFO(Render_Vulkan, "Cache contains old fragment program, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetFSFile(title_id, true));
    }

    if (file_info->profile != parent.profile && !regenerate_file) {
        LOG_INFO(Render_Vulkan,
                 "Cache has driver and user settings mismatch, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetFSFile(title_id, true));
    }

    if (regenerate_file) {
        RecreateCache(*regenerate_file, CacheFileType::FS_CACHE);
    }

    CacheEntry::CacheEntryHeader curr_header = curr.Header();
    size_t curr_offset = curr.Position();

    size_t current_callback_index = 0;
    size_t tot_callback_index = tot_entries - 1;

    // Scan the entire file first, while keeping track of configs.
    // SPIRV can be compiled directly, if a config has a missing
    // SPIRV entry it can be regenerated later.
    for (int i = 1; i < tot_entries; i++) {
        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        std::tie(curr_offset, curr_header) = fs_cache.ReadNextHeader(curr_header, curr_offset);

        if (!curr_header.Valid()) {
            MALFORMED_DISK_CACHE;
        }

        LOG_DEBUG(Render_Vulkan, "Processing ID: {:016X} (type {})", curr_header.Id(),
                  curr_header.Type());

        if (curr_header.Type() == CacheEntryType::FS_CONFIG) {
            pending_configs.push_back({curr_header.Id(), curr_offset});
        } else if (curr_header.Type() == CacheEntryType::FS_SPIRV) {

            // Only use SPIRV entries if we are not regenerating the cache, as the driver or
            // user settings do not match, which could lead to different SPIRV.
            // These will be regenerated from the cached config later.
            if (!regenerate_file) {
                LOG_DEBUG(Render_Vulkan, "    processing SPIRV.");

                curr = fs_cache.ReadAt(curr_offset);
                if (!curr.Valid() || curr.Type() != CacheEntryType::FS_SPIRV) {
                    MALFORMED_DISK_CACHE;
                }

                const u8* spirv_data = curr.Data().data();
                const size_t spirv_size = curr.Data().size();

                auto [iter_spirv, new_program] =
                    fragment_shaders.try_emplace(curr.Id(), parent.instance);
                if (new_program) {
                    LOG_DEBUG(Render_Vulkan, "    compiling SPIRV.");

                    const auto spirv = std::span<const u32>(
                        reinterpret_cast<const u32*>(spirv_data), spirv_size / sizeof(u32));

                    iter_spirv->second.module = CompileSPV(spirv, parent.instance.GetDevice());
                    // CodexAstraUlt-2: A failed cached fragment must not enter pipeline creation.
                    iter_spirv->second.PublishModule();

                    if (!iter_spirv->second.module) {
                        // Compilation failed for some reason, remove from cache to let it
                        // be regenerated at runtime or during config processing.
                        LOG_ERROR(Render_Vulkan, "Unexpected program compilation failure");
                        fragment_shaders.erase(iter_spirv);
                    }
                }
            }

            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Fragment Shader");
            }
        } else {
            MALFORMED_DISK_CACHE;
        }
    }

    // Once we have all the shader instances created from SPIRV, we can link them to the FS configs.
    LOG_DEBUG(Render_Vulkan, "Linking with config entries.");

    for (auto& offset : pending_configs) {
        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        if (callback) {
            callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                     tot_callback_index, "Fragment Shader");
        }

        LOG_DEBUG(Render_Vulkan, "Linking {:016X}.", offset.first);

        if (fragment_shaders.find(offset.first) != fragment_shaders.end()) {
            // SPIRV of config was already compiled, no need to regenerate
            // it from the cache. This can only happen if we are not regenerating
            // the cache.
            LOG_DEBUG(Render_Vulkan, "    linked with existing SPIRV.");
            continue;
        }

        // Cached SPIRV not found, need to recompile. Should only happen if
        // we are regenerating the cache.

        curr = fs_cache.ReadAt(offset.second);
        const FSConfigEntry* entry;

        if (!curr.Valid() || curr.Type() != CacheEntryType::FS_CONFIG ||
            !(entry = curr.Payload<FSConfigEntry>()) ||
            entry->version != FSConfigEntry::EXPECTED_VERSION) {
            MALFORMED_DISK_CACHE;
        }

        const auto fs_config_hash = entry->fs_config.Hash();
        if (curr.Id() != fs_config_hash) {
            LOG_ERROR(Render_Vulkan, "Unexpected FSConfig hash mismatch");
            continue;
        }

        const auto [it, new_shader] = fragment_shaders.try_emplace(fs_config_hash, parent.instance);
        auto& shader = it->second;

        std::vector<u32> spirv;
        if (parent.profile.vk_use_spirv_generator &&
            !entry->fs_config.UsesSpirvIncompatibleConfig()) {
            // Use SPIRV generator directly

            spirv = SPIRV::GenerateFragmentShader(entry->fs_config, parent.profile);
            shader.module = CompileSPV(spirv, parent.instance.GetDevice());
        } else {
            // Use GLSL generator then convert to SPIRV

            UserConfig user{};
            const std::string code_glsl =
                GLSL::GenerateFragmentShader(entry->fs_config, user, parent.profile);

            if (code_glsl.empty()) {
                LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
                fragment_shaders.erase(it);
                continue;
            }

            spirv = CompileGLSL(code_glsl, vk::ShaderStageFlagBits::eFragment);
            shader.module = CompileSPV(spirv, parent.instance.GetDevice());
        }
        // CodexAstraUlt-2: Reject unsuccessful regeneration before any dependent draw.
        shader.PublishModule();

        if (regenerate_file) {
            // Append the config and SPIRV to the new file.
            AppendFSSPIRV(*regenerate_file, spirv, fs_config_hash);
            AppendFSConfig(*regenerate_file, *entry, fs_config_hash);
        }

        LOG_DEBUG(Render_Vulkan, "    linked with new SPIRV.");
    }

    if (regenerate_file) {
        // If we are regenerating, replace the old file with the new one.
        fs_cache.SwitchMode(CacheFile::CacheOpMode::DELETE);
        regenerate_file.reset();
        FileUtil::Rename(GetFSFile(title_id, true), GetFSFile(title_id, false));
    }

    // Switch to append mode to receive new entries.
    return fs_cache.SwitchMode(CacheFile::CacheOpMode::APPEND);
}

bool ShaderDiskCache::InitGSCache(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {
    std::vector<std::pair<u64, size_t>> pending_configs;
    std::unique_ptr<CacheFile> regenerate_file;

    auto cleanup_on_error = [&]() {
        fixed_geometry_shaders.clear();
        if (regenerate_file) {
            regenerate_file->SwitchMode(CacheFile::CacheOpMode::DELETE);
        }
    };

    LOG_INFO(Render_Vulkan, "Loading GS disk shader cache for title {:016X}", title_id);

    gs_cache.SetFilePath(GetGSFile(title_id, false));

    if (!gs_cache.SwitchMode(CacheFile::CacheOpMode::READ)) {
        LOG_INFO(Render_Vulkan, "Missing GS disk shader cache for title {:016X}", title_id);
        cleanup_on_error();
        return false;
    }

    u32 tot_entries = gs_cache.GetTotalEntries();
    auto curr = gs_cache.ReadFirst();
    if (!curr.Valid() || curr.Type() != CacheEntryType::FILE_INFO) {
        MALFORMED_DISK_CACHE;
    }

    const FileInfoEntry* file_info = curr.Payload<FileInfoEntry>();
    if (!file_info || file_info->cache_magic != FileInfoEntry::CACHE_FILE_MAGIC ||
        file_info->file_version != FileInfoEntry::CACHE_FILE_VERSION ||
        file_info->file_type != CacheFileType::GS_CACHE) {
        MALFORMED_DISK_CACHE;
    }

    if (file_info->config_struct_hash != PicaGSConfigState::StructHash()) {
        LOG_ERROR(Render_Vulkan,
                  "Cache was created for a different PicaGSConfigState, resetting...");
        cleanup_on_error();
        return false;
    }

    // There is no need to load geometry shaders if we don't support them.
    // We can just load the known IDs and skip SPIRV and cache regeneration.
    const auto geo_shaders_needed = parent.instance.UseGeometryShaders() &&
                                    !parent.instance.IsFragmentShaderBarycentricSupported();

    if (geo_shaders_needed && file_info->source_hash != GetSourceFileCacheVersionHash()) {
        LOG_INFO(Render_Vulkan, "Cache contains old fragment program, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetGSFile(title_id, true));
    }

    if (geo_shaders_needed && file_info->profile != parent.profile && !regenerate_file) {
        LOG_INFO(Render_Vulkan,
                 "Cache has driver and user settings mismatch, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetGSFile(title_id, true));
    }

    if (regenerate_file) {
        RecreateCache(*regenerate_file, CacheFileType::GS_CACHE);
    }

    CacheEntry::CacheEntryHeader curr_header = curr.Header();
    size_t curr_offset = curr.Position();

    size_t current_callback_index = 0;
    size_t tot_callback_index = tot_entries - 1;

    // Scan the entire file first, while keeping track of configs.
    // SPIRV can be compiled directly, if a config has a missing
    // SPIRV entry it can be regenerated later.
    for (int i = 1; i < tot_entries; i++) {
        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        std::tie(curr_offset, curr_header) = gs_cache.ReadNextHeader(curr_header, curr_offset);

        if (!curr_header.Valid()) {
            MALFORMED_DISK_CACHE;
        }

        LOG_DEBUG(Render_Vulkan, "Processing ID: {:016X} (type {})", curr_header.Id(),
                  curr_header.Type());

        if (curr_header.Type() == CacheEntryType::GS_CONFIG) {
            if (geo_shaders_needed) {
                pending_configs.push_back({curr_header.Id(), curr_offset});
            } else {
                known_geometry_shaders.emplace(curr_header.Id());

                if (callback) {
                    callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                             tot_callback_index, "Geometry Shader");
                }
            }
        } else if (curr_header.Type() == CacheEntryType::GS_SPIRV) {

            // Only use SPIRV entries if we are not regenerating the cache, as the driver or
            // user settings do not match, which could lead to different SPIRV.
            // These will be regenerated from the cached config later.
            // Also, only use SPIRV entries if we support geometry shaders on this device.
            if (geo_shaders_needed && !regenerate_file) {
                LOG_DEBUG(Render_Vulkan, "    processing SPIRV.");

                curr = gs_cache.ReadAt(curr_offset);
                if (!curr.Valid() || curr.Type() != CacheEntryType::GS_SPIRV) {
                    MALFORMED_DISK_CACHE;
                }

                const u8* spirv_data = curr.Data().data();
                const size_t spirv_size = curr.Data().size();

                auto [iter_spirv, new_program] =
                    fixed_geometry_shaders.try_emplace(curr.Id(), parent.instance);
                if (new_program) {
                    LOG_DEBUG(Render_Vulkan, "    compiling SPIRV.");

                    const auto spirv = std::span<const u32>(
                        reinterpret_cast<const u32*>(spirv_data), spirv_size / sizeof(u32));

                    iter_spirv->second.module = CompileSPV(spirv, parent.instance.GetDevice());
                    // CodexAstraUlt-2: Cached geometry follows the same dependency contract.
                    iter_spirv->second.PublishModule();

                    if (!iter_spirv->second.module) {
                        // Compilation failed for some reason, remove from cache to let it
                        // be regenerated at runtime or during config processing.
                        LOG_ERROR(Render_Vulkan, "Unexpected program compilation failure");
                        fixed_geometry_shaders.erase(iter_spirv);
                    }
                }
            }

            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Geometry Shader");
            }
        } else {
            MALFORMED_DISK_CACHE;
        }
    }

    // Once we have all the shader instances created from SPIRV, we can link them to the FS configs.
    LOG_DEBUG(Render_Vulkan, "Linking with config entries.");

    for (auto& offset : pending_configs) {
        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        if (callback) {
            callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                     tot_callback_index, "Geometry Shader");
        }

        LOG_DEBUG(Render_Vulkan, "Linking {:016X}.", offset.first);

        if (fixed_geometry_shaders.find(offset.first) != fixed_geometry_shaders.end()) {
            // SPIRV of config was already compiled, no need to regenerate
            // it from the cache. This can only happen if we are not regenerating
            // the cache.
            LOG_DEBUG(Render_Vulkan, "    linked with existing SPIRV.");
            continue;
        }

        // Cached SPIRV not found, need to recompile. Should only happen if
        // we are regenerating the cache.

        curr = gs_cache.ReadAt(offset.second);
        const GSConfigEntry* entry;

        if (!curr.Valid() || curr.Type() != CacheEntryType::GS_CONFIG ||
            !(entry = curr.Payload<GSConfigEntry>()) ||
            entry->version != GSConfigEntry::EXPECTED_VERSION) {
            MALFORMED_DISK_CACHE;
        }

        const auto gs_config_hash = entry->gs_config.Hash();
        if (curr.Id() != gs_config_hash) {
            LOG_ERROR(Render_Vulkan, "Unexpected PicaGSConfigState hash mismatch");
            continue;
        }

        const auto [it, new_shader] =
            fixed_geometry_shaders.try_emplace(gs_config_hash, parent.instance);
        auto& shader = it->second;

        std::vector<u32> spirv;
        ExtraFixedGSConfig extra;
        extra.use_clip_planes = parent.profile.has_clip_planes;
        extra.separable_shader = true;

        const auto code_glsl = GLSL::GenerateFixedGeometryShader(entry->gs_config, extra);

        if (code_glsl.empty()) {
            LOG_ERROR(Render_Vulkan, "Failed to retrieve fixed geometry shader");
            fixed_geometry_shaders.erase(it);
            continue;
        }

        spirv = CompileGLSL(code_glsl, vk::ShaderStageFlagBits::eGeometry);
        shader.module = CompileSPV(spirv, parent.instance.GetDevice());
        // CodexAstraUlt-2: Geometry regeneration publishes a null module as failure.
        shader.PublishModule();

        if (regenerate_file) {
            // Append the config and SPIRV to the new file.
            AppendGSSPIRV(*regenerate_file, spirv, gs_config_hash);
            AppendGSConfig(*regenerate_file, *entry, gs_config_hash);
        }

        LOG_DEBUG(Render_Vulkan, "    linked with new SPIRV.");
    }

    if (regenerate_file) {
        // If we are regenerating, replace the old file with the new one.
        gs_cache.SwitchMode(CacheFile::CacheOpMode::DELETE);
        regenerate_file.reset();
        FileUtil::Rename(GetGSFile(title_id, true), GetGSFile(title_id, false));
    }

    // Switch to append mode to receive new entries.
    return gs_cache.SwitchMode(CacheFile::CacheOpMode::APPEND);
}

bool ShaderDiskCache::InitPLCache(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {

    auto cleanup_on_error = [&]() { graphics_pipelines.clear(); };

    LOG_INFO(Render_Vulkan, "Loading PL disk shader cache for title {:016X}", title_id);

    pl_cache.SetFilePath(GetPLFile(title_id, false));

    if (!pl_cache.SwitchMode(CacheFile::CacheOpMode::READ)) {
        LOG_INFO(Render_Vulkan, "Missing PL disk shader cache for title {:016X}", title_id);
        cleanup_on_error();
        return false;
    }

    u32 tot_entries = pl_cache.GetTotalEntries();
    auto curr = pl_cache.ReadFirst();
    if (!curr.Valid() || curr.Type() != CacheEntryType::FILE_INFO) {
        MALFORMED_DISK_CACHE;
    }

    const FileInfoEntry* file_info = curr.Payload<FileInfoEntry>();
    if (!file_info || file_info->cache_magic != FileInfoEntry::CACHE_FILE_MAGIC ||
        file_info->file_version != FileInfoEntry::CACHE_FILE_VERSION ||
        file_info->file_type != CacheFileType::PL_CACHE) {
        MALFORMED_DISK_CACHE;
    }

    if (file_info->config_struct_hash != StaticPipelineInfo::StructHash()) {
        LOG_ERROR(Render_Vulkan,
                  "Cache was created for a different StaticPipelineInfo, resetting...");
        cleanup_on_error();
        return false;
    }

    size_t current_callback_index = 0;
    size_t tot_callback_index = tot_entries - 1;

    // There is only one entry type in the pipeline info cache,
    // no need to keep track of anything.
    for (int i = 1; i < tot_entries; i++) {
        if (stop_loading) {
            cleanup_on_error();
            return true;
        }

        curr = pl_cache.ReadNext(curr);

        if (!curr.Valid()) {
            MALFORMED_DISK_CACHE;
        }

        LOG_DEBUG(Render_Vulkan, "Processing ID: {:016X} (type {})", curr.Id(), curr.Type());

        if (curr.Type() == CacheEntryType::PL_CONFIG) {

            const PLConfigEntry* entry;

            if (!(entry = curr.Payload<PLConfigEntry>()) ||
                entry->version != PLConfigEntry::EXPECTED_VERSION) {
                MALFORMED_DISK_CACHE;
            }

            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Pipeline");
            }

            known_graphic_pipelines.emplace(curr.Id());

            // Fetch all the shaders used in the pipeline,
            // if any is missing we cannot build it.
            std::array<Shader*, MAX_SHADER_STAGES> shaders;

            LOG_DEBUG(Render_Vulkan, "    uses VS: {:016X}, FS: {:016X}, GS: {:016X}",
                      entry->pl_info.shader_ids[ProgramType::VS],
                      entry->pl_info.shader_ids[ProgramType::FS],
                      entry->pl_info.shader_ids[ProgramType::GS]);

            if (entry->pl_info.shader_ids[ProgramType::VS]) {
                auto it_vs =
                    programmable_vertex_map.find(entry->pl_info.shader_ids[ProgramType::VS]);
                if (it_vs == programmable_vertex_map.end()) {
                    LOG_ERROR(Render_Vulkan, "Missing vertex shader {:016X} for pipeline {:016X}",
                              entry->pl_info.shader_ids[ProgramType::VS], curr.Id());
                    continue;
                }
                shaders[ProgramType::VS] = it_vs->second;
            } else {
                shaders[ProgramType::VS] = &parent.trivial_vertex_shader;
            }

            auto it_fs = fragment_shaders.find(entry->pl_info.shader_ids[ProgramType::FS]);
            if (it_fs == fragment_shaders.end()) {
                LOG_ERROR(Render_Vulkan, "Missing fragment shader {:016X} for pipeline {:016X}",
                          entry->pl_info.shader_ids[ProgramType::FS], curr.Id());
                continue;
            }
            shaders[ProgramType::FS] = &it_fs->second;

            if (parent.instance.UseGeometryShaders() &&
                !parent.instance.IsFragmentShaderBarycentricSupported() &&
                entry->pl_info.shader_ids[ProgramType::GS]) {
                auto it_gs =
                    fixed_geometry_shaders.find(entry->pl_info.shader_ids[ProgramType::GS]);
                if (it_gs == fixed_geometry_shaders.end()) {
                    LOG_ERROR(Render_Vulkan, "Missing geometry shader {:016X} for pipeline {:016X}",
                              entry->pl_info.shader_ids[ProgramType::GS], curr.Id());
                    continue;
                }
                shaders[ProgramType::GS] = &it_gs->second;
            } else {
                shaders[ProgramType::GS] = nullptr;
            }

            // Build the pipeline using the cached pipeline info.
            // AstraEH: Use the same host-module/active-state key on cache reload as
            // on first play. Guest shader IDs remain resolvable in the disk records.
            const u64 pl_hash_opt = entry->pl_info.ExecutionHash(
                parent.instance.IsExtendedDynamicStateSupported(), HostShaderIds(shaders));
            if (graphics_pipelines.find(pl_hash_opt) != graphics_pipelines.end()) {
                continue;
            }
            // The dynamic state can be left default initialized.
            PipelineInfo info{};
            info.state = entry->pl_info;

            auto [it_pl, _] = graphics_pipelines.try_emplace(pl_hash_opt);
            it_pl.value() = std::make_unique<GraphicsPipeline>(
                parent.instance, parent.renderpass_cache, info, *parent.driver_pipeline_cache,
                *parent.pipeline_layout, shaders, &parent.pipeline_workers,
                // AstraEH: Disk-loaded pipelines must publish to the same completion signal.
                parent.SpecializedBuildOptions());

            it_pl.value()->TryBuild(false);

            LOG_DEBUG(Render_Vulkan, "    built.");

        } else {
            MALFORMED_DISK_CACHE;
        }
    }

    // Switch to append mode to receive new entries.
    return pl_cache.SwitchMode(CacheFile::CacheOpMode::APPEND);
}

void ShaderDiskCache::AppendVSConfigProgram(CacheFile& file,
                                            const Pica::Shader::Generator::PicaVSConfig& config,
                                            const Pica::ShaderSetup& setup, u64 config_id,
                                            u64 spirv_id) {

    VSConfigEntry entry;
    entry.version = VSConfigEntry::EXPECTED_VERSION;
    entry.vs_config = config;
    entry.spirv_entry_id = spirv_id;
    entry.program_entry_id =
        Common::HashCombine(config.state.program_hash, config.state.swizzle_hash);

    bool new_entry = known_vertex_programs.emplace(entry.program_entry_id).second;
    if (new_entry) {
        std::unique_ptr<VSProgramEntry> prog_entry = std::make_unique<VSProgramEntry>();
        prog_entry->version = VSProgramEntry::EXPECTED_VERSION;
        prog_entry->program_len = setup.GetBiggestProgramSize();
        prog_entry->program_code = setup.GetProgramCode();
        prog_entry->swizzle_len = setup.GetBiggestSwizzleSize();
        prog_entry->swizzle_code = setup.GetSwizzleData();

        AppendVSProgram(file, *prog_entry, entry.program_entry_id);
    }

    AppendVSConfig(file, entry, config_id);
}

void ShaderDiskCache::AppendVSProgram(CacheFile& file, const VSProgramEntry& entry,
                                      u64 program_id) {
    file.Append(CacheEntryType::VS_PROGRAM, program_id, entry, true);
}

void ShaderDiskCache::AppendVSConfig(CacheFile& file, const VSConfigEntry& entry, u64 config_id) {
    file.Append(CacheEntryType::VS_CONFIG, config_id, entry, true);
}

void ShaderDiskCache::AppendVSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id) {
    file.Append(CacheEntryType::VS_SPIRV, program_id,
                {reinterpret_cast<const u8*>(program.data()), program.size() * sizeof(u32)}, true);
}

void ShaderDiskCache::AppendFSConfig(CacheFile& file, const FSConfigEntry& entry, u64 config_id) {
    file.Append(CacheEntryType::FS_CONFIG, config_id, entry, true);
}

void ShaderDiskCache::AppendFSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id) {
    file.Append(CacheEntryType::FS_SPIRV, program_id,
                {reinterpret_cast<const u8*>(program.data()), program.size() * sizeof(u32)}, true);
}

void ShaderDiskCache::AppendGSConfig(CacheFile& file, const GSConfigEntry& entry, u64 config_id) {
    file.Append(CacheEntryType::GS_CONFIG, config_id, entry, true);
}

void ShaderDiskCache::AppendGSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id) {
    file.Append(CacheEntryType::GS_SPIRV, program_id,
                {reinterpret_cast<const u8*>(program.data()), program.size() * sizeof(u32)}, true);
}

void ShaderDiskCache::AppendPLConfig(CacheFile& file, const PLConfigEntry& entry, u64 config_id) {
    file.Append(CacheEntryType::PL_CONFIG, config_id, entry, true);
}

} // namespace Vulkan
