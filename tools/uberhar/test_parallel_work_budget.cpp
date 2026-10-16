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
    AuxiliaryBudget budget{3, true}; // CodexAstraLocal: Explicit diagnostic census qualification.
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
    AuxiliaryBudget budget{4, true}; // CodexAstraLocal: Optional census must not alter real admission.
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
    using Sample = ParticipationWindow::Sample;
    const Sample base{1024,16,256,768,3,400000,200000,50000,0,false,false};
    auto window = [](ParticipationWindow& policy, Sample value) {
        for (unsigned i=0;i<8;++i) policy.Observe(value);
    };
    ParticipationWindow cadence{3};
    for (unsigned i=0;i<1024;++i) Need(!cadence.SelectSample(1,64), "tiny job sampled");
    for (unsigned i=1;i<=128;++i)
        Need(cadence.SelectSample(1024,16) == (i%64==0), "sample cadence");
    ParticipationWindow policy{3};
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
        ParticipationWindow reject{3}; window(reject,base);
        auto invalid=lower;
        if (reason==0) invalid.denied=true;
        if (reason==1) invalid.failed=true;
        if (reason==2) invalid.total_ns=4000;
        if (reason==3) invalid.owner_ns=1000000;
        if (reason==4) invalid.hook_ns=200000;
        window(reject,invalid);
        Need(reject.Desired()==3, "incomparable trial not rolled back");
    }
    ParticipationWindow changed{3}; window(changed,base);
    changed.Limit(1);
    Need(changed.Desired()==1, "registered progress capacity not applied");
    changed.Limit(3);
    Need(changed.Desired()==3, "released progress capacity not restored");
    window(changed,base);
    Need(changed.Desired()==2, "shape control missing trial");
    changed.Prepare(2048,16);
    Need(changed.Desired()==3, "unsampled shape inherited trial cap");
    // CodexAstraLocal: A single window still resets shape. Cross-job retention
    // belongs to the independently exercised exact-history owner below.
    changed.Prepare(1024,16);
    Need(changed.Desired()==3, "single-window shape did not reset");
    window(changed,base); window(changed,lower);
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

// CodexAstraLocal: Exercise the actual prepare/select/observe sequence. Unlike
// manually feeding history windows, these controls expose cadence starvation,
// protected-key ownership, dormant discovery and sampled expiry before helpers.
static void Histories() {
    using Policy = AdaptiveParticipation;
    using Sample = Policy::Sample;
    const Sample base{1024,16,256,768,3,400000,200000,50000,0,false,false};
    auto lower=base; lower.helpers=2; lower.owner_items=384; lower.worker_items=640;
    lower.owner_ns=300000; lower.total_ns=390000;
    const auto sample = [&](Policy& policy, std::uint64_t identity, Sample value) {
        for (unsigned attempts=0; attempts<Policy::DormantStride; ++attempts) {
            policy.Prepare(1024,16,false,identity);
            if (!policy.SelectSample(1024,16)) continue;
            policy.Observe(value); return;
        }
        Need(false,"history never sampled within bound");
    };
    Policy churn{3};
    for (unsigned i=0;i<128;++i) {
        churn.Prepare(1024,16,false,i+1);
        if(churn.SelectSample(1024,16)) churn.Observe(base);
    }
    Need(churn.GetStatistics().probes==2 && churn.GetStatistics().promotions==0 &&
         churn.GetStatistics().evictions==0,"key churn reset cadence or reconstructed windows");
    Policy retained{3};
    // Every sampling opportunity belongs to key11, with63unrelated eligible
    // calls between them. Costs from those other identities never enter key11.
    for(unsigned epoch=0;epoch<9;++epoch) {
        for(unsigned n=0;n<63;++n) {
            retained.Prepare(1024,16,false,100+epoch*63+n);
            Need(!retained.SelectSample(1024,16),"global cadence drift");
        }
        retained.Prepare(1024,16,false,11);
        Need(retained.SelectSample(1024,16),"recurring identity starved");
        retained.Observe(base);
    }
    Need(retained.Desired()==2 && retained.GetStatistics().promotions==1 &&
         retained.GetStatistics().samples==8 && retained.GetStatistics().trials==1,
         "probation failed to retain exact baseline");
    retained.Prepare(1024,16,false,0);
    Need(retained.Desired()==3 && !retained.SelectSample(1024,16),"mixed work inherited trial");
    retained.Prepare(2048,16,false,11);
    Need(retained.Desired()==3,"new shape inherited trial");
    retained.Prepare(1024,16,true,11);
    Need(retained.Desired()==3,"new hook inherited trial");
    for(unsigned i=0;i<8;++i) sample(retained,11,lower);
    Need(retained.GetStatistics().accepted==1 && retained.Desired()==2,"retained trial not consumed");
    retained.Limit(1); retained.Prepare(1024,16,false,11);
    Need(retained.Desired()==1 && retained.GetStatistics().histories==0 &&
         retained.GetStatistics().capacity_resets==1,"capacity change retained stale policy");

    Policy expiry{3};
    for(unsigned i=0;i<9;++i) sample(expiry,11,base);
    Need(expiry.Desired()==2,"expiry control missing trial");
    // Current invalid probes advance the pool's clock, but cannot indefinitely
    // protect the trial or enter discovery as cheap/unqualified evidence.
    auto invalid=base; invalid.total_ns=10;
    for(unsigned i=0;i<Policy::EvidenceAge;++i) sample(expiry,22,invalid);
    expiry.Prepare(1024,16,false,11);
    Need(expiry.Desired()==3 && expiry.GetStatistics().trial_expired==1 &&
         expiry.GetStatistics().reverted==1,"expired trial survived or lost rollback count");

    // CodexAstraLocal: Individually valid but alternating costs must not keep
    // an incomplete baseline alive forever merely by refreshing last-sample age.
    Policy unstable{3};
    for(unsigned i=0;i<Policy::EvidenceAge+2;++i) {
        auto value=base; if(i&1) value.owner_ns=350000;
        sample(unstable,77,value);
    }
    Need(unstable.GetStatistics().trials==0 && unstable.GetStatistics().expired==1 &&
         unstable.GetStatistics().histories==0 && unstable.GetStatistics().dormant,
         "partial samples pinned inert history");

    Policy dormant{3};
    for(unsigned i=0;i<Policy::EvidenceAge;++i) sample(dormant,101+i,invalid);
    Need(dormant.GetStatistics().dormant && dormant.GetStatistics().histories==0,
         "unproductive discovery never dormant");
    const auto probes=dormant.GetStatistics().probes;
    for(unsigned i=0;i<Policy::DormantStride-1;++i) {
        dormant.Prepare(1024,16,false,33);
        Need(!dormant.SelectSample(1024,16),"dormant policy read excessive samples");
    }
    dormant.Prepare(1024,16,false,33);
    Need(dormant.SelectSample(1024,16),"dormant probe absent"); dormant.Observe(base);
    sample(dormant,33,base);
    Need(!dormant.GetStatistics().dormant && dormant.GetStatistics().promotions==1 &&
         dormant.GetStatistics().samples==1 && dormant.GetStatistics().probes==probes+2,
         "dormant wake borrowed missing evidence");

    // CodexAstraLocal: A recurring fifth key remains static; discovery cannot
    // evict any of the four protected active trials to manufacture progress.
    Policy full{3};
    for(unsigned key=1;key<=4;++key) for(unsigned i=0;i<9;++i) sample(full,key,base);
    sample(full,5,base); sample(full,5,base);
    Need(full.Desired()==3 && full.GetStatistics().histories==4 &&
         full.GetStatistics().promotions==4 && full.GetStatistics().evictions==0,
         "probation evicted protected learning");
    full.Prepare(1024,16,false,1);
    Need(full.Desired()==2,"fifth key changed first trial");

    Policy disabled{3,false};
    for(unsigned i=0;i<8192;++i) {
        disabled.Prepare(1024,16,false,i+1);
        Need(disabled.Desired()==3 && !disabled.SelectSample(1024,16),"disabled policy executed");
    }
    Need(!disabled.GetStatistics().enabled && disabled.GetStatistics().probes==0 &&
         disabled.GetStatistics().history_misses==0,"disabled history maintenance remained");
    AuxiliaryBudget budget{4};
    ParallelWork pool{4,nullptr,budget,nullptr,false,false};
    std::array<std::atomic<unsigned>,128> visits{};
    pool.Run(128,16,[&](unsigned first,unsigned last){for(auto i=first;i<last;++i)++visits[i];});
    for(auto& value:visits) Need(value==1,"disabled adaptation changed real execution");
    Need(!pool.Scheduling().enabled && pool.Scheduling().prepare_samples==0 &&
         pool.Scheduling().observe_samples==0,"disabled diagnostic clocks sampled");
}

// CodexAstraLocal: Disabling diagnostic-only atomics must preserve the same
// hard admission/nesting/lifetime behavior; absent census is explicitly labeled.
static void WithoutCensus() {
    AuxiliaryBudget budget{2};
    { auto permit=budget.TryAcquire(); Need(bool(permit)&&!budget.TryAcquire(),"uncounted admission"); }
    auto lane=budget.ReserveProgress();
    Need(bool(lane)&&budget.HelperCapacity()==0&&!budget.TryAcquire(),"uncounted reservation bound");
    const auto stats=budget.GetStatistics();
    Need(!stats.execution_census && stats.active==0 && stats.peak==0 && stats.reservations==1,
         "absent execution census mislabeled");
}

int main() try {
    ProgressAndNesting(); ConcurrentPools(); Adaptation(); Histories(); WithoutCensus();
    std::cout << "shared budget PASS: execution, reserved progress, nesting, output, adaptive rollback\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
