// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Exercise admission/rejection and measured routing using production
// code.
#include <cstdio>
#include <stdexcept>
#include "common/vector_math.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"

struct Vertex {
    Common::Vec4f position, color;
};
void Check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}

int main() {
    using namespace Vulkan;
    using T = Pica::TexturingRegs::TevStageConfig;
    Pica::RegsInternal regs{};
    regs.framebuffer.framebuffer.allow_color_write.Assign(1);
    regs.framebuffer.output_merger.depth_color_mask = 0xf00;
    regs.framebuffer.output_merger.logic_op.Assign(Pica::FramebufferRegs::LogicOp::Copy);
    auto stage = T{};
    stage.color_source1.Assign(T::Source::PrimaryColor);
    stage.alpha_source1.Assign(T::Source::PrimaryColor);
    regs.texturing.tev_stage0 = stage;
    stage.color_source1.Assign(T::Source::Previous);
    stage.alpha_source1.Assign(T::Source::Previous);
    regs.texturing.tev_stage1 = regs.texturing.tev_stage2 = regs.texturing.tev_stage3 =
        regs.texturing.tev_stage4 = regs.texturing.tev_stage5 = stage;
    const auto base = regs;
    constexpr Common::Vec4f color{1.f, 0.f, 1.f, 1.f};
    const std::array<Vertex, 6> vertices{{{{-1, -1, -0.5f, 1}, color},
                                          {{1, -1, -0.5f, 1}, color},
                                          {{1, 1, -0.5f, 1}, color},
                                          {{-1, -1, -0.5f, 1}, color},
                                          {{1, 1, -0.5f, 1}, color},
                                          {{-1, 1, -0.5f, 1}, color}}};
    auto admit = [&](auto& v) {
        return MakeComputeRect(regs, std::span<const Vertex>{v}, {0, 0, 64, 32}, {0, 0, 64, 32});
    };
    auto result = admit(vertices);
    Check(result && result->rect == std::array<s32, 4>{0, 0, 64, 32} && result->color == 0xffff00ff,
          "solid rectangle/color not recognized");
    // AstraEH: Vertex order/winding must not change an uncullled rectangle's
    // coverage.
    unsigned permutations = 0;
    for (unsigned rotation = 0; rotation < 3; ++rotation) {
        for (unsigned rotation2 = 0; rotation2 < 3; ++rotation2) {
            auto v = vertices;
            std::rotate(v.begin(), v.begin() + rotation, v.begin() + 3);
            std::rotate(v.begin() + 3, v.begin() + 3 + rotation2, v.end());
            Check(admit(v).has_value(), "triangle rotation rejected");
            std::swap(v[0], v[1]);
            std::swap(v[3], v[4]);
            Check(admit(v).has_value(), "triangle winding rejected");
            permutations += 2;
        }
    }
    auto clipped =
        MakeComputeRect(regs, std::span<const Vertex>{vertices}, {0, 0, 64, 32}, {7, 3, 19, 17});
    Check(clipped && clipped->rect == std::array<s32, 4>{7, 3, 12, 14},
          "surface subrectangle incorrect");
    auto reject = [&](auto mutation) {
        regs = base;
        mutation();
        Check(!admit(vertices), "unsupported state admitted");
    };
    reject([&] { regs.framebuffer.output_merger.depth_test_enable.Assign(1); });
    reject([&] { regs.framebuffer.output_merger.depth_write_enable.Assign(1); });
    reject([&] { regs.framebuffer.output_merger.stencil_test.enable.Assign(1); });
    reject([&] { regs.framebuffer.output_merger.alpha_test.enable.Assign(1); });
    reject([&] { regs.rasterizer.clip_enable.Assign(1); });
    reject([&] {
        regs.rasterizer.scissor_test.mode.Assign(Pica::RasterizerRegs::ScissorMode::Include);
    });
    reject([&] { regs.texturing.tev_stage0.color_source1.Assign(T::Source::Texture0); });
    reject([&] { regs.texturing.tev_stage0.color_op.Assign(T::Operation::AddSigned); });
    regs = base;
    for (unsigned corruption = 0; corruption < 6; ++corruption) {
        auto v = vertices;
        switch (corruption) {
        case 0:
            v[0].position.w = 0;
            break;
        case 1:
            v[0].position.x = std::numeric_limits<float>::quiet_NaN();
            break;
        case 2:
            v[0].position.x = -0.75f;
            break;
        case 3:
            v[1] = v[0];
            break;
        case 4:
            v[0].color.x = 0.25f;
            break;
        case 5:
            v[0].position.z = 0.5f;
            break;
        }
        Check(!admit(v), "invalid rectangle accepted");
    }
    // AstraEH: Learn either winner, continue exploration, and adapt to changed
    // costs.
    for (bool winner : {false, true}) {
        ComputeRectSelector selector;
        const auto bucket = ComputeRectSelector::Bucket(2048);
        unsigned wins = 0;
        for (unsigned i = 0; i < 256; ++i) {
            const bool selected = selector.Select(bucket);
            selector.Record(bucket, selected, selected == winner ? 100 : 1000);
            if (i >= 64 && selected == winner)
                ++wins;
        }
        Check(wins > 180, "selector did not learn the measured faster route");
    }
    // AstraEH: Reproduce production's sparse sampling. Both exploration routes
    // must remain observable, and a changed winner must eventually replace the old one.
    ComputeRectSelector adaptive;
    const auto bucket = ComputeRectSelector::Bucket(2048);
    std::array<unsigned, 2> route_samples{};
    unsigned adapted_wins = 0;
    for (unsigned i = 0; i < 4096; ++i) {
        const bool selected = adaptive.Select(bucket);
        const bool winner = i < 1024;
        if (i < 64 || adaptive.NeedsSample(bucket) || ((i + 1) & 31) == 0) {
            adaptive.Record(bucket, selected, selected == winner ? 100 : 1000);
            ++route_samples[selected];
        }
        if (i >= 3072 && selected == winner)
            ++adapted_wins;
    }
    Check(route_samples[0] > 10 && route_samples[1] > 10, "sparse sampling starved one route");
    Check(adapted_wins > 990, "sparse selector did not adapt to a changed winner");
    Check(ComputeRectSelector::Bucket(std::numeric_limits<u64>::max()) ==
              ComputeRectSelector::Buckets - 1,
          "large rectangle bucket overflowed");
    std::printf("PASS: %u rectangle permutations, clipping, unsafe-state "
                "rejection, measured routing\n",
                permutations);
}
