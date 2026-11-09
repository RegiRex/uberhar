// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <array>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include "video_core/uberhar_cpu_clip_reject.h"

using namespace VideoCore::CpuClipReject;
struct Vertex { Position position; std::array<std::uint8_t, 72> other; };
static_assert(sizeof(Vertex) == 88);

int main() {
    const Guard yes{true, true, true, true, true, true, true, true, true};
    const std::array<Position, 3> right{{{2, 0, -0.5f, 1}, {3, 0.5f, -0.8f, 1}, {4, -0.5f, 5, 1}}};
    assert(Classify(yes, right).Rejected());
    for (unsigned plane = 0; plane < 4; ++plane) {
        auto v = right;
        for (auto& p : v) {
            const float outside = p[0];
            p[0] = 0; p[1] = 0;
            p[plane / 2] = plane % 2 ? -outside : outside;
        }
        const auto result = Classify(yes, v);
        assert(result.Rejected() && result.outside_planes == (1U << plane));
        for (auto& p : v) p[1] = -p[1]; // Trivial VS viewport flip cannot change visibility.
        assert(Classify(yes, v).Rejected());
    }
    auto touching = right; touching[1][0] = touching[1][3];
    assert(!Classify(yes, touching).Rejected());
    auto just_out = right;
    for (auto& p : just_out) p[0] = std::nextafter(p[3], std::numeric_limits<float>::infinity());
    assert(Classify(yes, just_out).Rejected()); // Exact strict boundary, no epsilon.
    auto spanning = right; spanning[1][0] = -3;
    assert(!Classify(yes, spanning).Rejected()); // Different outside planes do not reject.
    auto depth_only = right;
    for (auto& p : depth_only) { p[0] = 0; p[1] = 0; p[2] = 10000; }
    assert(!Classify(yes, depth_only).Rejected()); // No depth/sanitize approximation.
    for (const auto w : {0.f, -0.f, -1.f}) {
        auto v = right; v[1][3] = w;
        assert(Classify(yes, v).decision == Decision::NonPositiveW);
    }
    for (const auto bad : {std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
        for (unsigned i = 0; i < 4; ++i) {
            auto v = right; v[2][i] = bad;
            assert(Classify(yes, v).decision == Decision::NonFinite);
        }
    }
    auto denorm = right; denorm[1][0] = std::numeric_limits<float>::denorm_min();
    assert(Classify(yes, denorm).decision == Decision::Subnormal);
    std::array guards{&Guard::enabled, &Guard::vulkan, &Guard::trivial_vertex_shader,
        &Guard::no_geometry_shader, &Guard::triangle_list, &Guard::no_compute_consumer,
        &Guard::no_capture, &Guard::no_deferred, &Guard::no_bridge};
    for (const auto flag : guards) {
        auto no = yes; no.*flag = false;
        assert(Classify(no, right).decision == Decision::Ineligible);
    }
    std::array<Vertex, 9> records{};
    for (unsigned i = 0; i < records.size(); ++i) {
        records[i].position = right[i % 3];
        records[i].other.fill(static_cast<std::uint8_t>(i));
    }
    records[4].position[0] = 0; // Only middle triangle survives.
    const auto original = records;
    const auto position = [](const Vertex& v) noexcept { return v.position; };
    unsigned reads = 0;
    const auto off = Compact(std::span{records}, Guard{}, [&](const Vertex& v) noexcept { ++reads; return v.position; });
    assert(reads == 0 && off.output_vertices == 9 && !std::memcmp(records.data(), original.data(), sizeof(records)));
    const auto compact = Compact(std::span{records}, yes, position);
    assert(compact.inspected_triangles == 3 && compact.rejected_triangles == 2);
    assert(compact.output_vertices == 3 && compact.avoided_upload_bytes == 6 * sizeof(Vertex));
    assert(!std::memcmp(records.data(), original.data() + 3, 3 * sizeof(Vertex)));
    assert(Compact(std::span<Vertex>{records.data(), 2}, yes, position).malformed);
    auto all = original; all[4].position[0] = 3;
    assert(Compact(std::span{all}, yes, position).output_vertices == 0);
    // CodexAstraLocal: Integer classification cannot add/clear host FP exception state.
    std::feclearexcept(FE_ALL_EXCEPT); std::feraiseexcept(FE_INVALID | FE_DIVBYZERO);
    const int before = std::fetestexcept(FE_ALL_EXCEPT);
    volatile bool rejected = Classify(yes, right).Rejected(); (void)rejected;
    assert(std::fetestexcept(FE_ALL_EXCEPT) == before);
    std::puts("PASS: exact homogeneous X/Y clip rejection, guards, special values, stable byte compaction and FP status");
}
