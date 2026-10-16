// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array> // CodexAstraLocal: Fixed exact adaptive histories, no per-job allocation.
#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#if defined(__linux__)
#include <sched.h>
#endif

namespace Common::Uberhar {

// CodexAstraLocal: Discover the process allowance once per pool/budget lifetime;
// this is a capacity bound, not a claim that these logical CPUs are idle or equal.
inline unsigned AvailableProcessors() {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
        const int count = CPU_COUNT(&allowed);
        if (count > 0) return static_cast<unsigned>(count);
    }
#endif
    return std::max(1U, std::thread::hardware_concurrency());
}

// CodexAstraLocal: Bound executing auxiliaries across participating pools while
// leaving the emulation owner unblocked. Sleeping threads and queued work hold
// no execution token. Other emulator/compiler/driver threads are outside this
// budget; it neither pins CPUs nor purports to control total process occupancy.
class AuxiliaryBudget {
public:
    class Permit {
    public:
        Permit() = default;
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& other) noexcept
            : budget{std::exchange(other.budget, nullptr)}, progress{other.progress} {}
        ~Permit() {
            if (!budget) return;
            --held_tokens;
            if (budget->execution_census) budget->executing.fetch_sub(1, std::memory_order_relaxed);
            if (!progress) budget->state.fetch_sub(1, std::memory_order_release);
        }
        explicit operator bool() const { return budget != nullptr; }
    private:
        friend class AuxiliaryBudget;
        Permit(AuxiliaryBudget* owner, bool is_progress) : budget{owner}, progress{is_progress} {
            ++held_tokens;
            // CodexAstraLocal: Admission/nesting retain their real state contract.
            // The optional qualification census alone pays shared peak atomics.
            if (budget->execution_census) {
                const auto active = budget->executing.fetch_add(1, std::memory_order_relaxed) + 1;
                auto peak = budget->peak.load(std::memory_order_relaxed);
                while (active > peak && !budget->peak.compare_exchange_weak(peak, active,
                    std::memory_order_relaxed, std::memory_order_relaxed)) {}
            }
        }
        AuxiliaryBudget* budget{};
        bool progress{};
    };

    // CodexAstraLocal: An asynchronous coordinator reserves capacity, not an
    // active token, so it can always drain already published packets. Its sole
    // owner enters only around CPU processing, never queue sleep or pool join.
    // If no lane can be reserved, callers must keep their synchronous path.
    class ProgressLane {
    public:
        ProgressLane() = default;
        ProgressLane(const ProgressLane&) = delete;
        ProgressLane& operator=(const ProgressLane&) = delete;
        ProgressLane(ProgressLane&& other) noexcept : budget{std::exchange(other.budget, nullptr)} {}
        ~ProgressLane() {
            if (budget) budget->state.fetch_sub(Reservation, std::memory_order_release);
        }
        explicit operator bool() const { return budget != nullptr; }
        Permit Enter() {
            if (!budget || HasPermit()) return {};
            return Permit{budget, true};
        }
    private:
        friend class AuxiliaryBudget;
        explicit ProgressLane(AuxiliaryBudget* owner) : budget{owner} {}
        AuxiliaryBudget* budget{};
    };

    // CodexAstraLocal: Default production work avoids diagnostic-only shared
    // atomics; explicit host qualification can retain the exact execution census.
    explicit AuxiliaryBudget(unsigned processors, bool census = false)
        : capacity{processors > 1 ? processors - 1 : 0}, execution_census{census} {}
    AuxiliaryBudget(const AuxiliaryBudget&) = delete;
    AuxiliaryBudget& operator=(const AuxiliaryBudget&) = delete;
    static AuxiliaryBudget& Global() {
        static AuxiliaryBudget budget{AvailableProcessors()};
        return budget;
    }
    static bool HasPermit() { return held_tokens != 0; }
    // CodexAstraLocal: The existing owner report cadence can distinguish actual
    // permit use from capacity and sleeping reservations. These independently
    // atomic gauges are diagnostic snapshots, not one transactional census.
    struct Statistics {
        unsigned capacity{}, active{}, peak{}, reservations{};
        std::uint64_t denials{};
        bool execution_census{};
    };
    Statistics GetStatistics() const {
        return {capacity, Active(), peak.load(std::memory_order_relaxed),
            static_cast<unsigned>(state.load(std::memory_order_acquire) >> 32),
            denials.load(std::memory_order_relaxed), execution_census};
    }
    unsigned HelperCapacity() const {
        return capacity - static_cast<unsigned>(state.load(std::memory_order_acquire) >> 32);
    }
    unsigned Active() const {
        return executing.load(std::memory_order_relaxed);
    }
    Permit TryAcquire() {
        // CodexAstraLocal: Nested work executes within its existing token. No
        // thread waits for a permit or keeps one while joining a nested pool.
        if (HasPermit()) return {};
        auto observed = state.load(std::memory_order_relaxed);
        while (static_cast<unsigned>(observed) + (observed >> 32) < capacity) {
            if (state.compare_exchange_weak(observed, observed + 1,
                    std::memory_order_acquire, std::memory_order_relaxed))
                return Permit{this, false};
        }
        denials.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    ProgressLane ReserveProgress() {
        auto observed = state.load(std::memory_order_relaxed);
        while (static_cast<unsigned>(observed) + (observed >> 32) < capacity) {
            if (state.compare_exchange_weak(observed, observed + Reservation,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
                return ProgressLane{this};
        }
        return {};
    }
private:
    static constexpr std::uint64_t Reservation = std::uint64_t{1} << 32;
    const unsigned capacity;
    const bool execution_census;
    // CodexAstraLocal: One CAS covers helper admission and coordinator capacity
    // registration, closing the registration/admission oversubscription race.
    std::atomic<std::uint64_t> state{};
    std::atomic<unsigned> executing{}, peak{};
    std::atomic<std::uint64_t> denials{};
    inline static thread_local unsigned held_tokens{};
};

// CodexAstraLocal: Adapt each pool's useful participation, independently of the
// global hard capacity bound. Only sampled complete CPU-work envelopes enter
// this integer-only controller; pacing and GPU/dependency waits are not inputs.
// Comparable shape, owner cost and hook work gate each stable window. This is a
// conservative local trial, not a measurement of available physical CPU power.
class ParticipationWindow {
public:
    struct Sample {
        std::uint32_t count{}, grain{}, owner_items{}, worker_items{};
        unsigned helpers{};
        std::uint64_t total_ns{}, owner_ns{}, join_ns{}, hook_ns{};
        bool denied{}, failed{};
    };
    explicit ParticipationWindow(unsigned maximum = 0) : maximum{maximum}, desired{maximum} {}
    unsigned Desired() const { return desired; }
    // CodexAstraLocal: Snapshot only from the pool owner at its existing report
    // cadence. The counters make conservative non-admission distinguishable
    // from an actual trial/accepted change; no hot-path formatting is added.
    struct Statistics {
        unsigned desired{};
        std::uint64_t samples{}, rejected{}, trials{}, accepted{}, reverted{};
    };
    Statistics GetStatistics() const {
        return {desired, samples, rejected, trials, accepted, reverted};
    }
    // CodexAstraLocal: Registration can change the shared helper capacity after
    // this pool was constructed. Compare trials only under one capacity bound.
    void Limit(unsigned limit) {
        if (maximum == limit) return;
        Reset(); maximum = limit; desired = limit;
        shape_count = shape_grain = 0;
    }
    // CodexAstraLocal: Check shape at every task boundary, without a clock. A
    // trial for one workload must not cap intervening unrelated jobs or survive
    // a different shape merely because that job missed the sampling cadence.
    void Prepare(std::uint32_t count, std::uint32_t grain, bool hook = false) {
        grain = std::max(1U, grain);
        if (shape_count == count && shape_grain == grain && shape_hook == hook) return;
        Reset(); shape_count = count; shape_grain = grain; shape_hook = hook;
        desired = std::min(maximum, count / grain > 0 ? count / grain - 1 : 0);
    }
    bool SelectSample(std::uint32_t count, std::uint32_t grain) {
        // CodexAstraLocal: Tiny/serial-by-shape calls read no clocks and do not
        // advance the sampling cadence. At most one eligible call in 64 samples.
        return maximum && count / std::max(1U, grain) >= 2 && (++cadence % 64 == 0);
    }
    void Observe(const Sample& sample) {
        ++samples;
        if (sample.denied || sample.failed || !sample.grain || sample.count / sample.grain < 2 ||
            sample.owner_items == 0 ||
            sample.owner_items + std::uint64_t{sample.worker_items} != sample.count ||
            sample.total_ns < 50'000 || sample.total_ns > 1'000'000'000 ||
            sample.owner_ns < 5'000 || (sample.helpers && !sample.worker_items)) {
            RejectWindow();
            return;
        }
        const auto ceiling = std::min(maximum, sample.count / std::max(1U, sample.grain) - 1);
        if (shape_count != sample.count || shape_grain != sample.grain) {
            Prepare(sample.count, sample.grain, shape_hook);
        }
        if (sample.helpers != desired) { RejectWindow(); return; }
        const auto cost = sample.owner_ns * 1024 / sample.owner_items;
        const bool hook = sample.hook_ns != 0;
        if (window.samples && (!Near(cost, window.cost / window.samples) ||
            hook != window.hook || (hook && !Near(sample.hook_ns, window.hook_ns / window.samples)))) {
            RejectWindow();
            return;
        }
        window.hook = hook;
        window.cost += cost;
        window.total_ns += sample.total_ns;
        window.join_ns += sample.join_ns;
        window.hook_ns += sample.hook_ns;
        if (++window.samples != 8) return;
        const Window completed = std::exchange(window, {});
        if (trial) {
            const bool comparable = Near(completed.cost, baseline.cost) &&
                completed.hook == baseline.hook &&
                (!completed.hook || Near(completed.hook_ns, baseline.hook_ns));
            // CodexAstraLocal: A lower cap may keep essentially equal latency;
            // an increase must improve complete useful work by at least 5%.
            // A worsening join without that gain also rolls back the trial.
            const bool improved = completed.total_ns * 100 <= baseline.total_ns * 95;
            const bool keep_lower = desired < prior &&
                completed.total_ns * 100 <= baseline.total_ns * 102;
            const bool bad_join = completed.join_ns > baseline.join_ns + baseline.total_ns / 10;
            if (!comparable || (!improved && (!keep_lower || bad_join))) {
                desired = prior;
                direction = -direction;
                ++reverted;
            } else ++accepted;
            trial = false;
            baseline = {};
            cooldown = 2;
            return;
        }
        if (cooldown) { --cooldown; return; }
        baseline = completed;
        prior = desired;
        if ((direction < 0 && desired == 0) || (direction > 0 && desired >= ceiling))
            direction = -direction;
        if (direction < 0 && desired) --desired;
        else if (direction > 0 && desired < ceiling) ++desired;
        trial = desired != prior;
        trials += trial;
    }
private:
    struct Window {
        unsigned samples{};
        std::uint64_t cost{}, total_ns{}, join_ns{}, hook_ns{};
        bool hook{};
    };
    static bool Near(std::uint64_t a, std::uint64_t b) {
        return a && b && a * 4 >= b * 3 && b * 4 >= a * 3;
    }
    void RejectWindow() {
        ++rejected;
        window = {};
        if (trial) { desired = prior; trial = false; baseline = {}; cooldown = 2; ++reverted; }
    }
    void Reset() {
        window = {};
        if (trial) { desired = prior; trial = false; ++reverted; }
        baseline = {}; cooldown = 0; direction = -1;
    }
    unsigned maximum;
    unsigned desired{}, prior{}, cadence{}, cooldown{};
    std::uint32_t shape_count{}, shape_grain{};
    bool shape_hook{};
    int direction{-1};
    bool trial{};
    std::uint64_t samples{}, rejected{}, trials{}, accepted{}, reverted{};
    Window window{}, baseline{};
};

// CodexAstraLocal: Preserve four exact work/shape windows across intervening
// jobs instead of resetting every unsampled shape switch. Identity is supplied
// by the work owner, never a lossy shader hash. Unknown/mixed work keeps static
// participation. Capacity changes invalidate every retained trial together.
class AdaptiveParticipation {
public:
    using Sample = ParticipationWindow::Sample;
    struct Statistics : ParticipationWindow::Statistics {
        std::uint64_t history_hits{}, history_misses{}, evictions{}, capacity_resets{}, unkeyed{};
        std::uint64_t rejected_invalid{}, rejected_cost{}, rejected_helpers{};
    };
    explicit AdaptiveParticipation(unsigned maximum) : maximum{maximum}, fallback{maximum} {}
    unsigned Desired() const { return selected ? selected->window.Desired() : fallback; }
    void Limit(unsigned limit) {
        if (maximum == limit) return;
        // CodexAstraLocal: Retire counts before invalidation so a capacity reset
        // cannot erase evidence of a trial or apply its cap to another epoch.
        for (auto& entry : entries) if (entry.valid) Retire(entry);
        selected = nullptr; prepared_unkeyed = false;
        maximum = fallback = limit; ++totals.capacity_resets;
    }
    void Prepare(std::uint32_t count, std::uint32_t grain, bool hook = false,
                 std::uint64_t identity = 1) {
        grain = std::max(1U, grain);
        fallback = std::min(maximum, count / grain > 0 ? count / grain - 1 : 0);
        const Key key{identity, count, grain, hook};
        prepared_unkeyed = identity == 0;
        if (!identity) { selected = nullptr; ++totals.unkeyed; return; }
        if (selected && selected->key == key) return;
        for (auto& entry : entries) if (entry.valid && entry.key == key) {
            selected = &entry; ++totals.history_hits; return;
        }
        auto& entry = entries[next++ % entries.size()];
        if (entry.valid) { Retire(entry); ++totals.evictions; }
        entry.key = key; entry.valid = true; entry.window = ParticipationWindow{maximum};
        entry.window.Prepare(count, grain, hook); selected = &entry; ++totals.history_misses;
    }
    bool SelectSample(std::uint32_t count, std::uint32_t grain) {
        // CodexAstraLocal: Unprepared standalone callers retain the old public
        // cadence contract; production always prepares its exact identity first.
        if (!selected && !prepared_unkeyed) Prepare(count, grain);
        return selected && selected->window.SelectSample(count, grain);
    }
    void Observe(const Sample& sample) {
        if (prepared_unkeyed) return;
        if (!selected) Prepare(sample.count, sample.grain);
        if (!selected) return;
        if (!sample.failed && sample.grain &&
            (selected->key.count != sample.count || selected->key.grain != sample.grain))
            Prepare(sample.count, sample.grain, selected->key.hook, selected->key.identity);
        const auto before = selected->window.GetStatistics();
        selected->window.Observe(sample);
        const auto after = selected->window.GetStatistics();
        if (after.rejected != before.rejected) {
            if (sample.denied || sample.failed || !sample.grain ||
                sample.count / sample.grain < 2 || !sample.owner_items ||
                sample.owner_items + std::uint64_t{sample.worker_items} != sample.count ||
                sample.total_ns < 50'000 || sample.total_ns > 1'000'000'000 ||
                sample.owner_ns < 5'000 || (sample.helpers && !sample.worker_items))
                ++totals.rejected_invalid;
            else if (sample.helpers != before.desired) ++totals.rejected_helpers;
            else ++totals.rejected_cost;
        }
    }
    Statistics GetStatistics() const {
        auto result = totals;
        for (const auto& entry : entries) if (entry.valid) Add(result, entry.window.GetStatistics());
        result.desired = Desired(); return result;
    }
private:
    struct Key {
        std::uint64_t identity{}; std::uint32_t count{}, grain{}; bool hook{};
        bool operator==(const Key&) const = default;
    };
    // CodexAstraLocal: Direct initialization admits the explicit default window
    // constructor on Clang/libc++ as well as GCC; every history starts empty.
    struct Entry { Key key; ParticipationWindow window{}; bool valid{}; };
    static void Add(Statistics& target, const ParticipationWindow::Statistics& value) {
        target.samples += value.samples; target.rejected += value.rejected;
        target.trials += value.trials; target.accepted += value.accepted; target.reverted += value.reverted;
    }
    void Retire(Entry& entry) {
        // CodexAstraLocal: A discarded trial is a rollback, not an accepted cap.
        entry.window.Limit(0); Add(totals, entry.window.GetStatistics()); entry.valid = false;
    }
    unsigned maximum{}, fallback{};
    bool prepared_unkeyed{};
    std::array<Entry, 4> entries{};
    Entry* selected{};
    std::size_t next{};
    Statistics totals{};
};
} // namespace Common::Uberhar
