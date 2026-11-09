// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <vector>

namespace VideoCore {

// CodexAstraLocal: Deduplicate only complete, already-packed CPU triangle output.
// This is a draw-local byte transformation, not a shader/FIFO cache. In particular,
// opposite quaternion variants, signed zero and NaN payloads remain distinct.
class IndexedCpuOutput {
public:
    static constexpr std::uint32_t Stride = 88;
    static constexpr std::uint32_t MinVertices = 192;
    static constexpr std::uint32_t MaxVertices = 32766;
    static constexpr std::uint32_t PrefixVertices = 48;
    static constexpr std::uint32_t MaxProbes = 32;

    enum class Decision : std::uint8_t {
        Accepted,
        Shape,
        Small,
        Large,
        PrefixReuse,
        Savings,
        ProbeBudget,
        Allocation,
        Count,
    };

    struct Plan {
        Decision decision = Decision::Shape;
        std::uint32_t input_vertices = 0;
        std::uint32_t unique_vertices = 0;
        std::uint32_t expanded_bytes = 0;
        std::uint32_t index_offset = 0;
        std::uint32_t upload_bytes = 0;
        std::uint32_t hashed_vertices = 0;
        std::uint32_t probes = 0;

        explicit operator bool() const { return decision == Decision::Accepted; }
    };

    // CodexAstraLocal: Caller supplies the existing production XXH3 hash. Hash
    // collisions never establish identity: every match also compares all 88 bytes.
    // Prefix refusal is deliberately conservative; missing a profitable draw is
    // safe, while hashing every no-reuse draw would add unbounded practical cost.
    template <typename Hash>
    Plan Prepare(std::span<const std::uint8_t> bytes, Hash&& hash) noexcept {
        prepared = {};
        prepared_source = nullptr;
        if (bytes.size() % (Stride * 3) != 0) return prepared;
        const auto count = bytes.size() / Stride;
        if (count < MinVertices) return Refuse(Decision::Small);
        if (count > MaxVertices) return Refuse(Decision::Large);
        prepared.input_vertices = static_cast<std::uint32_t>(count);
        prepared.expanded_bytes = static_cast<std::uint32_t>(bytes.size());
        try {
            EnsureStorage(prepared.input_vertices);
        } catch (const std::bad_alloc&) {
            return Refuse(Decision::Allocation);
        }
        if (++generation == 0) {
            for (auto& entry : table) entry.generation = 0;
            generation = 1;
        }
        const auto mask = static_cast<std::uint32_t>(table.size() - 1);
        // CodexAstraLocal: Admission requires both 12.5% and 1 KiB saved after
        // index bytes. This is a transfer break-even gate, not a claimed CPU win.
        const auto required_saving = std::max<std::uint32_t>(1024, prepared.expanded_bytes / 8);
        for (std::uint32_t source = 0; source < count; ++source) {
            const auto* vertex = bytes.data() + source * Stride;
            const std::uint64_t key = hash(vertex, Stride);
            ++prepared.hashed_vertices;
            auto slot = static_cast<std::uint32_t>(key) & mask;
            bool inserted = false;
            for (std::uint32_t probe = 0; probe < MaxProbes; ++probe, slot = (slot + 1) & mask) {
                ++prepared.probes;
                auto& entry = table[slot];
                if (entry.generation != generation) {
                    entry = {key, prepared.unique_vertices, generation};
                    unique_sources[prepared.unique_vertices] = source;
                    indices[source] = static_cast<std::uint16_t>(prepared.unique_vertices++);
                    inserted = true;
                    break;
                }
                if (entry.hash == key &&
                    std::memcmp(vertex, bytes.data() + unique_sources[entry.unique] * Stride,
                                Stride) == 0) {
                    indices[source] = static_cast<std::uint16_t>(entry.unique);
                    inserted = true;
                    break;
                }
            }
            if (!inserted) return Refuse(Decision::ProbeBudget);
            if (source + 1 == PrefixVertices && prepared.unique_vertices > PrefixVertices * 7 / 8)
                return Refuse(Decision::PrefixReuse);
            // Unique count never decreases: stop as soon as even perfect reuse
            // of every remaining corner cannot meet the final savings bound.
            if (prepared.unique_vertices * Stride + count * sizeof(std::uint16_t) >
                prepared.expanded_bytes - required_saving)
                return Refuse(Decision::Savings);
        }
        // Stride is divisible by four, so the appended uint16 index region is
        // naturally aligned inside the caller's same 88-aligned reservation.
        static_assert(Stride % alignof(std::uint16_t) == 0);
        prepared.index_offset = prepared.unique_vertices * Stride;
        prepared.upload_bytes = prepared.index_offset + prepared.input_vertices * sizeof(std::uint16_t);
        prepared.decision = Decision::Accepted;
        prepared_source = bytes.data();
        return prepared;
    }

    // CodexAstraLocal: Synchronous copy only. Neither CPU spans nor this scratch
    // plan are captured by the command recorder; it receives GPU offsets alone.
    bool Copy(std::span<const std::uint8_t> source, std::span<std::uint8_t> destination) const noexcept {
        if (!prepared || source.data() != prepared_source ||
            source.size() != prepared.expanded_bytes || destination.size() < prepared.upload_bytes)
            return false;
        for (std::uint32_t unique = 0; unique < prepared.unique_vertices; ++unique)
            std::memcpy(destination.data() + unique * Stride,
                        source.data() + unique_sources[unique] * Stride, Stride);
        std::memcpy(destination.data() + prepared.index_offset, indices.data(),
                    prepared.input_vertices * sizeof(std::uint16_t));
        return true;
    }

    std::size_t ScratchBytes() const noexcept {
        return table.size() * sizeof(Entry) + unique_sources.size() * sizeof(std::uint32_t) +
               indices.size() * sizeof(std::uint16_t);
    }

private:
    struct Entry {
        std::uint64_t hash = 0;
        std::uint32_t unique = 0;
        std::uint32_t generation = 0;
    };

    Plan Refuse(Decision reason) noexcept {
        prepared.decision = reason;
        prepared_source = nullptr;
        return prepared;
    }

    void EnsureStorage(std::uint32_t count) {
        std::uint32_t capacity = 1;
        while (capacity < count) capacity <<= 1;
        // CodexAstraLocal: Power-of-two storage grows only to 32768 vertices and
        // 65536 slots (1.19 MiB total); generations avoid clearing it every draw.
        // Fresh vectors prevent unspecified geometric over-reservation on growth.
        if (table.size() < capacity * 2) {
            std::vector<Entry>(capacity * 2).swap(table);
            generation = 0;
        }
        if (unique_sources.size() < capacity) std::vector<std::uint32_t>(capacity).swap(unique_sources);
        if (indices.size() < capacity) std::vector<std::uint16_t>(capacity).swap(indices);
    }

    std::vector<Entry> table;
    std::vector<std::uint32_t> unique_sources;
    std::vector<std::uint16_t> indices;
    std::uint32_t generation = 0;
    Plan prepared;
    const std::uint8_t* prepared_source = nullptr;
};

} // namespace VideoCore
