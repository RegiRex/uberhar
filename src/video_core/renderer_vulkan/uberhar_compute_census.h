// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <array>
#include <cstddef>
#include <limits>
#include "common/common_types.h"

namespace Vulkan {
// CodexAstraLocal: Count only existing admission masks and submitted CPU vertex
// counts. Six vertices means two triangles, never rectangle/TEV correctness.
// CodexAstraLocal: Admitted/rejected fields here describe only raw mask zero/
// nonzero. The renderer's effective admission can differ after a new state proof.
class ComputeStateCensus {
public:
    static constexpr unsigned ReasonBits = 11;
    static constexpr std::size_t Bins = 1U << ReasonBits, TopGroups = 4;
    struct Counts { u64 six{}, other{}; };
    struct Group { u32 mask{}; Counts counts{}; u64 draws{}; };
    struct Snapshot {
        Counts total{}, admitted{}, invalid{}, ranked{}, unranked{};
        std::array<u64, ReasonBits> marginal{};
        std::array<u64, 2> state_only_six{};
        std::array<Group, TopGroups> top{};
        u64 draws{}, rejected{}, distinct{}, admitted_draws{}, ranked_draws{}, unranked_draws{}, invalid_draws{};
        unsigned groups{};
        bool overflow{};
    };

    // CodexAstraLocal: One bounded bucket update replaces the eleven-bit marginal scan
    // per rejected draw; invalid future masks stay explicit instead of aliasing.
    void Record(u32 mask, std::size_t vertices) noexcept {
        auto& counts = mask < Bins ? bins[mask] : invalid;
        auto& count = vertices == 6 ? counts.six : counts.other;
        if (count == std::numeric_limits<u64>::max()) overflow = true;
        else ++count;
    }

    // CodexAstraLocal: Saturation marks diagnostic conservation invalid rather
    // than wrapping a counter; it never changes rendering or admission.
    static void Add(u64& target, u64 value, bool& overflowed) noexcept {
        if (value > std::numeric_limits<u64>::max() - target) {
            target = std::numeric_limits<u64>::max();
            overflowed = true;
        } else target += value;
    }

    // CodexAstraLocal: Consume one interval without a second histogram. Ranking,
    // lifetime marginals and exact candidate upper bounds use all 2048 bins only
    // at an existing report boundary; no allocation, payload or hashing is needed.
    Snapshot Consume(const std::array<u32, 2>& allowed_masks) noexcept {
        Snapshot out{};
        out.overflow = overflow;
        out.invalid = invalid;
        out.total = invalid;
        invalid = {};
        overflow = false;
        for (u32 mask = 0; mask < Bins; ++mask) {
            const Counts counts = bins[mask];
            bins[mask] = {};
            u64 draws = counts.six;
            Add(draws, counts.other, out.overflow);
            if (!draws) continue;
            ++out.distinct;
            Add(out.total.six, counts.six, out.overflow);
            Add(out.total.other, counts.other, out.overflow);
            if (mask == 0) out.admitted = counts;
            for (unsigned bit = 0; bit < ReasonBits; ++bit)
                if (mask & (1U << bit)) Add(out.marginal[bit], draws, out.overflow);
            for (std::size_t i = 0; i < allowed_masks.size(); ++i)
                if ((mask & ~allowed_masks[i]) == 0)
                    Add(out.state_only_six[i], counts.six, out.overflow);
            // CodexAstraLocal: Descending population and ascending mask resolve
            // ties deterministically. Omitted populations remain exact counts.
            unsigned position = 0;
            while (position < out.groups &&
                   (out.top[position].draws > draws ||
                    (out.top[position].draws == draws && out.top[position].mask < mask)))
                ++position;
            if (position < TopGroups) {
                for (std::size_t i = TopGroups - 1; i > position; --i) out.top[i] = out.top[i - 1];
                out.top[position] = {mask, counts, draws};
                if (out.groups < TopGroups) ++out.groups;
            }
        }
        out.draws = out.total.six;
        Add(out.draws, out.total.other, out.overflow);
        out.admitted_draws = out.admitted.six;
        Add(out.admitted_draws, out.admitted.other, out.overflow);
        out.rejected = out.draws - out.admitted_draws;
        for (unsigned i = 0; i < out.groups; ++i) {
            Add(out.ranked.six, out.top[i].counts.six, out.overflow);
            Add(out.ranked.other, out.top[i].counts.other, out.overflow);
        }
        out.unranked = {out.total.six - out.ranked.six, out.total.other - out.ranked.other};
        // CodexAstraLocal: Derived totals saturate too. An overflowed report is
        // explicitly invalid evidence, never a wrapped small population.
        out.ranked_draws = out.ranked.six;
        Add(out.ranked_draws, out.ranked.other, out.overflow);
        out.unranked_draws = out.unranked.six;
        Add(out.unranked_draws, out.unranked.other, out.overflow);
        out.invalid_draws = out.invalid.six;
        Add(out.invalid_draws, out.invalid.other, out.overflow);
        return out;
    }

private:
    std::array<Counts, Bins> bins{};
    Counts invalid{};
    bool overflow{};
};
// CodexAstraLocal: Fix the intended metadata footprint independently of future
// renderer growth; report snapshots are small stack values, never another bank.
static_assert(sizeof(ComputeStateCensus) <= 32 * 1024 + 32);
static_assert(sizeof(ComputeStateCensus::Snapshot) <= 384);
} // namespace Vulkan
