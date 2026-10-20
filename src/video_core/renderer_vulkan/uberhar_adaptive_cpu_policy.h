// CodexAstraLocal: Bounded CPU PSO reuse replaces an append-only bank. Reclaim only after
// caller-proved command drain, compiler release and completed GPU use; shader
// selection and complete generic fallback remain the renderer's responsibility.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace Vulkan::AdaptiveCpu {
using u64 = std::uint64_t;
enum class State { Empty, Reserved, Building, Ready, Failed, Retired, DestroyQueued };

// CodexAstraLocal: Existing users retain one eight-owner/64-attempt bank.
// An explicit partition policy may reserve separate capacity and lifetime work
// for independent optional tiers without weakening command/compiler/GPU release.
template<class Key> struct SinglePartition {
    static constexpr std::size_t Slots = 8, CpuAttempts = 64;
    static constexpr std::size_t Count = 1;
    static std::size_t Index(const Key&) { return 0; }
    static constexpr std::size_t Owners(std::size_t) { return Slots; }
    static constexpr std::size_t Attempts(std::size_t) { return CpuAttempts; }
};

template<class Key, class Owner, class Partitions = SinglePartition<Key>> class Cache {
public:
    static constexpr std::size_t Slots = Partitions::Slots, Observations = 64,
                                 CpuAttempts = Partitions::CpuAttempts;
    static constexpr std::size_t CombinedLimit = 256;
    static constexpr u64 Window = 8, Distinct = 4, Requests = 16, Interval = 4;
    static_assert(std::is_nothrow_copy_constructible_v<Key>);
    static_assert(std::is_nothrow_copy_assignable_v<Key>);

    struct Token {
        std::size_t slot = Slots;
        u64 generation{};
        explicit operator bool() const { return slot < Slots && generation != 0; }
    };
    struct Slot {
        std::unique_ptr<Owner> owner;
        std::optional<Key> observation;
        u64 observation_hash{}, execution_hash{}, generation{}, last_requested{}, last_use_tick{};
        std::atomic<u64> compiler_released{}, destroyed{};
        State state = State::Empty;
        std::size_t attempt{};
        bool lit{};
    };
    struct Stats {
        u64 observations{}, collisions{}, probation{}, retirements{}, destroyed{},
            failed_keys{}, queue_failures{}, exhausted{}, stale_tokens{}, max_owned{};
    } stats;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // CodexAstraLocal: Ready-map lookup never exposes retired or destruction-queued
    // owners. Final full execution/owner equality is still checked by the caller.
    Slot* Find(u64 execution_hash) {
        for (auto& slot : slots)
            if (Active(slot.state) && slot.execution_hash == execution_hash && slot.owner)
                return &slot;
        return nullptr;
    }
    void Requested(Slot& slot) {
        // CodexAstraLocal: A terminal failed key cannot become useful by being
        // requested again; leave it reclaimable while the failed ledger blocks retries.
        if (slot.state != State::Failed) slot.last_requested = swap;
    }
    Token Selected(Slot& slot, u64 tick) {
        Requested(slot);
        slot.last_use_tick = std::max(slot.last_use_tick, tick);
        return {static_cast<std::size_t>(&slot - slots.data()), slot.generation};
    }
    // CodexAstraLocal: A stream-buffer wait can flush between bind and draw. The
    // caller stamps the same token after actual draw enqueue, retaining both ticks.
    void DrawQueued(Token token, u64 tick) {
        if (!token || slots[token.slot].generation != token.generation ||
            !Active(slots[token.slot].state)) {
            ++stats.stale_tokens;
            return;
        }
        auto& slot = slots[token.slot];
        slot.last_use_tick = std::max(slot.last_use_tick, tick);
    }

    // CodexAstraLocal: Exact-key fixed-table probation counts all requests in an
    // eight-swap window, with at most16 per interval. A collision only loses demand.
    bool Observe(u64 hash, const Key& key) {
        if (!DemandBudget(Partitions::Index(key))) return false;
        ++stats.observations;
        if (Failed(hash, key)) { ++stats.failed_keys; return false; }
        auto& entry = observations[ObservationIndex(hash, key)];
        if (!entry.key || entry.hash != hash || !(*entry.key == key)) {
            stats.collisions += entry.key.has_value();
            entry.key = key;
            entry.hash = hash;
            entry.last_swap = swap;
            entry.counts = {};
        } else if (swap != entry.last_swap) {
            const u64 elapsed = swap - entry.last_swap;
            if (elapsed >= Window) entry.counts = {};
            else {
                for (std::size_t i = Window; i-- > elapsed;)
                    entry.counts[i] = entry.counts[i - elapsed];
                std::fill_n(entry.counts.begin(), elapsed, 0);
            }
            entry.last_swap = swap;
        }
        entry.counts[0] = std::min<unsigned>(Requests, entry.counts[0] + 1);
        const bool qualified = Qualified(entry);
        stats.probation += !qualified;
        return qualified;
    }

    // CodexAstraLocal: Terminal admission limits avoid rebuilding a large exact
    // observation key forever; active ready lookup remains independent.
    bool AcceptingDemand(std::size_t combined_attempts) const {
        return !disabled && attempts < CpuAttempts && combined_attempts < CombinedLimit;
    }
    // CodexAstraLocal: Exhaustion of one tier stops only its new demand; ready
    // lookup and the other tier remain usable, including after terminal budgets.
    bool AcceptingDemand(std::size_t combined_attempts, std::size_t partition) const {
        return partition < Partitions::Count && combined_attempts < CombinedLimit &&
               DemandBudget(partition);
    }

    bool CanCreate(std::size_t other_owned, std::size_t combined_attempts,
                   bool external_pending) {
        if (disabled || attempts >= CpuAttempts || combined_attempts >= CombinedLimit) {
            ++stats.exhausted;
            return false;
        }
        if (owned >= Slots || other_owned >= CombinedLimit - owned) {
            return false;
        }
        return !external_pending && !AnyJob() &&
            (!last_admission || swap - *last_admission >= Interval);
    }

    // CodexAstraLocal: Count in-flight, failed and retiring owners against their
    // original partition until physical destruction publishes completion.
    bool CanCreate(std::size_t other_owned, std::size_t combined_attempts,
                   bool external_pending, const Key& key) {
        const auto partition = Partitions::Index(key);
        if (!DemandBudget(partition)) { ++stats.exhausted; return false; }
        return Owned(partition) < Partitions::Owners(partition) &&
               CanCreate(other_owned, combined_attempts, external_pending);
    }

    // CodexAstraLocal: Reserve counts the slot and lifetime work token before
    // allocation. Allocation/queue failures cannot create an unaccounted owner.
    Slot* Reserve(u64 hash, const Key& key, bool lit, std::size_t other_owned,
                  std::size_t& combined_attempts, bool external_pending) {
        if (!CanCreate(other_owned, combined_attempts, external_pending, key) || Failed(hash, key))
            return nullptr;
        const auto& seen = observations[ObservationIndex(hash, key)];
        if (!seen.key || seen.hash != hash || !(*seen.key == key) ||
            seen.last_swap != swap || !Qualified(seen)) return nullptr;
        for (auto& slot : slots) {
            if (slot.state != State::Empty) continue;
            if (slot.generation == std::numeric_limits<u64>::max()) {
                disabled = true;
                return nullptr;
            }
            ++slot.generation;
            slot.observation = key;
            slot.observation_hash = hash;
            slot.execution_hash = 0;
            slot.last_requested = swap;
            slot.last_use_tick = 0;
            slot.lit = lit;
            slot.state = State::Reserved;
            slot.attempt = attempts;
            ledger[attempts] = Attempt{key, hash, false};
            ++attempts;
            ++combined_attempts;
            ++owned;
            if constexpr (Partitions::Count > 1) {
                ++partition_owned[Partitions::Index(key)];
                ++partition_attempts[Partitions::Index(key)];
            }
            stats.max_owned = std::max<u64>(stats.max_owned, owned);
            last_admission = swap;
            return &slot;
        }
        return nullptr;
    }
    void AllocationFailed(Slot& slot) noexcept {
        ledger[slot.attempt].failed = true;
        slot.state = State::Failed;
        slot.compiler_released.store(slot.generation, std::memory_order_release);
    }

    // CodexAstraLocal: The stable slot's release generation is the final access
    // after Build/failed completion, so early IsDone publication cannot free an
    // owner still read by logging/caller epilogues. No owning capture can unwind
    // on a caller after a failed enqueue.
    template<class Queue, class Build>
    bool Start(Slot& slot, std::unique_ptr<Owner> owner, u64 execution_hash,
               Queue& queue, Build build) noexcept {
        static_assert(std::is_trivially_destructible_v<Build>);
        slot.owner = std::move(owner);
        slot.execution_hash = execution_hash;
        slot.state = State::Building;
        auto* stable = &slot;
        const auto generation = slot.generation;
        try {
            queue.QueueWork([stable, generation, build] {
                try {
                    if (!build(*stable->owner)) stable->owner->MarkFailed();
                } catch (...) {
                    if (!stable->owner->IsDone()) stable->owner->MarkFailed();
                }
                stable->compiler_released.store(generation, std::memory_order_release);
            });
            return true;
        } catch (...) {
            slot.owner->MarkFailed();
            ledger[slot.attempt].failed = true;
            slot.state = State::Failed;
            slot.compiler_released.store(generation, std::memory_order_release);
            ++stats.queue_failures;
            return false;
        }
    }

    // CodexAstraLocal: Caller invokes only immediately after its existing command
    // worker drain. Invalidation is at that boundary; GPU completion and compiler
    // release remain independent. At most one destroy job exists, and its owner
    // remains in the fixed slot until the worker publishes completion.
    template<class Queue, class Invalidate>
    void FrameAfterDrain(u64 known_gpu_tick, std::size_t other_owned,
                         std::size_t combined_attempts, bool external_pending, Queue& queue,
                         Invalidate invalidate) {
        if (swap == std::numeric_limits<u64>::max()) { disabled = true; return; }
        ++swap;
        for (auto& slot : slots) {
            if (slot.state == State::DestroyQueued &&
                slot.destroyed.load(std::memory_order_acquire) == slot.generation) {
                slot.state = State::Empty;
                if constexpr (Partitions::Count > 1)
                    --partition_owned[Partitions::Index(*slot.observation)];
                slot.observation.reset();
                --owned;
                ++stats.destroyed;
            }
            if (slot.state == State::Building && Released(slot)) {
                const bool failed = slot.owner->HasFailed() || !slot.owner->Handle();
                slot.state = failed ? State::Failed : State::Ready;
                ledger[slot.attempt].failed |= failed;
            }
        }
        if (disabled) return;
        // CodexAstraLocal: Retire only to make a physically necessary replacement
        // while creation tokens remain. One in-flight retirement prevents one miss
        // from emptying the bank before its first replacement can be admitted.
        const bool pressure = owned == Slots || other_owned >= CombinedLimit - owned ||
                              PartitionPressure();
        bool retiring = false;
        for (const auto& slot : slots)
            retiring |= slot.state == State::Retired || slot.state == State::DestroyQueued;
        if (pressure && attempts < CpuAttempts && combined_attempts < CombinedLimit &&
            !retiring && FreshDemand()) {
            Slot* oldest{};
            for (auto& slot : slots) {
                if ((slot.state != State::Ready && slot.state != State::Failed) ||
                    !Released(slot) || !ReplacementDemand(slot, other_owned) ||
                    (slot.state != State::Failed &&
                                        swap - slot.last_requested < Window)) continue;
                if (!oldest || (slot.state == State::Failed && oldest->state != State::Failed) ||
                    (slot.state == oldest->state && slot.last_requested < oldest->last_requested))
                    oldest = &slot;
            }
            if (oldest) {
                // CodexAstraLocal: Removal from lookup precedes any later queued draw.
                oldest->state = State::Retired;
                invalidate(oldest->owner.get());
                ++stats.retirements;
            }
        }
        if (external_pending || AnyJob()) return;
        for (auto& slot : slots) {
            if (slot.state != State::Retired || !Released(slot) ||
                known_gpu_tick < slot.last_use_tick) continue;
            slot.state = State::DestroyQueued;
            auto* stable = &slot;
            const auto generation = slot.generation;
            try {
                queue.QueueWork([stable, generation] {
                    stable->owner.reset();
                    stable->destroyed.store(generation, std::memory_order_release);
                });
            } catch (...) {
                slot.state = State::Retired;
                disabled = true;
                ++stats.queue_failures;
            }
            break;
        }
    }

    // CodexAstraLocal: Only the inherited complete title/shutdown drains permit
    // unconditional teardown. Runtime capacity reuse never calls this method.
    void ResetAfterDrain() {
        for (auto& slot : slots) {
            slot.owner.reset();
            slot.observation.reset();
            slot.state = State::Empty;
            slot.last_requested = slot.last_use_tick = 0;
        }
        observations = {};
        ledger = {};
        swap = attempts = owned = 0;
        partition_owned = {};
        partition_attempts = {};
        last_admission.reset();
        disabled = false;
        stats = {};
    }
    // CodexAstraLocal: Final bounded census reads producer-owned attempt
    // metadata only, never the unique_ptr being destroyed on the worker.
    template<class Visitor> void VisitAttempts(Visitor visitor) const {
        for (std::size_t i = 0; i < attempts; ++i)
            visitor(i + 1, *ledger[i].key, ledger[i].hash, ledger[i].failed);
    }
    std::size_t Owned() const { return owned; }
    std::size_t Attempts() const { return attempts; }
    // CodexAstraLocal: Reports and pressure checks use owner-thread counters,
    // never the unique_ptr concurrently released by the optional worker.
    std::size_t Owned(std::size_t partition) const {
        if constexpr (Partitions::Count == 1) return owned;
        else return partition_owned[partition];
    }
    std::size_t Attempts(std::size_t partition) const {
        if constexpr (Partitions::Count == 1) return attempts;
        else return partition_attempts[partition];
    }
    u64 Swap() const { return swap; }
    bool Disabled() const { return disabled; }
    const auto& AllSlots() const { return slots; }
    bool AnyJob() const {
        for (const auto& slot : slots)
            if (slot.state == State::DestroyQueued ||
                (slot.state == State::Building && !Released(slot))) return true;
        return false;
    }

private:
    struct Observation {
        std::optional<Key> key;
        u64 hash{}, last_swap{};
        std::array<std::uint8_t, Window> counts{};
    };
    struct Attempt { std::optional<Key> key; u64 hash{}; bool failed{}; };
    static bool Active(State state) {
        return state == State::Building || state == State::Ready || state == State::Failed;
    }
    static bool Released(const Slot& slot) {
        return slot.compiler_released.load(std::memory_order_acquire) == slot.generation;
    }
    static bool Qualified(const Observation& entry) {
        unsigned count{}, distinct{};
        for (auto value : entry.counts) { count += value; distinct += value != 0; }
        return count >= Requests && distinct >= Distinct;
    }
    bool Failed(u64 hash, const Key& key) const {
        for (std::size_t i = 0; i < attempts; ++i)
            if (ledger[i].failed && ledger[i].hash == hash && *ledger[i].key == key) return true;
        return false;
    }
    // CodexAstraLocal: Preserve all 64 full-tier probation slots. A stable draw
    // observes both tiers consecutively; a shared modulo bucket would otherwise
    // let their exact, different keys erase each other's history forever.
    static std::size_t ObservationIndex(u64 hash, const Key& key) {
        return Partitions::Index(key) * Observations + hash % Observations;
    }
    bool FreshDemand(std::optional<std::size_t> partition = {}) const {
        for (const auto& seen : observations) {
            if (!seen.key || seen.last_swap != swap - 1 || !Qualified(seen) ||
                (partition && Partitions::Index(*seen.key) != *partition) ||
                (Partitions::Count > 1 && !DemandBudget(Partitions::Index(*seen.key))) ||
                Failed(seen.hash, *seen.key)) continue;
            bool resident = false;
            for (const auto& slot : slots)
                resident |= Active(slot.state) && slot.observation &&
                    slot.observation_hash == seen.hash && *slot.observation == *seen.key;
            if (!resident) return true;
        }
        return false;
    }
    // CodexAstraLocal: A ninth full owner must be able to replace an idle full
    // owner even when the partial half is empty. Conversely, partial pressure
    // cannot evict a full owner. Both demand and victim belong to the same tier.
    bool DemandBudget(std::size_t partition) const {
        return !disabled && attempts < CpuAttempts &&
               Attempts(partition) < Partitions::Attempts(partition);
    }
    bool PartitionPressure() const {
        if constexpr (Partitions::Count == 1) return false;
        for (std::size_t i = 0; i < Partitions::Count; ++i)
            if (Owned(i) >= Partitions::Owners(i) && DemandBudget(i) && FreshDemand(i))
                return true;
        return false;
    }
    bool ReplacementDemand(const Slot& slot, std::size_t other_owned) const {
        if constexpr (Partitions::Count == 1) return true;
        const auto partition = Partitions::Index(*slot.observation);
        return DemandBudget(partition) && FreshDemand(partition) &&
               (Owned(partition) >= Partitions::Owners(partition) ||
                other_owned >= CombinedLimit - owned);
    }
    std::array<Slot, Slots> slots;
    std::array<Observation, Observations * Partitions::Count> observations;
    std::array<Attempt, CpuAttempts> ledger;
    u64 swap{};
    std::size_t attempts{}, owned{};
    std::array<std::size_t, Partitions::Count> partition_owned{}, partition_attempts{};
    std::optional<u64> last_admission;
    bool disabled{};
};
} // namespace Vulkan::AdaptiveCpu
