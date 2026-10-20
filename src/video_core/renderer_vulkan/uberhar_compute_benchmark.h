// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include "video_core/pica/output_vertex.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"

namespace Vulkan::ComputeBenchmarkData {

// CodexAstraLocal: Fixed synthetic work is independent of guest registers and
// selector history. Four cases and one in-flight pair bound all queued work.
constexpr u32 CaseCount = 4;
constexpr u32 PairsPerCase = 8;
constexpr u32 OperationsPerRoute = 32;
constexpr u32 RouteCount = CaseCount * PairsPerCase * 2;
constexpr u32 MaxWidth = 800, MaxHeight = 480;
constexpr u32 MaxImageBytes = MaxWidth * MaxHeight * 4;
constexpr u32 QueryCount = RouteCount * 2;
constexpr u32 DeadlineSeconds = 120;
static_assert(RouteCount == 64 && QueryCount == 128);

struct Case {
    u32 width, height, channels;
};
constexpr std::array Cases{Case{256, 256, 15}, Case{256, 256, 5},
                           Case{800, 480, 15}, Case{800, 480, 5}};

// CodexAstraLocal: Original HardwareVertex bytes are constructed through the
// production constructor. Two overlapping rectangles use different colors;
// repeated cycles remain an idempotent workload, not proof of every command.
template <typename HardwareVertex>
struct Workload {
    Pica::RegsInternal regs{};
    std::array<HardwareVertex, 12> vertices{};
    std::array<ComputeRectPacket, 2> packets{};
};

template <typename HardwareVertex>
bool Prepare(u32 index, Workload<HardwareVertex>& out) {
    if (index >= Cases.size())
        return false;
    const auto c = Cases[index];
    auto& regs = out.regs;
    regs = {};
    regs.framebuffer.framebuffer.allow_color_write.Assign(1);
    regs.framebuffer.framebuffer.flip.Assign(1);
    regs.framebuffer.output_merger.depth_color_mask = c.channels << 8;
    regs.framebuffer.output_merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    regs.lighting.disable.Assign(1);
    using T = Pica::TexturingRegs::TevStageConfig;
    T stage{};
    stage.color_source1.Assign(T::Source::PrimaryColor);
    stage.alpha_source1.Assign(T::Source::PrimaryColor);
    regs.texturing.tev_stage0 = stage;
    stage.color_source1.Assign(T::Source::Previous);
    stage.alpha_source1.Assign(T::Source::Previous);
    regs.texturing.tev_stage1 = regs.texturing.tev_stage2 = regs.texturing.tev_stage3 =
        regs.texturing.tev_stage4 = regs.texturing.tev_stage5 = stage;
    constexpr std::array<std::array<float, 4>, 2> bounds{{
        {-.75f, -.75f, .5f, .5f}, {-.25f, -.25f, .75f, .75f}}};
    constexpr std::array<std::array<u32, 4>, 2> colors{{{31, 113, 197, 255}, {211, 71, 43, 255}}};
    constexpr std::array<unsigned, 6> corners{0, 1, 2, 0, 2, 3};
    for (u32 rectangle = 0; rectangle < 2; ++rectangle) {
        const auto b = bounds[rectangle];
        const std::array<std::array<float, 2>, 4> xy{{
            {b[0], b[1]}, {b[2], b[1]}, {b[2], b[3]}, {b[0], b[3]}}};
        for (u32 vertex = 0; vertex < 6; ++vertex) {
            Pica::OutputVertex p{};
            p.pos = {Pica::f24::FromFloat32(xy[corners[vertex]][0]),
                     Pica::f24::FromFloat32(xy[corners[vertex]][1]),
                     Pica::f24::FromFloat32(-.5f), Pica::f24::FromFloat32(1.f)};
            for (u32 lane = 0; lane < 4; ++lane)
                p.color[lane] = Pica::f24::FromFloat32(colors[rectangle][lane] / 255.f);
            out.vertices[rectangle * 6 + vertex] = HardwareVertex{p, false};
        }
        const auto packet = MakeComputeRect(regs,
            std::span<const HardwareVertex>{out.vertices}.subspan(rectangle * 6, 6),
            {0, 0, static_cast<s32>(c.width), static_cast<s32>(c.height)},
            {0, 0, static_cast<s32>(c.width), static_cast<s32>(c.height)});
        if (!packet || !packet->PixelCount() || !packet->byte_mask)
            return false;
        out.packets[rectangle] = *packet;
    }
    return true;
}

// CodexAstraLocal: Independent integer image oracle starts from a known pattern
// and applies the two specified rectangles, rather than deriving expected pixels
// from the compute packet under test. All disabled bytes must remain unchanged.
constexpr u32 Background(u32 x, u32 y) {
    return ((x * 17 + y * 3 + 9) & 255) | (((x * 5 + y * 19 + 23) & 255) << 8) |
           (((x * 11 + y * 7 + 47) & 255) << 16) | (((x * 13 + y * 23 + 101) & 255) << 24);
}

constexpr u32 Expected(u32 index, u32 x, u32 y) {
    const auto c = Cases[index];
    u32 value = Background(x, y);
    u32 mask = 0;
    for (u32 lane = 0; lane < 4; ++lane)
        if (c.channels & (1U << lane))
            mask |= 255U << (lane * 8);
    // CodexAstraLocal: Bounds are fixed fractions, independent of the float
    // projection/packet implementation and exact for both chosen extents.
    if (x >= c.width / 8 && x < c.width * 3 / 4 &&
        y >= c.height / 8 && y < c.height * 3 / 4)
        value = (value & ~mask) | (0xffc5711fU & mask);
    if (x >= c.width * 3 / 8 && x < c.width * 7 / 8 &&
        y >= c.height * 3 / 8 && y < c.height * 7 / 8)
        value = (value & ~mask) | (0xff2b47d3U & mask);
    return value;
}

} // namespace Vulkan::ComputeBenchmarkData
