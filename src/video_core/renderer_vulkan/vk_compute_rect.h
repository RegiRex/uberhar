// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <chrono>
#include <memory>
#include "common/settings.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"
#include "video_core/renderer_vulkan/uberhar_compute_census.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace Vulkan {
class Instance;
class Scheduler;
class DescriptorUpdateQueue;
class Surface;

// AstraEH: Own a startup-compiled compute rasterization subset and nonblocking
// GPU measurements. All methods run on the renderer thread; scheduler lambdas
// only record commands. Destruction requires draining the scheduler/GPU first.
class ComputeRectRenderer {
public:
    ComputeRectRenderer(const Instance& instance, Scheduler& scheduler,
                        DescriptorUpdateQueue& updates);
    void Initialize(vk::PipelineCache cache);
    bool Ready() const {
        return bool(pipeline);
    }
    // CodexAstraLocal: Immutable mode policy keeps new proof work off Native;
    // this does not bypass startup readiness or the existing measured chooser.
    bool AllowsExpandedRectangles() const;
    bool Choose(const ComputeRectPacket& packet);
    void Draw(Surface& surface, const ComputeRectPacket& packet);
    int ReserveSample(bool compute, u64 pixels);
    void BeginSample(int slot);
    void EndSample(int slot);
    void Poll();
    void Report();
    // AstraEH: Count all blocking state components, not just the first one.
    // CodexAstraLocal: Replace the per-draw marginal loop with the full joint
    // interval bank. Observe the already-computed state mask exactly once,
    // including mask 0; geometry and fragment qualification remain unchanged.
    void ObserveState(u32 reasons, std::size_t vertices) noexcept {
        state_census.Record(reasons, vertices);
        // CodexAstraLocal: Raw state blockers remain comparable across admission
        // changes; they must not be confused with effective fallback decisions.
        if (reasons != 0)
            ComputeStateCensus::Add(raw_unsupported, 1, census_overflow);
    }
    // CodexAstraLocal: Reuse the caller's existing 30s clock/cadence. A final
    // partial interval is distinct from cumulative lifetime blocker totals.
    void ReportCensus(std::chrono::steady_clock::time_point now, bool final = false) noexcept;
    // AstraEH: Counters describe actual route coverage, including rejected states.
    u64 considered{}, unsupported{}, geometry_rejected{}, format_rejected{}, eligible{},
        native_draws{}, compute_draws{}, compute_pixels{};

private:
    struct Sample {
        u64 tick{};
        unsigned bucket{};
        bool compute{};
        bool pending{};
    };
    const Instance& instance;
    Scheduler& scheduler;
    DescriptorUpdateQueue& updates;
    const Settings::UberharTestMode mode;
    std::unique_ptr<DescriptorHeap> heap;
    vk::UniquePipelineLayout layout;
    vk::UniqueShaderModule module;
    vk::UniquePipeline pipeline;
    vk::UniqueQueryPool queries;
    std::array<Sample, 32> samples{};
    ComputeRectSelector selector;
    std::array<u64, static_cast<unsigned>(ComputeRectReject::Count)> rejected_state{};
    // CodexAstraLocal: One 32 KiB interval bank; lifetime marginals are accumulated
    // only on reports. This adds no per-draw clock, allocation or guest read.
    ComputeStateCensus state_census;
    std::chrono::steady_clock::time_point census_start{std::chrono::steady_clock::now()};
    u64 census_sequence{}, census_considered{}, census_unsupported{}, census_log_failures{};
    // CodexAstraLocal: Two scalar totals join unchanged raw-mask intervals to
    // effective route counts without another histogram or per-draw state scan.
    u64 raw_unsupported{}, census_raw_unsupported{};
    bool census_overflow{};
    static_assert(static_cast<unsigned>(ComputeRectReject::Count) == ComputeStateCensus::ReasonBits);
    double timestamp_period{};
    u64 timestamp_mask{}, measurement_attempts{};
    std::array<u64, 2> measured_draws{};
    std::array<double, 2> measured_ns{};
};
} // namespace Vulkan
