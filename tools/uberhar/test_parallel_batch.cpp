// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Compare the production parallel FIFO/ordered submission path
// with an independent deque oracle and the inherited production serial runner.
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_vertex_parallel_batch.h"

// CodexAstraLocal: Only the logging sink is adapted; execute the real assembler,
// serial FIFO and parallel scheduling code, turning unexpected errors into failure.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}
using namespace Pica;
using Topology = PipelineRegs::TriangleTopology;
using Triangle = std::array<OutputVertex, 3>;
static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static OutputVertex Value(u32 vertex, u32 input, u32 draw) {
    std::array<u32, 24> words;
    for (u32 i = 0; i < words.size(); ++i)
        words[i] = 0x3f000000U | ((vertex * 193U + input * 31U + draw * 7U + i) & 0x7fffffU);
    OutputVertex result;
    std::memcpy(&result, words.data(), sizeof(result));
    return result;
}

template <typename T>
static bool Same(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

int main() {
    u64 draws{}, vertices{};
    const auto owner = std::this_thread::get_id();
    for (unsigned cores : {1U, 2U, 6U, 8U, 12U, 17U}) {
        NativeParallelBatch batch{cores};
        Require(!batch.Prepare(0), "empty preparation");
        for (auto topology : {Topology::List, Topology::Strip, Topology::Fan, Topology::Shader}) {
            // CodexAstraLocal: Exercise both production callback shapes with every
            // core/FIFO/chunk/topology cohort; each keeps its own cross-draw tails.
            for (bool bulk : {false, true}) {
                PrimitiveAssembler actual_assembler{topology}, serial_assembler{topology};
                std::vector<Triangle> actual_triangles, serial_triangles;
                // CodexAstraLocal: Assemblers intentionally survive every draw here,
                // including partial list/strip/fan tails and pending Shader winding.
                for (bool indexed : {false, true}) {
                    for (u32 count : {1U, 63U, 64U, 65U, 127U, 128U, 255U, 256U,
                                      4095U, 4096U, 4097U, 8193U, 17011U}) {
                        for (u32 pattern = 0; pattern < 3; ++pattern) {
                            ++draws;
                            vertices += count;
                            const u32 draw = static_cast<u32>(draws);
                            std::vector<u32> indices(count);
                            for (u32 i = 0; i < count; ++i) {
                                indices[i] = !indexed ? i + 19 : pattern == 0 ? i % 53 :
                                    pattern == 1 ? (i % 97) * 129 : (i * 73 + i / 7) % 65536;
                            }
                            struct CacheItem { u32 key; OutputVertex output; };
                            std::deque<CacheItem> oracle_fifo;
                            std::vector<OutputVertex> expected, actual, serial;
                            std::vector<u32> expected_misses;
                            for (u32 i = 0; i < count; ++i) {
                                auto found = oracle_fifo.end();
                                if (indexed) found = std::find_if(oracle_fifo.begin(), oracle_fifo.end(),
                                    [&](const CacheItem& item) { return item.key == indices[i]; });
                                if (found != oracle_fifo.end()) expected.push_back(found->output);
                                else {
                                    const auto value = Value(indices[i], i, draw);
                                    expected.push_back(value);
                                    expected_misses.push_back(i);
                                    if (indexed) {
                                        if (oracle_fifo.size() == 64) oracle_fifo.pop_front();
                                        oracle_fifo.push_back({indices[i], value});
                                    }
                                }
                            }
                            std::vector<std::atomic<unsigned>> shaded(count);
                            Require(batch.Prepare(count), "scratch allocation");
                            if (topology == Topology::Shader && draw % 3 == 0) {
                                actual_assembler.SetWinding();
                                serial_assembler.SetWinding();
                            }
                            auto vertex_at = [&](u32 i) { return indices[i]; };
                            // CodexAstraLocal: Interleave observed and unobserved
                            // draws across FIFO/chunk/core cases; measuring must not
                            // change exact output, invocation order or primitive tails.
                            const bool measure = draw % 2 == 0;
                            const auto shade = [&](auto inputs, auto outputs) {
                                for (u32 i = 0; i < inputs.size(); ++i) {
                                    const auto invocation = inputs[i];
                                    Require(invocation.vertex == indices[invocation.index], "input identity");
                                    ++shaded[invocation.index];
                                    outputs[i] = Value(invocation.vertex, invocation.index, draw);
                                }
                            };
                            const auto submit = [&](const OutputVertex& output) {
                                Require(std::this_thread::get_id() == owner, "worker submitted triangle");
                                actual.push_back(output);
                                actual_assembler.SubmitVertex(output, [&](auto a, auto b, auto c) {
                                    actual_triangles.push_back({a, b, c});
                                });
                            };
                            // CodexAstraLocal: Inspect borrowed FIFO order separately
                            // from exact triangle/state comparisons so a bulk adapter
                            // cannot hide omitted inputs or reordered cache hits.
                            const auto submit_bulk = [&](u32 size, const auto& get) {
                                Require(std::this_thread::get_id() == owner, "worker submitted range");
                                for (u32 offset = 0; offset < size; ++offset)
                                    actual.push_back(get(offset));
                                actual_assembler.SubmitOrdered(size, get, [&](auto a, auto b, auto c) {
                                    actual_triangles.push_back({a, b, c});
                                });
                            };
                            const auto counts = bulk
                                ? batch.Run(count, indexed, vertex_at, shade, submit_bulk, measure)
                                : batch.Run(count, indexed, vertex_at, shade, submit, measure);
                            NativeVertexSamples samples;
                            auto inherited = RunNativeVertexBatch<false>(count, indexed, vertex_at,
                                [&]<bool>(u32 vertex, u32 index) { return Value(vertex, index, draw); },
                                [&](const OutputVertex& output) {
                                    serial.push_back(output);
                                    serial_assembler.SubmitVertex(output, [&](auto a, auto b, auto c) {
                                        serial_triangles.push_back({a, b, c});
                                    });
                                }, samples);
                            Require(Same(expected, actual) && Same(actual, serial), "ordered output/FIFO mismatch");
                            Require(counts.invocations == expected_misses.size() &&
                                    counts.invocations == inherited.invocations && counts.hits == inherited.hits &&
                                    counts.invocations + counts.hits == count, "FIFO accounting mismatch");
                            std::vector<u32> actual_misses;
                            for (u32 i = 0; i < count; ++i) {
                                Require(shaded[i] <= 1, "repeated shader invocation");
                                if (shaded[i]) actual_misses.push_back(i);
                            }
                            Require(actual_misses == expected_misses, "changed invocation population");
                            const auto& work = batch.LastWork();
                            Require(work.owner_invocations + work.worker_invocations == counts.invocations,
                                    "owner/worker work omitted");
                            // CodexAstraLocal: Validate sample populations directly
                            // and nested wall-clock bounds without asserting a host
                            // timing threshold or requiring nonzero timer resolution.
                            Require(work.sampled_inputs == (measure ? count : 0) &&
                                    work.sampled_invocations == (measure ? counts.invocations : 0) &&
                                    work.sampled_chunks == (measure ? work.chunks : 0),
                                    "sampled work population changed");
                            Require(work.owner_process_ns + work.join_ns <= work.pool_ns,
                                    "nested pool timing escaped outer bracket");
                            if (!measure)
                                Require(work.plan_ns == 0 && work.pool_ns == 0 &&
                                        work.owner_process_ns == 0 && work.join_ns == 0 &&
                                        work.submit_ns == 0, "unobserved draw recorded timings");
                            Require(Same(actual_triangles, serial_triangles), "primitive order/tail mismatch");
                            Require(actual_assembler.IsEmpty() == serial_assembler.IsEmpty() &&
                                    actual_assembler.HasPendingWinding() == serial_assembler.HasPendingWinding(),
                                    "persistent assembly state mismatch");
                            actual_triangles.clear();
                            serial_triangles.clear();
                        }
                    }
                }
            }
        }
    }
    std::cout << "parallel batch PASS: " << draws << " draws, " << vertices
              << " ordered inputs; exact FIFO, fresh draws, chunk boundaries and persistent assembly\n";
}
