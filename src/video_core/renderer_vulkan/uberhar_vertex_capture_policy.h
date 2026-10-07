// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace Vulkan::VertexCapture {

// CodexAstraLocal: These are evidence bounds, never renderer admission limits.
// Reserve 256 KiB for fixed metadata, parsing and the bounded JSON manifest;
// all game-derived payload allocations share the remaining 3.75 MiB budget.
inline constexpr std::size_t MaxBytes = 4 * 1024 * 1024;
inline constexpr std::size_t MetadataReserve = 256 * 1024;
inline constexpr std::size_t MaxPayloadBytes = MaxBytes - MetadataReserve;
inline constexpr std::size_t MaxPacketBytes = 512 * 1024;
// CodexAstraLocal: A packet includes its serialized metadata. Reserving 16 KiB
// per packet also admits eight maximal manifests without a second JSON tree.
inline constexpr std::size_t PacketMetadataReserve = 16 * 1024;
inline constexpr std::size_t MaxPacketPayloadBytes = MaxPacketBytes - PacketMetadataReserve;
inline constexpr std::size_t MaxManifestBytes = 128 * 1024;
inline constexpr std::size_t MaxConfigBytes = 8 * 1024;
inline constexpr std::uint32_t MaxPackets = 8;
inline constexpr std::uint32_t MaxRows = 128;
inline constexpr std::uint32_t MaxVertices = 4096;
inline constexpr std::uint32_t MaxAttempts = 32;
inline constexpr std::uint32_t MaxExaminedPerSwap = 1024;
inline constexpr std::size_t MaxCopiedBytes = 8 * 1024 * 1024;

// CodexAstraLocal: Count and indexed span are independent. High-base compact
// meshes are allowed; sparse endpoints and overflow never authorize a copy.
inline bool ValidShape(std::uint32_t count, std::uint32_t minimum,
                       std::uint32_t maximum) noexcept {
    return count > 0 && count <= MaxVertices && maximum >= minimum &&
           static_cast<std::uint64_t>(maximum) - minimum + 1 <= MaxVertices;
}

inline bool CanAppend(std::size_t used, std::size_t bytes, std::size_t capacity) noexcept {
    return used <= capacity && bytes <= capacity - used;
}

// CodexAstraLocal: A draw command must actually execute before a post-submit
// high-water mark can classify it. Ticks belong to one immutable session.
struct RecordedState {
    std::atomic_bool recorded{};
    std::uint64_t tick{};

    void MarkRecorded() noexcept { recorded.store(true, std::memory_order_release); }
    bool WasRecorded() const noexcept { return recorded.load(std::memory_order_acquire); }
    bool Accepted(std::optional<std::uint64_t> submitted) const noexcept {
        return tick != 0 && WasRecorded() && submitted && tick <= *submitted;
    }
    bool Completed(std::optional<std::uint64_t> submitted,
                   std::uint64_t completed) const noexcept {
        return Accepted(submitted) && tick <= completed;
    }
};

// CodexAstraLocal: Observe the manual phase itself, not generation changes made
// by frontend startup. One finite window per title session; no file polling.
class Window {
public:
    enum class State { Waiting, Delayed, Capturing, Closed };

    Window(std::uint32_t delay, std::uint32_t width, std::uint32_t initial_phase)
        : delay_left{delay}, width{width}, previous_phase{initial_phase} {}

    void NextSwap(std::uint32_t manual_phase, bool startup) noexcept {
        ++swap;
        if (state == State::Closed)
            return;
        if (state == State::Waiting) {
            const bool edge = previous_phase != 3 && manual_phase == 3 && !startup;
            previous_phase = manual_phase;
            if (!edge)
                return;
            state = State::Delayed;
        }
        if (state == State::Delayed) {
            if (delay_left) {
                --delay_left;
                return;
            }
            first_swap = swap;
            state = State::Capturing;
            return;
        }
        if (state == State::Capturing && swap - first_swap >= width)
            state = State::Closed;
    }

    void Close() noexcept { state = State::Closed; }
    bool Active() const noexcept { return state == State::Capturing; }
    State CurrentState() const noexcept { return state; }
    std::uint64_t Swap() const noexcept { return swap; }
    std::uint64_t FirstSwap() const noexcept { return first_swap; }
    std::uint32_t Interval() const noexcept {
        return Active() ? static_cast<std::uint32_t>(swap - first_swap) : 0;
    }

private:
    State state{State::Waiting};
    std::uint32_t delay_left{}, width{}, previous_phase{};
    std::uint64_t swap{}, first_swap{};
};

} // CodexAstraLocal: namespace Vulkan::VertexCapture
