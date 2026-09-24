// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <memory>
#include "common/settings.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"
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
    bool Choose(const ComputeRectPacket& packet);
    void Draw(Surface& surface, const ComputeRectPacket& packet);
    int ReserveSample(bool compute, u64 pixels);
    void BeginSample(int slot);
    void EndSample(int slot);
    void Poll();
    void Report() const;
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
    double timestamp_period{};
    u64 timestamp_mask{}, measurement_attempts{};
    std::array<u64, 2> measured_draws{};
    std::array<double, 2> measured_ns{};
};
} // namespace Vulkan
