// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// Claude: PicaCore::CanOverlapDeferredVertices lets an ordinary List draw run
// before queued CPU packets are joined, then skips those packets' trailing-pair
// adoption. Prove on the real assembler that, from an empty List state, two or
// more submitted vertices emit the same triangles and leave the same serialized
// state whether or not the packet's pair was adopted first. Two intended defects
// (one vertex, and adopting the older pair afterwards) must be detected.
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <boost/serialization/access.hpp>
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

using namespace Pica;
using Topology = PipelineRegs::TriangleTopology;

static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static OutputVertex Value(u32 vertex, u32 seed) {
    std::array<u32, 24> words;
    for (u32 i = 0; i < words.size(); ++i)
        words[i] = 0x3f000000U | ((vertex * 193U + seed * 7919U + i) & 0x7fffffU);
    OutputVertex result;
    std::memcpy(&result, words.data(), sizeof(result));
    return result;
}

// Claude: Capture exactly what a save state stores, through the assembler's own
// serialize member, as raw bytes.
struct ByteArchive {
    std::vector<unsigned char> bytes;
    template <typename T>
    ByteArchive& operator&(const T& value) {
        const auto* first = reinterpret_cast<const unsigned char*>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(T));
        return *this;
    }
};

static std::vector<unsigned char> Serialized(PrimitiveAssembler& assembler) {
    ByteArchive archive;
    boost::serialization::access::serialize(archive, assembler, 0U);
    return archive.bytes;
}

struct Outcome {
    std::vector<unsigned char> triangles, state;
    bool operator==(const Outcome&) const = default;
};

enum class Order { JoinFirst, Overlap, AdoptAfter };

static Outcome Run(Order order, u32 serial_vertices, u32 seed) {
    PrimitiveAssembler assembler{Topology::List};
    // Claude: Earlier serial work left some pair in the buffer.
    assembler.AdoptCompletedTriangleTail({Value(900, seed), Value(901, seed)});
    const std::array<OutputVertex, 2> packet_tail{Value(500, seed), Value(501, seed)};
    Outcome outcome;
    const auto handler = [&](const OutputVertex& a, const OutputVertex& b, const OutputVertex& c) {
        for (const auto* v : {&a, &b, &c}) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(v);
            outcome.triangles.insert(outcome.triangles.end(), bytes, bytes + sizeof(OutputVertex));
        }
    };
    if (order == Order::JoinFirst)
        assembler.AdoptCompletedTriangleTail(packet_tail);
    for (u32 i = 0; i < serial_vertices; ++i)
        assembler.SubmitVertex(Value(i, seed + 1), handler);
    if (order == Order::AdoptAfter && assembler.IsEmpty())
        assembler.AdoptCompletedTriangleTail(packet_tail);
    outcome.state = Serialized(assembler);
    return outcome;
}

int main() {
    u32 cases{}, detected_single{}, detected_late{};
    for (u32 seed = 0; seed < 16; ++seed) {
        for (u32 count = 1; count <= 40; ++count) {
            const auto reference = Run(Order::JoinFirst, count, seed);
            const auto overlap = Run(Order::Overlap, count, seed);
            if (count >= 2) {
                Require(overlap == reference, "overlapped List draw differs from joined order");
                ++cases;
            } else if (!(overlap == reference)) {
                ++detected_single; // Claude: Why the rule requires two vertices.
            }
            if (count >= 2 && !(Run(Order::AdoptAfter, count, seed) == reference))
                ++detected_late; // Claude: Why superseded tails are never adopted.
        }
    }
    Require(detected_single == 16, "single-vertex defect not detected");
    Require(detected_late > 0, "late adoption defect not detected");
    std::cout << "overlap tail PASS: " << cases << " List draws match joined order; "
              << detected_single << " single-vertex and " << detected_late
              << " late-adoption defects detected\n";
}
