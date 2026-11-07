// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array> // CodexAstraLocal: Fixed owner-only cohort storage, never a hot-path allocation.
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
    // CodexAstraLocal: First acquisition freezes optional census for the process.
    // System initialization supplies the requested value before pools exist;
    // later titles report requested/effective rather than racing live permits.
    static AuxiliaryBudget& Global(bool requested_census = false) {
        static AuxiliaryBudget budget{AvailableProcessors(), requested_census};
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
    // CodexAstraLocal: With census disabled this gauge is unavailable, not zero
    // execution. Admission relies solely on state, never this diagnostic value.
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
class AdaptiveWorkWindow {
public:
    struct Sample {
        std::uint32_t count{}, grain{}, owner_items{}, worker_items{};
        unsigned helpers{};
        std::uint64_t total_ns{}, owner_ns{}, join_ns{}, hook_ns{};
        bool denied{}, failed{};
    };
    AdaptiveWorkWindow() = default;
    explicit AdaptiveWorkWindow(unsigned maximum) : maximum{maximum}, desired{maximum} {}
    unsigned Desired() const { return desired; }
    // CodexAstraLocal: Snapshot only from the pool owner at its existing report
    // cadence. The counters make conservative non-admission distinguishable
    // from an actual trial/accepted change; no hot-path formatting is added.
    struct Statistics {
        unsigned desired{};
        std::uint64_t samples{}, rejected{}, trials{}, accepted{}, reverted{};
        // CodexAstraLocal TEST-ONLY LOG: Count unstable eligible task calls
        // excluded from sampling, not the number of clock readings avoided;
        // remove after stable-shape admission is qualified on gameplay.
        std::uint64_t unstable_skipped{};
        // CodexAstraLocal TEST-ONLY LOG: Infrequent sampled refusals are split
        // into contention/failure/shape/size/owner/helper/cost causes; remove
        // after cohort admission and rollback are qualified on gameplay.
        std::array<std::uint64_t, 7> reject_reasons{};
    };
    Statistics GetStatistics() const {
        return {desired, samples, rejected, trials, accepted, reverted, unstable_skipped, reject_reasons};
    }
    // CodexAstraLocal: Check shape at every task boundary, without a clock. A
    // trial for one workload must not cap intervening unrelated jobs or survive
    // a different shape merely because that job missed the sampling cadence.
    void Prepare(std::uint32_t count, std::uint32_t grain, bool hook = false) {
        grain = std::max(1U, grain);
        // CodexAstraLocal: A window is discarded on every shape change. Avoid
        // clocks and worker sample bookkeeping until repeated comparable work
        // exists; this gates measurement only, never workers or explicit timing.
        if (shape_count == count && shape_grain == grain && shape_hook == hook) {
            stable_shapes = std::min(StableShapeThreshold, stable_shapes + 1);
            return;
        }
        Reset(); shape_count = count; shape_grain = grain; shape_hook = hook;
        stable_shapes = 1;
        desired = std::min(maximum, count / grain > 0 ? count / grain - 1 : 0);
    }
    bool SelectSample(std::uint32_t count, std::uint32_t grain) {
        // CodexAstraLocal: Tiny/serial-by-shape calls read no clocks and do not
        // advance the sampling cadence. At most one eligible call in 64 samples.
        if (!maximum || count / std::max(1U, grain) < 2) return false;
        if (stable_shapes < StableShapeThreshold) {
            ++unstable_skipped;
            return false;
        }
        return ++cadence % 64 == 0;
    }
    void Observe(const Sample& sample) {
        ++samples;
        if (sample.denied) { RejectWindow(0); return; }
        if (sample.failed) { RejectWindow(1); return; }
        if (!sample.grain || sample.count / sample.grain < 2 ||
            sample.owner_items + std::uint64_t{sample.worker_items} != sample.count) {
            RejectWindow(2); return;
        }
        if (sample.total_ns < 50'000 || sample.total_ns > 1'000'000'000) {
            RejectWindow(3); return;
        }
        if (sample.owner_items == 0 || sample.owner_ns < 5'000 ||
            (sample.helpers && !sample.worker_items)) { RejectWindow(4); return; }
        const auto ceiling = std::min(maximum, sample.count / std::max(1U, sample.grain) - 1);
        if (shape_count != sample.count || shape_grain != sample.grain) {
            Prepare(sample.count, sample.grain, shape_hook);
        }
        if (sample.helpers != desired) { RejectWindow(5); return; }
        const auto cost = sample.owner_ns * 1024 / sample.owner_items;
        const bool hook = sample.hook_ns != 0;
        if (window.samples && (!Near(cost, window.cost / window.samples) ||
            hook != window.hook || (hook && !Near(sample.hook_ns, window.hook_ns / window.samples)))) {
            RejectWindow(6);
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
    void RejectWindow(unsigned reason) {
        ++rejected;
        ++reject_reasons[reason];
        window = {};
        if (trial) { desired = prior; trial = false; baseline = {}; cooldown = 2; ++reverted; }
    }
    void Reset() {
        window = {};
        if (trial) { desired = prior; trial = false; ++reverted; }
        baseline = {}; cooldown = 0; direction = -1;
    }
    unsigned maximum{};
    // CodexAstraLocal: Eight recurring visits qualify this exact-key window;
    // other cohorts no longer discard these visits. Timing stays sparse (1/64).
    static constexpr unsigned StableShapeThreshold = 8;
    unsigned stable_shapes{};
    unsigned desired{}, prior{}, cadence{}, cooldown{};
    std::uint32_t shape_count{}, shape_grain{};
    bool shape_hook{};
    int direction{-1};
    bool trial{};
    std::uint64_t samples{}, rejected{}, trials{}, accepted{}, reverted{}, unstable_skipped{};
    std::array<std::uint64_t, 7> reject_reasons{};
    Window window{}, baseline{};
};

// CodexAstraLocal: Callers supply an already available immutable work identity,
// not a guest pointer or a newly calculated shader hash. Unknown/mixed work stays
// on static admission. Equal-sized draws from different programs cannot share a
// trial merely because their vertex counts happen to match.
struct AdaptiveWorkIdentity {
    std::uint64_t program{}, operands{}, contract{};
    bool operator==(const AdaptiveWorkIdentity&) const = default;
    explicit operator bool() const { return program || operands || contract; }
};

// CodexAstraLocal: A small two-way table preserves independent exact-shape
// histories across interleaved draws. Only the joined owner touches it. Workers,
// memory ownership, admission permits and the cheap serial path are unchanged.
class AdaptiveParticipation {
public:
    using Sample = AdaptiveWorkWindow::Sample;
    struct Statistics : AdaptiveWorkWindow::Statistics {
        // CodexAstraLocal TEST-ONLY LOG: Owner-cadence admission attribution;
        // remove after recurring gameplay cohorts justify retaining adaptation.
        std::uint64_t cohort_hits{}, cohort_misses{}, cohort_evictions{}, cohort_expired{};
        std::uint64_t unknown_identity{}, capacity_resets{};
        bool enabled{true};
    };
    explicit AdaptiveParticipation(unsigned maximum) : maximum{maximum} {}
    unsigned Desired() const { return current ? current->policy.Desired() : maximum; }
    void Limit(unsigned limit) {
        if (maximum == limit) return;
        for (auto& entry : entries) Retire(entry);
        current = nullptr;
        maximum = limit;
        ++capacity_resets;
    }
    void Prepare(std::uint32_t count, std::uint32_t grain, bool hook = false,
                 AdaptiveWorkIdentity identity = {}) {
        current = nullptr;
        if (!identity) { ++unknown_identity; return; }
        grain = std::max(1U, grain);
        if (!maximum || count / grain < 2) return;
        const Key key{identity, count, grain, hook};
        // CodexAstraLocal: Two probes and integer mixing only. Full keys are
        // compared after hashing, so a collision cannot mix execution histories.
        const auto hash = identity.program ^ (identity.operands >> 17) ^
            (identity.contract * 0x9e3779b97f4a7c15ULL) ^
            (std::uint64_t{count} * 0x85ebca6bU) ^ (std::uint64_t{grain} << 11) ^ hook;
        const auto set = static_cast<std::size_t>((hash ^ (hash >> 32)) & (Sets - 1)) * Ways;
        const auto stamp = ++sequence;
        Entry* replace = &entries[set];
        for (unsigned way = 0; way < Ways; ++way) {
            auto& entry = entries[set + way];
            if (entry.valid && entry.key == key) {
                ++cohort_hits;
                // CodexAstraLocal: Periodic static requalification bounds a
                // stale accepted cap under changed thermal/OS conditions. An
                // inactive cohort also expires rather than reviving old trials.
                if (stamp - entry.last > MaxAge || entry.visits >= RecheckVisits) {
                    Retire(entry);
                    ++cohort_expired;
                    entry.key = key;
                    entry.policy = AdaptiveWorkWindow{maximum};
                    entry.valid = true;
                }
                current = &entry;
                break;
            }
            if (!entry.valid || (replace->valid && entry.last < replace->last))
                replace = &entry;
        }
        if (!current) {
            ++cohort_misses;
            if (replace->valid) { ++cohort_evictions; Retire(*replace); }
            replace->key = key;
            replace->policy = AdaptiveWorkWindow{maximum};
            replace->valid = true;
            current = replace;
        }
        current->last = stamp;
        ++current->visits;
        current->policy.Prepare(count, grain, hook);
    }
    bool SelectSample(std::uint32_t count, std::uint32_t grain) {
        return current && current->policy.SelectSample(count, grain);
    }
    void Observe(const Sample& sample) {
        if (current) current->policy.Observe(sample);
    }
    Statistics GetStatistics() const {
        Statistics result;
        static_cast<AdaptiveWorkWindow::Statistics&>(result) = retired;
        for (const auto& entry : entries)
            if (entry.valid) Add(result, entry.policy.GetStatistics());
        result.desired = Desired();
        result.cohort_hits = cohort_hits;
        result.cohort_misses = cohort_misses;
        result.cohort_evictions = cohort_evictions;
        result.cohort_expired = cohort_expired;
        result.unknown_identity = unknown_identity;
        result.capacity_resets = capacity_resets;
        return result;
    }
private:
    static constexpr unsigned Sets = 16, Ways = 2;
    static constexpr std::uint64_t MaxAge = 65536, RecheckVisits = 8192;
    struct Key {
        AdaptiveWorkIdentity identity;
        std::uint32_t count{}, grain{};
        bool hook{};
        bool operator==(const Key&) const = default;
    };
    struct Entry {
        Key key;
        AdaptiveWorkWindow policy;
        std::uint64_t last{}, visits{};
        bool valid{};
    };
    static void Add(AdaptiveWorkWindow::Statistics& to,
                    const AdaptiveWorkWindow::Statistics& from) {
        to.samples += from.samples; to.rejected += from.rejected;
        to.trials += from.trials; to.accepted += from.accepted;
        to.reverted += from.reverted; to.unstable_skipped += from.unstable_skipped;
        for (unsigned i = 0; i < to.reject_reasons.size(); ++i)
            to.reject_reasons[i] += from.reject_reasons[i];
    }
    void Retire(Entry& entry) {
        if (!entry.valid) return;
        Add(retired, entry.policy.GetStatistics());
        // CodexAstraLocal: Evicting a pending experiment restores static policy
        // on the next lookup and is reported as a canceled trial, not a success.
        const auto status = entry.policy.GetStatistics();
        if (status.trials > status.accepted + status.reverted) ++retired.reverted;
        entry = Entry{};
    }
    unsigned maximum;
    std::array<Entry, Sets * Ways> entries{};
    Entry* current{};
    AdaptiveWorkWindow::Statistics retired{};
    std::uint64_t sequence{}, cohort_hits{}, cohort_misses{}, cohort_evictions{};
    std::uint64_t cohort_expired{}, unknown_identity{}, capacity_resets{};
};

} // namespace Common::Uberhar
