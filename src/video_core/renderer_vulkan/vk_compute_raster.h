// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <memory>
#include <span>
#include "video_core/renderer_vulkan/uberhar_compute_raster_shader.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/shader_uniforms.h"

namespace Vulkan {
class Instance;
class Scheduler;
class DescriptorUpdateQueue;
class Surface;

// CodexAstraLocal: This owner joins real cache resources to ordered
// compute rasterization. It neither selects a graphics fallback nor treats an
// unsupported draw as completed. Destruction requires the renderer's GPU drain.
class ComputeRasterizer final {
public:
    // CodexAstraLocal: Units 0..2 retain their ordinary slots. Shadow-cube
    // face zero uses slot 0 and faces 1..5 use slots 3..7; each face remains
    // an independently owned typed cache image, not a fabricated cube upload.
    using TextureBindings = std::array<vk::DescriptorImageInfo, 8>;
    using LutBindings = std::array<vk::BufferView, 3>;

    // CodexAstraLocal: The independent vertex/primitive producer supplies
    // this GPU allocation, already containing a complete finite clipped list
    // in the production 88-byte ABI. Its allocation must survive the draw tick;
    // this interface does not certify arbitrary guest geometry by itself.
    struct GpuVertices {
        vk::Buffer buffer;
        vk::DeviceSize bytes;
        u32 first_word{};
    };

    ComputeRasterizer(const Instance& instance, Scheduler& scheduler,
                      DescriptorUpdateQueue& updates);
    ~ComputeRasterizer();

    // CodexAstraLocal: Packed-color and depth-only paths still bind a valid
    // R32 storage image for the inactive image branch of the shared kernel.
    void DrawGpu(Surface* color, Surface* depth, vk::ImageView inactive_color_view,
                 const ComputeRasterPush& push, const Pica::Shader::FSConfig& config,
                 const Pica::Shader::UserConfig& user, const Pica::Shader::Profile& profile,
                 const Pica::Shader::Generator::FSUniformData& uniforms,
                 const TextureBindings& textures, const LutBindings& luts,
                 const GpuVertices& vertices);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Vulkan
