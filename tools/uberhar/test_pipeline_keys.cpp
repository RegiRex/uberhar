// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Production execution-key tests. Inactive state and aliased guest IDs
// may merge; different active native state or host modules must never merge.
#include <stdexcept>
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

int main() {
    using namespace Vulkan;
    using FB = Pica::FramebufferRegs;
    const std::array<u64, 3> modules{0x100, 0x200, 0};
    PipelineInfo base{};
    base.state.shader_ids = {1, 2, 3};
    base.state.vertex_layout.binding_count = 1;
    base.state.vertex_layout.attribute_count = 1;
    base.state.vertex_layout.bindings[0].byte_count.Assign(16);
    base.state.vertex_layout.attributes[0].type.Assign(
        Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
    base.state.vertex_layout.attributes[0].size.Assign(4);
    base.state.blending.blend_enable = 1;
    base.state.blending.color_write_mask = 15;
    base.state.blending.logic_op = FB::LogicOp::Copy;
    base.state.depth_stencil.depth_test_enable.Assign(1);
    base.state.depth_stencil.depth_compare_op.Assign(FB::CompareFunc::LessThan);
    u32 checked{};
    const auto pair = [&](const PipelineInfo& a, const PipelineInfo& b, bool same, bool dynamic,
                          const char* label) {
        if ((a.state.ExecutionHash(dynamic, modules) == b.state.ExecutionHash(dynamic, modules)) !=
            same)
            throw std::runtime_error(label);
        ++checked;
    };
    for (bool dynamic : {false, true}) {
        auto other = base;
        other.state.shader_ids = {400, 500, 600};
        pair(base, other, true, dynamic, "Guest identities fragmented an identical host pipeline");
        other = base;
        for (std::size_t i = 1; i < other.state.vertex_layout.bindings.size(); ++i)
            other.state.vertex_layout.bindings[i].value = 0xffff;
        for (std::size_t i = 1; i < other.state.vertex_layout.attributes.size(); ++i)
            other.state.vertex_layout.attributes[i].value = 0xffffffff;
        pair(base, other, true, dynamic, "Inactive layout tails fragmented a pipeline");
        other = base;
        other.state.blending.logic_op = FB::LogicOp::Xor;
        pair(base, other, true, dynamic, "Inactive native logic operation fragmented a pipeline");
        other = base;
        other.state.depth_stencil.stencil_compare_op.Assign(FB::CompareFunc::Never);
        other.state.depth_stencil.stencil_fail_op.Assign(FB::StencilAction::Invert);
        pair(base, other, true, dynamic, "Disabled stencil state fragmented a pipeline");
        auto disabled = base;
        disabled.state.blending.blend_enable = 0;
        other = disabled;
        other.state.blending.value = 0x123456;
        pair(disabled, other, true, dynamic, "Disabled blend factors fragmented a pipeline");
        other = disabled;
        other.state.blending.logic_op = FB::LogicOp::Xor;
        pair(disabled, other, false, dynamic, "Active native logic operation collapsed");
        disabled = base;
        disabled.state.depth_stencil.depth_test_enable.Assign(0);
        other = disabled;
        other.state.depth_stencil.depth_compare_op.Assign(FB::CompareFunc::Never);
        pair(disabled, other, true, dynamic, "Disabled depth comparison fragmented a pipeline");

        // AstraEH: Change one consumed native dimension at a time.
        other = base;
        other.state.vertex_layout.bindings[0].byte_count.Assign(32);
        pair(base, other, false, dynamic, "Active vertex stride collapsed");
        other = base;
        other.state.vertex_layout.attributes[0].size.Assign(3);
        pair(base, other, false, dynamic, "Active attribute interface collapsed");
        other = base;
        other.state.blending.src_color_blend_factor.Assign(FB::BlendFactor::SourceAlpha);
        pair(base, other, false, dynamic, "Active blend factor collapsed");
        other = base;
        other.state.blending.color_write_mask = 7;
        pair(base, other, false, dynamic, "Color write mask collapsed");
        other = base;
        other.state.attachments.color = VideoCore::PixelFormat::RGB8;
        pair(base, other, false, dynamic, "Attachment format collapsed");
        other = base;
        other.state.rasterization.topology.Assign(Pica::PipelineRegs::TriangleTopology::Strip);
        pair(base, other, dynamic, dynamic, "Topology did not follow enabled dynamic state");
        other = base;
        other.state.depth_stencil.depth_compare_op.Assign(FB::CompareFunc::GreaterThan);
        pair(base, other, dynamic, dynamic, "Active depth comparison collapsed");
        auto stencil = base;
        stencil.state.depth_stencil.stencil_test_enable.Assign(1);
        other = stencil;
        other.state.depth_stencil.stencil_fail_op.Assign(FB::StencilAction::Invert);
        pair(stencil, other, dynamic, dynamic, "Active stencil operation collapsed");
        for (u32 stage = 0; stage < 3; ++stage) {
            auto different = modules;
            ++different[stage];
            if (base.state.ExecutionHash(dynamic, modules) ==
                base.state.ExecutionHash(dynamic, different))
                throw std::runtime_error("Different host shader module collapsed");
            ++checked;
        }
    }
    fmt::print("PASS: {} production pipeline-key alias/distinction checks\n", checked);
}
