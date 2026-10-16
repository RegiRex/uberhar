// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono> // CodexAstraLocal: Sparse owner-side phase clocks, never per vertex.
#include <limits>
#include <span>
#include <vector>
#include "common/uberhar_parallel_work.h"
#include "common/scope_exit.h" // CodexAstraLocal: Roll back unpublished owner append on failure.
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
        // CodexAstraLocal: Count useful completed prefixes, not requested workers.
        u64 prefix_chunks{}, prefix_inputs{}, prefix_ns{};
        unsigned peak_threads{};
        // CodexAstraLocal: These totals cover only explicitly sampled draws.
        // Owner/worker invocation totals above continue to cover every job.
        u64 sampled_inputs{}, sampled_invocations{}, sampled_chunks{};
        u64 plan_ns{}, pool_ns{}, owner_process_ns{}, join_ns{}, submit_ns{};
    };
    static constexpr u32 Grain = 64;

    explicit NativeParallelBatch(unsigned processors = Common::Uberhar::AvailableProcessors(),
                                 void (*worker_start)() = nullptr,
                                 bool adaptive = true, bool diagnostics = true)
        // CodexAstraLocal: Explicit title-frozen policy reaches the real worker.
        : pool{processors, worker_start, Common::Uberhar::AuxiliaryBudget::Global(),
               nullptr, adaptive, diagnostics} {}

    unsigned Available() const { return pool.Available(); }
    unsigned CreatedWorkers() const { return pool.CreatedWorkers(); }
    unsigned StartupFailures() const { return pool.StartupFailures(); }
    // CodexAstraLocal: Expose the selected sleeping mechanism at the ordinary log cadence.
    const char* WaitTransport() const { return pool.WaitTransport(); }
    // CodexAstraLocal TEST-ONLY LOG: Owner-cadence scheduling evidence; remove
    // this diagnostic accessor after adaptive participation is qualified.
    auto Scheduling() const { return pool.Scheduling(); }
    // CodexAstraLocal: A trial follows an exact shader proof, not just its FIFO
    // miss count; unrelated draws still receive their static helper ceiling.
    void SetWorkIdentity(u64 identity) { pool.SetWorkIdentity(identity); }
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
                           Shade&& shade, Submit&& submit, bool measure = false,
                           bool overlap = true) {
        assert(capacity != 0 || count == 0);
        VertexCacheIndex fifo;
        std::array<OutputVertex, VertexCacheIndex::Capacity> previous;
        NativeVertexCounts counts;
        work = {};
        for (u64 begin = 0; begin < count; begin += capacity) {
            // CodexAstraLocal: Conditional clocks partition this owner's sampled
            // chunk wall time; they are not a worker CPU or full-frame budget.
            using Clock = std::chrono::steady_clock;
            const auto plan_start = measure ? Clock::now() : Clock::time_point{};
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
            const auto pool_start = measure ? Clock::now() : Clock::time_point{};
            // CodexAstraLocal: Only an explicitly reversible append sink opts
            // into overlap. Scalar/ordinary callbacks retain the original path.
            constexpr bool reversible = requires {
                submit.Begin(size); submit.Rollback(); submit.Commit();
            };
            const auto get = [&](u32 offset) noexcept -> const OutputVertex& {
                const u32 source = references[offset];
                return source < capacity ? results[source] : previous[source - capacity];
            };
            u32 submitted{};
            bool transaction = false;
            SCOPE_EXIT({
                if constexpr (reversible) {
                    if (transaction) submit.Rollback();
                }
            });
            const auto shade_range = [&](u32 first, u32 last) {
                shade(std::span<const Invocation>{invocations}.subspan(first, last - first),
                      std::span<OutputVertex>{results}.subspan(first, last - first));
            };
            Common::Uberhar::ParallelWork::Result completed;
            const auto execute = [&] {
                if constexpr (reversible) {
                    // CodexAstraLocal: OFF uses the unchanged joined worker path,
                    // with no readiness array or speculative assembler transaction.
                    if (overlap && misses >= 256) {
                        const u32 groups = (misses + Grain - 1) / Grain;
                        return pool.RunWithOwnerHook(misses, grain,
                            [&](u32 first, u32 last) {
                                shade_range(first, last);
                                // CodexAstraLocal: Setup precedes publication;
                                // a partial or failed grain is never readable.
                                if (transaction)
                                    ready[first / Grain].store(true, std::memory_order_release);
                            }, [&] {
                                // CodexAstraLocal: This runs only after actual
                                // workers exist, before publication. Failed setup
                                // uses the ordinary joined callback, with no replay.
                                try {
                                    if (ready_slots < groups) {
                                        auto replacement = std::make_unique<std::atomic<bool>[]>(groups);
                                        ready = std::move(replacement);
                                        ready_slots = groups;
                                    }
                                } catch (const std::bad_alloc&) { return false; }
                                if (!submit.Begin(size)) return false;
                                for (u32 i = 0; i < groups; ++i)
                                    ready[i].store(false, std::memory_order_relaxed);
                                transaction = true;
                                return true;
                            }, [&] {
                                // CodexAstraLocal: One acquire scan stops at the
                                // first unfinished grain. Its original input index
                                // also safely bounds FIFO hits before that grain.
                                u32 done{};
                                while (done < groups && ready[done].load(std::memory_order_acquire))
                                    ++done;
                                submitted = done == groups ? size :
                                    invocations[done * Grain].index - static_cast<u32>(begin);
                                if (submitted) {
                                    submit(submitted, get);
                                    ++work.prefix_chunks;
                                    work.prefix_inputs += submitted;
                                }
                            }, measure);
                    }
                }
                return pool.Run(misses, grain, shade_range, measure);
            };
            completed = execute();
            const auto submit_start = measure ? Clock::now() : Clock::time_point{};
            counts.invocations += misses;
            work.owner_invocations += completed.owner_items;
            work.worker_invocations += completed.worker_items;
            work.peak_threads = std::max(work.peak_threads, completed.working_threads);
            ++work.chunks;
            // CodexAstraLocal: One immutable result generation remains alive
            // through ordered submission. Qualified sinks consume a whole range;
            // ordinary callable sinks retain scalar delivery and ownership.
            if constexpr (requires { submit(size, get); }) {
                if (submitted < size)
                    submit(size - submitted, [&](u32 offset) noexcept -> const OutputVertex& {
                        return get(submitted + offset);
                    });
            } else {
                for (u32 offset = 0; offset < size; ++offset)
                    submit(get(offset));
            }
            if constexpr (reversible) {
                if (transaction) { submit.Commit(); transaction = false; }
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
            // CodexAstraLocal: Include between-chunk FIFO payload copies in ordered
            // submission work and expose exact sample sizes beside durations.
            if (measure) {
                const auto submit_end = Clock::now();
                const auto ns = [](auto duration) -> u64 {
                    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
                };
                work.sampled_inputs += size;
                work.sampled_invocations += misses;
                ++work.sampled_chunks;
                work.plan_ns += ns(pool_start - plan_start);
                work.pool_ns += ns(submit_start - pool_start);
                work.owner_process_ns += completed.owner_process_ns;
                work.join_ns += completed.join_ns;
                work.prefix_ns += completed.owner_hook_ns;
                work.submit_ns += ns(submit_end - submit_start);
            }
        }
        return counts;
    }

private:
    Common::Uberhar::ParallelWork pool;
    std::vector<Invocation> invocations;
    std::vector<OutputVertex> results;
    std::vector<u32> references;
    // CodexAstraLocal: Bounded by this chunk's misses; no readiness allocation
    // for scalar sinks, small jobs or unsuccessful worker startup.
    std::unique_ptr<std::atomic<bool>[]> ready;
    u32 ready_slots{};
    u32 capacity{};
    Work work;
};
} // namespace Pica
