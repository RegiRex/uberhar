// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Exercise the production admission/controller and real joined
// pools. Synthetic time describes policy decisions only, never performance.
#include <atomic>
#include <barrier>
#include <cfenv>
#include <chrono>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <vector>
#include "common/uberhar_parallel_work.h"

using namespace Common::Uberhar;
static void Need(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

// CodexAstraLocal: Saturated helpers cannot block a normal owner or a reserved
// coordinator. A nested callback cannot claim another token or wake borrowers.
static void ProgressAndNesting() {
    AuxiliaryBudget single{1};
    Need(!single.ReserveProgress() && !single.TryAcquire(), "single CPU capacity");
    AuxiliaryBudget budget{3, true}; // CodexAstraLocal: Explicit execution-census qualification.
    {
        auto lane = budget.ReserveProgress();
        Need(bool(lane) && budget.Active() == 0 && budget.HelperCapacity() == 1,
             "idle reservation counted as execution");
        std::binary_semaphore acquired{0}, release{0};
        std::thread holder{[&] {
            auto permit = budget.TryAcquire();
            if (!permit) std::terminate();
            acquired.release(); release.acquire();
        }};
        acquired.acquire();
        // CodexAstraLocal: Always release/join the controlled holder, including
        // intentional-defect assertions, so a deadlock is never credited.
        struct Guard {
            std::binary_semaphore& release; std::thread& holder;
            ~Guard() { release.release(); holder.join(); }
        } guard{release, holder};
        Need(!budget.ReserveProgress(), "reserved capacity oversubscribed");
        Need(!budget.TryAcquire(), "exhausted shared capacity admitted work");
        ParallelWork owner{3, nullptr, budget};
        std::uint32_t sum{};
        const auto normal = owner.Run(128, 1, [&](auto a, auto b) { sum += b-a; });
        Need(sum == 128 && normal.worker_items == 0 && budget.Active() == 1,
             "saturated owner progress");
        ParallelWork coordinator{3, nullptr, budget, &lane};
        ParallelWork nested{3, nullptr, budget};
        const auto thread = std::this_thread::get_id();
        bool caught{};
        try {
            coordinator.Run(64, 1, [&](auto, auto) {
                Need(budget.Active() == 2 && AuxiliaryBudget::HasPermit(),
                     "coordinator execution uncounted");
                const auto inner = nested.Run(32, 1, [&](auto first, auto last) {
                    Need(std::this_thread::get_id() == thread && first == 0 && last == 32,
                         "nested work escaped existing token");
                });
                Need(inner.owner_items == 32 && nested.CreatedWorkers() == 0,
                     "nested pool created borrowers");
                throw std::runtime_error("expected callback failure");
            });
        } catch (const std::runtime_error& error) {
            Need(std::string_view{error.what()} == "expected callback failure", error.what());
            caught = true;
        }
        Need(caught && budget.Active() == 1 && !AuxiliaryBudget::HasPermit(),
             "coordinator token leaked on failure");
    }
    Need(budget.Active() == 0 && budget.HelperCapacity() == 2, "lane/permit lifetime");
    // CodexAstraLocal: More than one independent coordinator is legal only if
    // every lane fits. Each retains its own counted capacity until destruction.
    auto first = budget.ReserveProgress();
    auto second = budget.ReserveProgress();
    Need(first && second && !budget.ReserveProgress() && budget.HelperCapacity() == 0,
         "multiple coordinator capacity");
}

// CodexAstraLocal: Concurrent pools share one hard limit, while their owners
// remain independent. Check exact output and each helper's live token; short
// sleeps widen this correctness overlap and are not benchmark observations.
static void ConcurrentPools() {
    AuxiliaryBudget budget{4, true}; // CodexAstraLocal: Census does not replace admission.
    const auto capacity = budget.GetStatistics().capacity;
    std::barrier start{4};
    std::atomic<unsigned> active{}, peak{}, helpers{}, errors{};
    std::vector<std::thread> owners;
    for (unsigned p=0; p<4; ++p) owners.emplace_back([&] {
        ParallelWork pool{4, nullptr, budget};
        std::vector<std::atomic<unsigned>> visits(64);
        const auto owner = std::this_thread::get_id();
        start.arrive_and_wait();
        try {
            const auto result = pool.Run(64, 1, [&](unsigned a, unsigned b) {
                const bool helper = std::this_thread::get_id() != owner;
                if (helper) {
                    ++helpers;
                    const auto live = ++active;
                    auto prior = peak.load();
                    while (live > prior && !peak.compare_exchange_weak(prior, live)) {}
                    if (!AuxiliaryBudget::HasPermit() || live > capacity) ++errors;
                }
                std::this_thread::sleep_for(std::chrono::microseconds{100});
                for (auto i=a; i<b; ++i) ++visits[i];
                if (helper) --active;
            });
            if (result.owner_items + result.worker_items != 64) ++errors;
            for (auto& value : visits) if (value != 1) ++errors;
        } catch (...) { ++errors; }
    });
    for (auto& owner : owners) owner.join();
    Need(errors == 0 && helpers != 0 && peak <= capacity && budget.Active() == 0,
         "shared executing-helper capacity/output");
    const auto counters=budget.GetStatistics();
    Need(counters.peak >= peak && counters.peak <= capacity && counters.reservations==0,
         "executing-helper peak snapshot");
}

// CodexAstraLocal: Feed stable same-work windows, not elapsed sleeps or clock
// guesses. These values force accept/rollback independently of host scheduling.
static void Adaptation() {
    using Sample = AdaptiveWorkWindow::Sample;
    const Sample base{1024,16,256,768,3,400000,200000,50000,0,false,false};
    auto window = [](AdaptiveWorkWindow& policy, Sample value) {
        for (unsigned i=0;i<8;++i) policy.Observe(value);
    };
    AdaptiveWorkWindow cadence{3};
    for (unsigned i=0;i<1024;++i) Need(!cadence.SelectSample(1,64), "tiny job sampled");
    // CodexAstraLocal: Alternating task shapes must avoid adaptive clocks while
    // retaining the full worker allowance; stable work still reaches trials.
    for (unsigned i=0;i<128;++i) {
        cadence.Prepare(i%2 ? 1024 : 2048,16);
        Need(!cadence.SelectSample(i%2 ? 1024 : 2048,16), "unstable shape sampled");
        Need(cadence.Desired()==3, "sampling gate changed worker budget");
    }
    cadence.Prepare(4096,16);
    for (unsigned i=0;i<7;++i) cadence.Prepare(4096,16);
    for (unsigned i=1;i<=128;++i) {
        cadence.Prepare(4096,16);
        Need(cadence.SelectSample(4096,16) == (i%64==0), "sample cadence");
    }
    Need(cadence.GetStatistics().unstable_skipped==128, "avoided-sample accounting");
    AdaptiveWorkWindow policy{3};
    for (unsigned i=0;i<7;++i) policy.Observe(base);
    Need(policy.Desired()==3, "unstable early trial");
    policy.Observe(base);
    Need(policy.Desired()==2, "stable window missing trial");
    auto lower=base; lower.helpers=2; lower.owner_items=384; lower.worker_items=640;
    lower.owner_ns=300000; lower.total_ns=390000;
    window(policy,lower);
    Need(policy.Desired()==2, "equal useful work rejected lower participation");
    window(policy,lower); window(policy,lower);
    Need(policy.Desired()==2, "cooldown changed participation");
    window(policy,lower);
    Need(policy.Desired()==1, "subsequent adjacent trial missing");
    auto slower=lower; slower.helpers=1; slower.owner_items=512; slower.worker_items=512;
    slower.owner_ns=400000; slower.total_ns=700000; slower.join_ns=250000;
    window(policy,slower);
    Need(policy.Desired()==2, "regressing trial not rolled back");
    window(policy,lower); window(policy,lower); window(policy,lower);
    Need(policy.Desired()==3, "reversed direction did not test recovery");
    auto faster=base; faster.total_ns=350000;
    window(policy,faster);
    Need(policy.Desired()==3, "useful faster participation not accepted");
    for (unsigned reason=0;reason<5;++reason) {
        AdaptiveWorkWindow reject{3}; window(reject,base);
        auto invalid=lower;
        if (reason==0) invalid.denied=true;
        if (reason==1) invalid.failed=true;
        if (reason==2) invalid.total_ns=4000;
        if (reason==3) invalid.owner_ns=1000000;
        if (reason==4) invalid.hook_ns=200000;
        window(reject,invalid);
        Need(reject.Desired()==3, "incomparable trial not rolled back");
    }
    AdaptiveWorkWindow changed{3}; window(changed,base);
    Need(changed.Desired()==2, "shape control missing trial");
    changed.Prepare(2048,16);
    Need(changed.Desired()==3, "unsampled shape inherited trial cap");
    changed.Prepare(1024,16); window(changed,base);
    Need(changed.Desired()==2, "restored shape baseline");
    changed.Prepare(1024,16,true);
    Need(changed.Desired()==3, "changed hook inherited trial cap");
    const auto counters=policy.GetStatistics();
    Need(counters.samples && counters.trials==3 && counters.accepted==2 && counters.reverted==1,
         "adaptive outcome reporting");
    // CodexAstraLocal: Controller arithmetic must not disturb the caller's
    // guest rounding/sticky status even while accepting or reversing a trial.
    const auto fp=ParallelFloatEnvironment::Capture();
    std::feclearexcept(FE_ALL_EXCEPT); std::feraiseexcept(FE_DIVBYZERO);
    const auto before=ParallelFloatEnvironment::Capture();
    window(changed,base); window(changed,lower);
    const auto flags=ParallelFloatEnvironment::Flags();
    before.Apply(); const auto expected=ParallelFloatEnvironment::Flags(); fp.Apply();
    Need(flags==expected, "adaptive arithmetic altered FP status");
}

// CodexAstraLocal: Interleaved exact-shape/program cohorts must learn separately;
// identical sizes never authorize cross-program reuse. These are deterministic
// controller costs, not claims about host/device performance.
static void CohortsAndBypass() {
    using Identity = AdaptiveWorkIdentity;
    AdaptiveParticipation policy{3};
    const Identity a{11, 7, 1}, b{22, 8, 2}, c{33, 9, 3};
    auto visit = [&](Identity identity, std::uint32_t count) {
        policy.Prepare(count, 16, false, identity);
        if (!policy.SelectSample(count, 16)) return;
        const auto helpers = policy.Desired();
        const auto owner = helpers == 3 ? count / 4 : count * 3 / 8;
        policy.Observe({count, 16, owner, count - owner, helpers,
            (helpers == 3 ? 400000U : 390000U) * std::uint64_t{count} / 1024,
            owner * 800U, 50000, 0, false, false});
    };
    for (unsigned i = 0; i < 1031; ++i) { visit(a, 1024); visit(b, 2048); }
    policy.Prepare(1024, 16, false, a);
    Need(policy.Desired() == 2, "interleaved A history missing accepted limit");
    policy.Prepare(2048, 16, false, b);
    Need(policy.Desired() == 2, "interleaved B history missing accepted limit");
    policy.Prepare(1024, 16, false, c);
    Need(policy.Desired() == 3, "different program reused same-size trial");
    policy.Prepare(1024, 16, true, a);
    Need(policy.Desired() == 3, "different hook reused trial");
    const auto learned = policy.GetStatistics();
    Need(learned.samples == 32 && learned.trials == 2 && learned.accepted == 2 &&
         learned.cohort_hits > 2000 && learned.cohort_misses == 4,
         "cohort outcome accounting");
    policy.Limit(1);
    policy.Prepare(1024, 16, false, a);
    Need(policy.Desired() == 1 && policy.GetStatistics().capacity_resets == 1,
         "changed capacity retained earlier limit");
    // CodexAstraLocal: Unique keys evict within fixed storage and preserve
    // cumulative evidence; returning to an evicted shape starts statically.
    for (unsigned i = 0; i < 100; ++i) policy.Prepare(1024, 16, false, {i + 100U, 7, 1});
    Need(policy.GetStatistics().cohort_evictions != 0 &&
         policy.GetStatistics().accepted == learned.accepted, "bounded eviction/accounting");
    policy.Prepare(1024, 16);
    Need(policy.Desired() == 1 && !policy.SelectSample(1024,16) &&
         policy.GetStatistics().unknown_identity == 1, "unknown work trained history");

    AdaptiveParticipation expired{3};
    for (unsigned i = 0; i < 8193; ++i) expired.Prepare(1024, 16, false, a);
    Need(expired.GetStatistics().cohort_expired == 1 && expired.Desired() == 3,
         "periodic static requalification missing");

    AuxiliaryBudget budget{2};
    ParallelWork off{2, nullptr, budget, nullptr, false};
    std::vector<unsigned> output(128);
    for (unsigned run = 0; run < 80; ++run) {
        off.Run(128, 16, [&](auto first, auto last) {
            for (auto i = first; i < last; ++i) ++output[i];
        }, false, a);
    }
    for (const auto value : output) Need(value == 80, "adaptive OFF changed work output");
    const auto disabled = off.Scheduling();
    Need(!disabled.enabled && disabled.samples == 0 && disabled.cohort_hits == 0 &&
         disabled.cohort_misses == 0 && disabled.unknown_identity == 0 &&
         off.CreatedWorkers() != 0, "adaptive OFF retained bookkeeping or removed pool");
}

// CodexAstraLocal: Removing diagnostic atomics must leave real admission,
// nesting and reserved capacity intact; absent active/peak counts are labeled.
static void WithoutCensus() {
    AuxiliaryBudget budget{2};
    { auto permit = budget.TryAcquire();
      Need(bool(permit) && !budget.TryAcquire(), "uncounted admission"); }
    auto lane = budget.ReserveProgress();
    Need(bool(lane) && budget.HelperCapacity() == 0 && !budget.TryAcquire(),
         "uncounted reservation bound");
    const auto stats = budget.GetStatistics();
    Need(!stats.execution_census && stats.active == 0 && stats.peak == 0 && stats.reservations == 1,
         "absent execution census mislabeled");
}

int main() try {
    ProgressAndNesting(); ConcurrentPools(); Adaptation(); CohortsAndBypass(); WithoutCensus();
    std::cout << "shared budget PASS: execution, reserved progress, nesting, output, adaptive rollback\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
