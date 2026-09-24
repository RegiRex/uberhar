// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono> // AstraEH: Pipeline queue and driver timings.

#include "common/async_handle.h" // AstraEH: Shared completion signal for first-ready selection.
#include "common/hash.h"
#include "common/thread_worker.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica/regs_rasterizer.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_common.h"

#define LAYOUT_HASH static_cast<u64>(sizeof(T)), static_cast<u64>(alignof(T))
#define FIELD_HASH(x) static_cast<u64>(offsetof(T, x)), static_cast<u64>(sizeof(x))

namespace Vulkan {

class Instance;
class RenderManager;

constexpr u32 MAX_SHADER_STAGES = 3;
constexpr u32 MAX_VERTEX_ATTRIBUTES = 16;
constexpr u32 MAX_VERTEX_BINDINGS = 13;

/**
 * The pipeline state is tightly packed with bitfields to reduce
 * the overhead of hashing as much as possible
 */
union RasterizationState {
    u8 value;
    BitField<0, 2, Pica::PipelineRegs::TriangleTopology> topology;
    BitField<4, 2, Pica::RasterizerRegs::CullMode> cull_mode;
    BitField<6, 1, u8> flip_viewport;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = RasterizationState;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(topology), FIELD_HASH(cull_mode),
                                   FIELD_HASH(flip_viewport));
    }
};
static_assert(std::is_trivial_v<RasterizationState>);

union DepthStencilState {
    u32 value;
    BitField<0, 1, u32> depth_test_enable;
    BitField<1, 1, u32> depth_write_enable;
    BitField<2, 1, u32> stencil_test_enable;
    BitField<3, 3, Pica::FramebufferRegs::CompareFunc> depth_compare_op;
    BitField<6, 3, Pica::FramebufferRegs::StencilAction> stencil_fail_op;
    BitField<9, 3, Pica::FramebufferRegs::StencilAction> stencil_pass_op;
    BitField<12, 3, Pica::FramebufferRegs::StencilAction> stencil_depth_fail_op;
    BitField<15, 3, Pica::FramebufferRegs::CompareFunc> stencil_compare_op;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = DepthStencilState;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(depth_test_enable), FIELD_HASH(depth_write_enable),
                                   FIELD_HASH(stencil_test_enable), FIELD_HASH(depth_compare_op),
                                   FIELD_HASH(stencil_fail_op), FIELD_HASH(stencil_pass_op),
                                   FIELD_HASH(stencil_depth_fail_op),
                                   FIELD_HASH(stencil_compare_op));
    }
};
static_assert(std::is_trivial_v<DepthStencilState>);

struct BlendingState {
    u16 blend_enable;
    u16 color_write_mask;
    Pica::FramebufferRegs::LogicOp logic_op;
    union {
        u32 value;
        BitField<0, 4, Pica::FramebufferRegs::BlendFactor> src_color_blend_factor;
        BitField<4, 4, Pica::FramebufferRegs::BlendFactor> dst_color_blend_factor;
        BitField<8, 3, Pica::FramebufferRegs::BlendEquation> color_blend_eq;
        BitField<11, 4, Pica::FramebufferRegs::BlendFactor> src_alpha_blend_factor;
        BitField<15, 4, Pica::FramebufferRegs::BlendFactor> dst_alpha_blend_factor;
        BitField<19, 3, Pica::FramebufferRegs::BlendEquation> alpha_blend_eq;
    };

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = BlendingState;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(blend_enable), FIELD_HASH(color_write_mask),
                                   FIELD_HASH(logic_op), FIELD_HASH(src_color_blend_factor),
                                   FIELD_HASH(dst_color_blend_factor), FIELD_HASH(color_blend_eq),
                                   FIELD_HASH(src_alpha_blend_factor),
                                   FIELD_HASH(dst_alpha_blend_factor), FIELD_HASH(alpha_blend_eq));
    }
};
static_assert(std::is_trivial_v<BlendingState>);

union VertexBinding {
    u16 value;
    BitField<0, 4, u16> binding;
    BitField<4, 1, u16> fixed;
    BitField<5, 11, u16> byte_count;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = VertexBinding;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(binding), FIELD_HASH(fixed), FIELD_HASH(byte_count));
    }
};
static_assert(std::is_trivial_v<VertexBinding>);

union VertexAttribute {
    u32 value;
    BitField<0, 4, u32> binding;
    BitField<4, 4, u32> location;
    BitField<8, 3, Pica::PipelineRegs::VertexAttributeFormat> type;
    BitField<11, 3, u32> size;
    BitField<14, 11, u32> offset;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = VertexAttribute;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(binding), FIELD_HASH(location), FIELD_HASH(type),
                                   FIELD_HASH(size), FIELD_HASH(offset));
    }
};
static_assert(std::is_trivial_v<VertexAttribute>);

struct VertexLayout {
    u8 binding_count;
    u8 attribute_count;
    std::array<VertexBinding, MAX_VERTEX_BINDINGS> bindings;
    std::array<VertexAttribute, MAX_VERTEX_ATTRIBUTES> attributes;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = VertexLayout;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(binding_count), FIELD_HASH(attribute_count),
                                   FIELD_HASH(bindings), FIELD_HASH(attributes),

                                   // nested layout
                                   VertexBinding::StructHash(), VertexAttribute::StructHash());
    }
};
static_assert(std::is_trivial_v<VertexLayout>);

struct AttachmentInfo {
    VideoCore::PixelFormat color;
    VideoCore::PixelFormat depth;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = AttachmentInfo;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(color), FIELD_HASH(depth));
    }
};
static_assert(std::is_trivial_v<AttachmentInfo>);

struct StaticPipelineInfo {
    std::array<u64, MAX_SHADER_STAGES> shader_ids;

    BlendingState blending;
    AttachmentInfo attachments;
    VertexLayout vertex_layout;

    RasterizationState rasterization;
    DepthStencilState depth_stencil;

    [[nodiscard]] u64 OptimizedHash(const Instance& instance) const;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = StaticPipelineInfo;
        return Common::HashCombine(
            STRUCT_VERSION,

            // layout
            LAYOUT_HASH,

            // fields
            FIELD_HASH(shader_ids), FIELD_HASH(blending), FIELD_HASH(attachments),
            FIELD_HASH(vertex_layout), FIELD_HASH(rasterization), FIELD_HASH(depth_stencil),

            // nested layout
            BlendingState::StructHash(), AttachmentInfo::StructHash(), VertexLayout::StructHash(),
            RasterizationState::StructHash(), DepthStencilState::StructHash());
    }
};
static_assert(std::is_trivial_v<StaticPipelineInfo>);

struct DynamicPipelineInfo {
    u32 blend_color = 0;
    u8 stencil_reference;
    u8 stencil_compare_mask;
    u8 stencil_write_mask;

    Common::Rectangle<u32> scissor;
    Common::Rectangle<s32> viewport;

    bool operator==(const DynamicPipelineInfo& other) const noexcept {
        return std::memcmp(this, &other, sizeof(DynamicPipelineInfo)) == 0;
    }
};

/**
 * Information about a graphics pipeline
 */
struct PipelineInfo : Common::HashableStruct<StaticPipelineInfo> {
    DynamicPipelineInfo dynamic_info;

    [[nodiscard]] bool IsDepthWriteEnabled() const noexcept {
        const bool has_stencil = state.attachments.depth == VideoCore::PixelFormat::D24S8;
        const bool depth_write =
            state.depth_stencil.depth_test_enable && state.depth_stencil.depth_write_enable;
        const bool stencil_write = has_stencil && state.depth_stencil.stencil_test_enable &&
                                   dynamic_info.stencil_write_mask != 0;

        return depth_write || stencil_write;
    }

    [[nodiscard]] u16 GetFinalColorWriteMask(const Instance& instance);
};

struct Shader : public Common::AsyncHandle {
    explicit Shader(const Instance& instance);
    explicit Shader(const Instance& instance, vk::ShaderStageFlagBits stage, std::string code);
    ~Shader();

    [[nodiscard]] vk::ShaderModule Handle() const noexcept {
        return module;
    }

    vk::ShaderModule module;
    vk::Device device;
    std::string program;
};

// AstraEH: Atomics allow bounded progress reports while compiler workers remain active.
// Durations are wall time; parallel work overlaps and must not be summed as gameplay stalls.
struct PipelineBuildStats {
    std::atomic<u64> builds{};
    std::atomic<u64> queue_ns{};
    std::array<std::atomic<u64>, 3> shader_wait_ns{};
    std::atomic<u64> driver_ns{};
    std::atomic<u64> driver_max_ns{};
    std::atomic<u64> slow_builds{};
};

struct PipelineBuildOptions {
    Common::AsyncCompletion* completion{};
    PipelineBuildStats* stats{};
    // AstraEH: Diagnostic label only; both paths use normal driver optimization.
    bool is_fallback{};
};

class GraphicsPipeline : public Common::AsyncHandle {
public:
    explicit GraphicsPipeline(const Instance& instance, RenderManager& renderpass_cache,
                              const PipelineInfo& info, vk::PipelineCache pipeline_cache,
                              vk::PipelineLayout layout, std::array<Shader*, 3> stages,
                              Common::ThreadWorker* worker, PipelineBuildOptions options = {});
    ~GraphicsPipeline();

    // AstraEH: Hybrid draws must not enter driver cache probes on the rendering thread.
    bool TryBuild(bool wait_built, bool background_only = false);

    bool Build(bool fail_on_compile_required = false);

    // AstraEH: Nonblocking snapshots used to identify the dependency at a slow wait's start.
    [[nodiscard]] u32 PendingShaderMask() const noexcept;
    [[nodiscard]] u32 BuildPhase() const noexcept {
        return build_phase.load(std::memory_order::relaxed);
    }
    [[nodiscard]] u64 Key() const noexcept;

    // AstraEH: Track whether speculative compilation ever served a draw. Atomics
    // permit progress snapshots while the command/compiler workers are active.
    void RecordFallbackUse() noexcept {
        fallback_uses.fetch_add(1, std::memory_order::relaxed);
    }
    [[nodiscard]] u64 FallbackUses() const noexcept {
        return fallback_uses.load(std::memory_order::relaxed);
    }
    [[nodiscard]] u64 DriverBuildNs() const noexcept {
        return driver_build_ns.load(std::memory_order::relaxed);
    }

    [[nodiscard]] vk::Pipeline Handle() const noexcept {
        return *pipeline;
    }

private:
    const Instance& instance;
    RenderManager& renderpass_cache;
    Common::ThreadWorker* worker;

    vk::UniquePipeline pipeline;
    vk::PipelineLayout pipeline_layout;
    vk::PipelineCache pipeline_cache;

    PipelineInfo info;
    std::array<Shader*, 3> stages;
    bool is_pending{};
    // AstraEH: Phase 0=queued/not started, 1=shader dependencies, 2=driver, 3=complete.
    const PipelineBuildOptions build_options;
    std::atomic<u32> build_phase{};
    std::atomic<u64> fallback_uses{};
    std::atomic<u64> driver_build_ns{};
    std::chrono::steady_clock::time_point queued_at{};
};

} // namespace Vulkan

#undef LAYOUT_HASH
#undef FIELD_HASH
