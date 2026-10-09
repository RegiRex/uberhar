// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <span>
#include <vector>
#include "common/uberhar_parallel_work.h"
#include "video_core/pica/uberhar_vertex_output.h"

namespace Pica {

// CodexAstraLocal: Plan the inherited 64-slot FIFO on the owner, shade only its
// misses concurrently, then submit every input in original order. A cache entry
// overwritten later in a chunk cannot invalidate an earlier hit: old payloads
// remain immutable until all submissions finish. No transformed values are reused
// across draws, and no worker enters the assembler, rasterizer or GPU command interface.
class NativeParallelBatch {
public:
    struct Invocation {
        u32 vertex{}, index{};
    };
    struct Work {
        u64 owner_invocations{}, worker_invocations{}, chunks{};
        unsigned peak_threads{};
    };
    static constexpr u32 Grain = 64;

    explicit NativeParallelBatch(unsigned processors = Common::Uberhar::AvailableProcessors(),
                                 void (*worker_start)() = nullptr)
        : pool{processors, worker_start} {}

    unsigned Available() const { return pool.Available(); }
    unsigned CreatedWorkers() const { return pool.CreatedWorkers(); }
    unsigned StartupFailures() const { return pool.StartupFailures(); }
    const Work& LastWork() const { return work; }

    bool Prepare(u32 inputs) {
        // CodexAstraLocal: Bound scratch storage by an actual draw and a modest
        // per-processor grain budget, while allowing hosts wider than eight cores.
        // All growth finishes before any worker starts or triangle is submitted.
        const u64 target = std::max<u64>(4096, static_cast<u64>(Available()) * Grain * 4);
        const auto wanted = static_cast<u32>(std::min<u64>(
            inputs, std::min<u64>(target, std::numeric_limits<u32>::max() - VertexCacheIndex::Capacity)));
        if (wanted == 0)
            return false;
        try {
            if (invocations.size() < wanted) invocations.resize(wanted);
            if (results.size() < wanted) results.resize(wanted);
            if (references.size() < wanted) references.resize(wanted);
        } catch (const std::bad_alloc&) {
            return false;
        }
        capacity = wanted;
        return true;
    }

    template <typename VertexAt, typename Shade, typename Submit>
    NativeVertexCounts Run(u32 count, bool indexed, VertexAt&& vertex_at,
                           Shade&& shade, Submit&& submit) {
        assert(capacity != 0 || count == 0);
        VertexCacheIndex fifo;
        std::array<OutputVertex, VertexCacheIndex::Capacity> previous;
        NativeVertexCounts counts;
        work = {};
        for (u64 begin = 0; begin < count; begin += capacity) {
            const auto size = static_cast<u32>(std::min<u64>(capacity, count - begin));
            std::array<u32, VertexCacheIndex::Capacity> cache_sources;
            // CodexAstraLocal: Unindexed inputs never consult FIFO payloads, so
            // avoid initializing an unused slot map on every unindexed chunk.
            if (indexed) {
                for (u32 slot = 0; slot < cache_sources.size(); ++slot)
                    cache_sources[slot] = capacity + slot;
            }
            u32 misses{};
            for (u32 offset = 0; offset < size; ++offset) {
                const auto input = static_cast<u32>(begin + offset);
                const u32 vertex = vertex_at(input);
                const int slot = indexed ? fifo.Find(static_cast<u16>(vertex)) : -1;
                if (slot >= 0) {
                    references[offset] = cache_sources[slot];
                    ++counts.hits;
                } else {
                    invocations[misses] = {vertex, input};
                    references[offset] = misses;
                    if (indexed)
                        cache_sources[fifo.Insert(static_cast<u16>(vertex))] = misses;
                    ++misses;
                }
            }
            // CodexAstraLocal: The certificate permits a fresh draw-initial
            // ShaderUnit per grain. Shade sees only immutable invocation IDs and
            // a disjoint result span; borrowed data cannot outlive this join.
            // CodexAstraLocal: Submitted indices can mostly hit the FIFO. Real
            // host replay found worker wakeups outweighed savings for 142/168
            // misses; keep fewer than 256 actual invocations on the owner.
            const u32 grain = misses < 256 ? std::max(1U, misses) : Grain;
            const auto completed = pool.Run(misses, grain, [&](u32 first, u32 last) {
                shade(std::span<const Invocation>{invocations}.subspan(first, last - first),
                      std::span<OutputVertex>{results}.subspan(first, last - first));
            });
            counts.invocations += misses;
            work.owner_invocations += completed.owner_items;
            work.worker_invocations += completed.worker_items;
            work.peak_threads = std::max(work.peak_threads, completed.working_threads);
            ++work.chunks;
            for (u32 offset = 0; offset < size; ++offset) {
                const u32 source = references[offset];
                submit(source < capacity ? results[source] : previous[source - capacity]);
            }
            // CodexAstraLocal: Carry exactly the FIFO's final payloads across
            // storage chunks, without re-shading hits or resetting primitive tails.
            // The final chunk has no consumer for another payload copy.
            if (indexed && begin + size < count) {
                for (u32 slot = 0; slot < cache_sources.size(); ++slot) {
                    if (cache_sources[slot] < capacity)
                        previous[slot] = results[cache_sources[slot]];
                }
            }
        }
        return counts;
    }

private:
    Common::Uberhar::ParallelWork pool;
    std::vector<Invocation> invocations;
    std::vector<OutputVertex> results;
    std::vector<u32> references;
    u32 capacity{};
    Work work;
};
} // namespace Pica
