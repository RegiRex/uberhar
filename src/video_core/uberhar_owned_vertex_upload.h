// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace VideoCore {

// CodexAstraLocal: Transfer the existing packed vector into one of four retained
// slots, swapping an old allocation back to the owner. No vertex byte is copied
// for ownership and no new worker is created. Busy/contended/full slots refuse
// immediately; the caller retains its ordinary synchronous upload path.
template <typename Vertex>
class OwnedVertexUpload {
    static constexpr std::size_t Slots = 4;
    static constexpr std::size_t MaxBytes = 8 * 1024 * 1024;
    struct State {
        struct Slot { std::vector<Vertex> vertices; bool leased{}; };
        std::mutex mutex;
        std::array<Slot, Slots> slots;
        std::size_t resident_bytes{};
        std::atomic<std::uint64_t> published{}, copy_samples{}, copy_ns{}, copy_failures{};
    };

public:
    struct Statistics {
        std::uint64_t acquired{}, refused{}, bytes{}, published{}, copy_samples{}, copy_ns{}, copy_failures{};
        std::size_t peak_resident_bytes{};
    };
    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : state{std::move(other.state)}, slot{other.slot} {}
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) { Release(); state = std::move(other.state); slot = other.slot; }
            return *this;
        }
        ~Lease() { Release(); }
        explicit operator bool() const { return bool(state); }
        std::span<const std::uint8_t> Bytes() const noexcept {
            const auto& vertices = state->slots[slot].vertices;
            return {reinterpret_cast<const std::uint8_t*>(vertices.data()), vertices.size() * sizeof(Vertex)};
        }
        // CodexAstraLocal TEST-ONLY LOG: Recorder-only copy evidence. Remove
        // after the ordinary-worker-upload experiment is qualified on hardware.
        void Published(bool success, bool sampled, std::uint64_t ns) const noexcept {
            if (success) state->published.fetch_add(1, std::memory_order_relaxed);
            else state->copy_failures.fetch_add(1, std::memory_order_relaxed);
            if (sampled) {
                state->copy_samples.fetch_add(1, std::memory_order_relaxed);
                state->copy_ns.fetch_add(ns, std::memory_order_relaxed);
            }
        }
    private:
        friend class OwnedVertexUpload;
        Lease(std::shared_ptr<State> owner, std::size_t index) : state{std::move(owner)}, slot{index} {}
        void Release() noexcept {
            if (!state) return;
            // Only metadata is changed under this lock; byte storage stays put
            // for reuse, and no copy, allocation or wait-for-worker occurs here.
            { std::lock_guard lock{state->mutex}; state->slots[slot].leased = false; }
            state.reset();
        }
        std::shared_ptr<State> state;
        std::size_t slot{};
    };

    Lease TryAcquire(std::vector<Vertex>& source) noexcept {
        if (source.empty() || source.capacity() > MaxBytes / sizeof(Vertex)) {
            ++stats.refused; return {};
        }
        try {
            if (!state) state = std::make_shared<State>();
            std::unique_lock lock{state->mutex, std::try_to_lock};
            if (!lock.owns_lock()) { ++stats.refused; return {}; }
            for (std::size_t index = 0; index < Slots; ++index) {
                auto& slot = state->slots[index];
                if (slot.leased) continue;
                const auto incoming = source.capacity() * sizeof(Vertex);
                const auto old = slot.vertices.capacity() * sizeof(Vertex);
                const auto retained = state->resident_bytes - old;
                if (incoming > MaxBytes - retained) continue;
                const auto bytes = source.size() * sizeof(Vertex);
                source.swap(slot.vertices);
                source.clear(); // Recycled allocation; submitted vertices remain in the lease.
                slot.leased = true;
                state->resident_bytes = retained + incoming;
                ++stats.acquired; stats.bytes += bytes;
                stats.peak_resident_bytes = std::max(stats.peak_resident_bytes, state->resident_bytes);
                return Lease{state, index};
            }
        } catch (...) {
            // All possibly throwing allocation/locking precedes vector swap.
            // An unavailable optional pool must leave source bytes untouched.
        }
        ++stats.refused;
        return {};
    }

    Statistics GetStatistics() const noexcept {
        auto result = stats;
        if (state) {
            result.published = state->published.load(std::memory_order_relaxed);
            result.copy_samples = state->copy_samples.load(std::memory_order_relaxed);
            result.copy_ns = state->copy_ns.load(std::memory_order_relaxed);
            result.copy_failures = state->copy_failures.load(std::memory_order_relaxed);
        }
        return result;
    }

private:
    std::shared_ptr<State> state;
    Statistics stats; // Producer only; worker counters are separate atomics above.
};

} // namespace VideoCore
