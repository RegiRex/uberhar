// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Compare the real PICA assembler with independent host-topology
// sequences, including indexing, winding, repeated route changes and exceptions.
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

using Topology = Pica::PipelineRegs::TriangleTopology;
using Triangle = std::array<u32, 3>;

void Check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<Triangle> Reference(Topology topology, const std::vector<u32>& indices) {
    std::vector<Triangle> expected;
    if (topology == Topology::List || topology == Topology::Shader) {
        for (u32 i = 0; i + 2 < indices.size(); i += 3)
            expected.push_back({indices[i], indices[i + 1], indices[i + 2]});
    } else {
        for (u32 i = 0; i + 2 < indices.size(); ++i) {
            if (topology == Topology::Fan) {
                expected.push_back({indices[0], indices[i + 1], indices[i + 2]});
            } else if (i & 1) {
                expected.push_back({indices[i + 1], indices[i], indices[i + 2]});
            } else {
                expected.push_back({indices[i], indices[i + 1], indices[i + 2]});
            }
        }
    }
    return expected;
}

void Submit(Pica::PrimitiveAssembler& assembler, const std::vector<u32>& indices,
            std::vector<Triangle>& triangles) {
    const auto handler = [&](const auto& a, const auto& b, const auto& c) {
        triangles.push_back({static_cast<u32>(a.pos.x.ToFloat32()),
                             static_cast<u32>(b.pos.x.ToFloat32()),
                             static_cast<u32>(c.pos.x.ToFloat32())});
    };
    for (u32 index : indices) {
        Pica::OutputVertex vertex{};
        vertex.pos.x = Pica::f24::FromFloat32(static_cast<float>(index));
        assembler.SubmitVertex(vertex, handler);
    }
}

int main() {
    u32 cases = 0;
    for (auto topology : {Topology::List, Topology::Strip, Topology::Fan, Topology::Shader}) {
        for (u32 count : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 31U, 1367U, 4095U}) {
            for (bool indexed : {false, true}) {
                std::vector<u32> indices(count);
                for (u32 i = 0; i < count; ++i)
                    indices[i] = indexed ? (i * 7) % 13 : i;
                Pica::PrimitiveAssembler assembler{topology};
                for (int repeat = 0; repeat < 3; ++repeat) {
                    std::vector<Triangle> actual;
                    assembler.RunIsolatedBatch([&] { Submit(assembler, indices, actual); });
                    Check(actual == Reference(topology, indices),
                          "Host/CPU triangle order differs");
                    Check(assembler.IsEmpty(), "Bridge tail leaked into the next accelerated draw");
                    Check(assembler.GetTopology() == topology,
                          "Bridge changed persistent topology");
                    ++cases;
                }
            }
        }
    }
    // AstraEH: Ordinary CPU batches still carry partial list/strip/fan state.
    for (auto topology : {Topology::List, Topology::Strip, Topology::Fan}) {
        Pica::PrimitiveAssembler assembler{topology};
        std::vector<Triangle> actual;
        Submit(assembler, {0, 1}, actual);
        Check(!assembler.IsEmpty(), "Ordinary partial vertices were lost");
        Submit(assembler, {2, 3, 4, 5}, actual);
        Check(actual == Reference(topology, {0, 1, 2, 3, 4, 5}), "Ordinary CPU continuity changed");
    }
    // AstraEH: The host path ignores pending GS winding; isolate it but restore it
    // for a later ordinary software batch, including exceptional/empty bridge exits.
    for (bool fail : {false, true}) {
        Pica::PrimitiveAssembler assembler{Topology::Shader};
        assembler.SetWinding();
        std::vector<Triangle> bridge;
        try {
            assembler.RunIsolatedBatch([&] {
                Submit(assembler, {0, 1, 2, 3}, bridge);
                if (fail)
                    throw std::runtime_error("injected vertex failure");
            });
        } catch (const std::runtime_error&) {
            Check(fail, "Unexpected exception");
        }
        Check(assembler.IsEmpty(), "Exception left partial bridge geometry");
        Check(bridge == std::vector<Triangle>{{0, 1, 2}}, "Bridge inherited stale winding");
        std::vector<Triangle> ordinary;
        Submit(assembler, {4, 5, 6}, ordinary);
        Check(ordinary == std::vector<Triangle>{{5, 4, 6}}, "Prior winding was not restored");
    }
    std::printf("PASS: %u production bridge assembly comparisons; ordinary continuity and "
                "exception restoration\n",
                cases);
}
