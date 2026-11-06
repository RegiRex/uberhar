// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace VideoCore {

// CodexAstraLocal TEST-ONLY LOG: Bounded submission-work census for the .57
// slider/pass investigation. Remove after equivalent-work attribution is resolved.
// This owner-thread-only helper never reads GPU results or measures shader work.
// The caller must guard descriptor construction with Enabled(), and serialize
// transfers with draws. Each successful actual enqueue is recorded exactly once.
class FrameWorkloadCensus {
public:
    using U64 = std::uint64_t;
    using U32 = std::uint32_t;
    static constexpr std::size_t TargetCapacity = 16, TextureCapacity = 32,
                                 TransferCapacity = 8, TextureUnits = 3;
    static constexpr unsigned DescriptorQuota = 4, ReportClockPeriod = 64;

    enum class Delivery { Accepted, Dropped, Unknown };
    enum class Route : unsigned {
        Unknown, GpuVertex, ReadyGpuVertex, CpuVertexUnclassified, CpuVertexGenericFragment,
        CpuVertexPartialFragment, CpuVertexFullFragment, ComputeRectangle, Count
    };
    enum class TransferKind : unsigned { Unknown, Display, TextureCopy, Dma, Fill };

    struct Rect {
        std::int32_t x{}, y{}, width{}, height{};
        bool operator==(const Rect&) const = default;
    };

    // CodexAstraLocal: A known bit means that the caller actually observed the
    // corresponding values. Zero without that bit is missing, not a zero-size
    // surface. HostSamples is the actual attachment sample count, never guessed
    // from guest resolution or apparent AA. Rectangles retain their signed extent.
    struct Target {
        enum Known : U32 {
            GuestColor = 1 << 0, GuestDepth = 1 << 1, GuestExtent = 1 << 2,
            HostExtent = 1 << 3, HostSamples = 1 << 4, HostLayers = 1 << 5,
            Viewport = 1 << 6, Scissor = 1 << 7, RenderArea = 1 << 8,
            HostColorFormat = 1 << 9, HostDepthFormat = 1 << 10,
        };
        U32 known{};
        U64 color_address{}, depth_address{};
        U32 guest_width{}, guest_height{}, guest_color_format{}, guest_depth_format{};
        U32 host_width{}, host_height{}, host_samples{}, host_layers{};
        U32 host_color_format{}, host_depth_format{};
        // CodexAstraLocal: Requested area for this draw, not necessarily the
        // larger active pass extent when contained-pass reuse succeeds.
        Rect viewport{}, scissor{}, render_area{};
        bool operator==(const Target&) const = default;
    };

    // CodexAstraLocal: These are configured dimensions/mip/filter settings, not
    // sampled mip levels, texture fetches, resident bytes or shader-source hashes.
    struct Texture {
        enum Known : U32 {
            Address = 1 << 0, Extent = 1 << 1, Format = 1 << 2,
            MipRange = 1 << 3, Filters = 1 << 4, LodBias = 1 << 5, Type = 1 << 6,
        };
        U32 known{}, unit{};
        U64 address{};
        U32 width{}, height{}, format{}, type{};
        U32 mip_min{}, mip_max{}, min_filter{}, mag_filter{}, mip_filter{}, lod_bias_raw{};
        bool operator==(const Texture&) const = default;
    };
    struct Draw {
        Target target{};
        std::array<Texture, TextureUnits> textures{};
        U32 texture_mask{};
        U64 submitted_vertices{}; // Index entries for indexed draws; no invocation claim.
        Route route{Route::Unknown};
        bool indexed{};
    };

    // CodexAstraLocal: Transfer descriptors describe the existing operation's
    // address lineage/configuration. Logical bytes require a truthful caller
    // value; they are not GPU/DRAM traffic or measured bandwidth. Host cache
    // uploads need separate hooks and must never be implied by guest transfers.
    struct TransferKey {
        enum Known : U32 {
            Addresses = 1 << 0, InputExtent = 1 << 1, OutputExtent = 1 << 2,
            Formats = 1 << 3, Flags = 1 << 4, ScaleMode = 1 << 5,
        };
        TransferKind kind{TransferKind::Unknown};
        U32 known{};
        U64 source_address{}, destination_address{};
        U32 input_width{}, input_height{}, output_width{}, output_height{};
        U32 input_format{}, output_format{}, flags{}, scale_mode{};
        bool operator==(const TransferKey&) const = default;
    };
    struct Transfer {
        TransferKey key{};
        U64 logical_bytes{};
        bool logical_bytes_known{};
    };
    struct FrameIdentity {
        U64 callback_index{}, command_lists_total{}, game_submissions_total{};
        bool command_lists_known{}, game_submissions_known{}, partial{};
    };
    template <class Key> struct Descriptor {
        unsigned slot{};
        U64 generation{};
        Key key{};
    };
    struct Bin {
        U64 generation{}, operations{}, submitted_vertices{}, logical_bytes{},
            unknown_byte_operations{};
        U32 texture_slots{}; // Co-occurrence within this target/frame, not per-draw joins.
        U64 texture_overflow_bindings{};
    };
    struct RouteCounts { U64 draws{}, submitted_vertices{}; };
    struct Quality {
        U64 target_overflow_draws{}, texture_overflow_bindings{}, transfer_overflow_operations{};
        U64 invalid_routes{}, invalid_texture_mask_draws{};
        U64 evicted_pending_descriptors{}, evicted_unconfirmed_descriptors{};
        U64 sink_attempts{}, sink_known_drops{}, sink_unconfirmed{}, sink_exceptions{};
        U64 pending_descriptors{}, used_unconfirmed_descriptors{};
        bool counter_saturated{};
    };
    struct FrameReport {
        FrameIdentity identity{};
        U64 report_sequence{}, draws{}, submitted_vertices{}, indexed_draws{}, texture_bindings{};
        U64 transfer_operations{}, transfer_logical_bytes{}, transfer_unknown_byte_operations{};
        std::array<RouteCounts, static_cast<unsigned>(Route::Count)> routes{};
        std::array<Bin, TargetCapacity> targets{};
        std::array<Bin, TextureCapacity> textures{};
        std::array<Bin, TransferCapacity> transfers{};
        Quality quality{};
        unsigned descriptor_attempts{};
        // CodexAstraLocal: Sparse host duration of a PREVIOUS FinishFrame only,
        // including sink calls. Excludes per-draw bookkeeping and GPU execution.
        U64 sampled_report_sequence{}, sampled_report_host_ns{};
    };

    // CodexAstraLocal: OFF has no state allocation, clocks, sinks or scans. ON
    // performs one nothrow allocation for fixed tables; allocation failure disables
    // diagnostics, never rendering. Enabled() must guard caller-side preparation.
    explicit FrameWorkloadCensus(bool enabled) noexcept
        : requested{enabled}, state{enabled ? new (std::nothrow) State{} : nullptr} {}
    [[nodiscard]] bool Enabled() const noexcept { return state != nullptr; }
    [[nodiscard]] bool AllocationFailed() const noexcept { return requested && !state; }

    void RecordDraw(const Draw& draw) noexcept {
        if (!state) return;
        auto& s = *state;
        Add(s.frame.draws, 1);
        Add(s.frame.submitted_vertices, draw.submitted_vertices);
        if (draw.indexed) Add(s.frame.indexed_draws, 1);
        unsigned route = static_cast<unsigned>(draw.route);
        if (route >= s.frame.routes.size()) {
            route = 0;
            Add(s.frame.quality.invalid_routes, 1);
        }
        Add(s.frame.routes[route].draws, 1);
        Add(s.frame.routes[route].submitted_vertices, draw.submitted_vertices);
        const auto target = Find(s.targets, draw.target);
        if (target == TargetCapacity) Add(s.frame.quality.target_overflow_draws, 1);
        else {
            Add(s.targets[target].bin.operations, 1);
            Add(s.targets[target].bin.submitted_vertices, draw.submitted_vertices);
        }
        if (draw.texture_mask & ~((1U << TextureUnits) - 1))
            Add(s.frame.quality.invalid_texture_mask_draws, 1);
        for (unsigned unit = 0; unit < TextureUnits; ++unit) {
            if (!(draw.texture_mask & (1U << unit))) continue;
            Add(s.frame.texture_bindings, 1);
            const auto texture = Find(s.textures, draw.textures[unit]);
            if (texture == TextureCapacity) {
                Add(s.frame.quality.texture_overflow_bindings, 1);
                if (target != TargetCapacity)
                    Add(s.targets[target].bin.texture_overflow_bindings, 1);
            } else {
                Add(s.textures[texture].bin.operations, 1);
                if (target != TargetCapacity)
                    s.targets[target].bin.texture_slots |= U32{1} << texture;
            }
        }
    }

    void RecordTransfer(const Transfer& transfer) noexcept {
        if (!state) return;
        auto& s = *state;
        Add(s.frame.transfer_operations, 1);
        if (transfer.logical_bytes_known) Add(s.frame.transfer_logical_bytes, transfer.logical_bytes);
        else Add(s.frame.transfer_unknown_byte_operations, 1);
        const auto slot = Find(s.transfers, transfer.key);
        if (slot == TransferCapacity) {
            Add(s.frame.quality.transfer_overflow_operations, 1);
            return;
        }
        auto& bin = s.transfers[slot].bin;
        Add(bin.operations, 1);
        if (transfer.logical_bytes_known) Add(bin.logical_bytes, transfer.logical_bytes);
        else Add(bin.unknown_byte_operations, 1);
    }

    // CodexAstraLocal: Consume one owner callback interval, including empty ones.
    // Sinks run synchronously with const references and must copy if retaining.
    // Descriptor IDs are dictionary/slot/generation, valid across frame boundaries.
    // Missing or evicted descriptors cannot erase all-draw totals. A dropped
    // summary is visible as a report-sequence gap; delivery accounting for that
    // summary appears in the next one. No recording time/sample cutoff exists.
    template <class Sink> void FinishFrame(const FrameIdentity& identity, Sink&& sink) noexcept {
        if (!state) return;
        auto& s = *state;
        Add(s.sequence, 1);
        const bool sample = s.sequence % ReportClockPeriod == 0;
        const auto start = sample ? Clock::now() : Clock::time_point{};
        s.frame.identity = identity;
        s.frame.report_sequence = s.sequence;
        constexpr auto slots = TargetCapacity + TextureCapacity + TransferCapacity;
        unsigned emitted = 0;
        // CodexAstraLocal: Round-robin across dictionaries prevents target churn
        // from permanently starving texture/transfer definitions. At most four
        // descriptor sink calls plus one summary, regardless of draw count.
        for (unsigned scanned = 0; scanned < slots && emitted < DescriptorQuota; ++scanned) {
            const auto index = s.descriptor_cursor;
            s.descriptor_cursor = (s.descriptor_cursor + 1) % slots;
            if (index < TargetCapacity)
                emitted += Emit(s.targets[index], sink);
            else if (index < TargetCapacity + TextureCapacity)
                emitted += Emit(s.textures[index - TargetCapacity], sink);
            else emitted += Emit(s.transfers[index - TargetCapacity - TextureCapacity], sink);
        }
        s.frame.descriptor_attempts = emitted;
        CopyBins(s.targets, s.frame.targets);
        CopyBins(s.textures, s.frame.textures);
        CopyBins(s.transfers, s.frame.transfers);
        s.frame.quality.sink_attempts = s.delivery.attempts;
        s.frame.quality.sink_known_drops = s.delivery.drops;
        s.frame.quality.sink_unconfirmed = s.delivery.unconfirmed;
        s.frame.quality.sink_exceptions = s.delivery.exceptions;
        s.frame.sampled_report_sequence = s.sampled_sequence;
        s.frame.sampled_report_host_ns = s.sampled_ns;
        static_cast<void>(Deliver(sink, s.frame));
        // CodexAstraLocal: Frame counters reset even after logging failure; a
        // subsequent interval must never replay observations from the lost one.
        s.frame = {};
        ResetBins(s.targets);
        ResetBins(s.textures);
        ResetBins(s.transfers);
        if (sample) {
            s.sampled_sequence = s.sequence;
            s.sampled_ns = static_cast<U64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - start).count());
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    template <class Key> struct Slot {
        Descriptor<Key> descriptor{};
        Bin bin{};
        bool valid{}, used{}, pending{}, confirmed{};
    };
    struct DeliveryCounts { U64 attempts{}, drops{}, unconfirmed{}, exceptions{}; };
    struct State {
        std::array<Slot<Target>, TargetCapacity> targets{};
        std::array<Slot<Texture>, TextureCapacity> textures{};
        std::array<Slot<TransferKey>, TransferCapacity> transfers{};
        FrameReport frame{};
        DeliveryCounts delivery{};
        U64 sequence{}, sampled_sequence{}, sampled_ns{};
        std::size_t descriptor_cursor{};
    };
    static_assert(sizeof(State) <= 24 * 1024, "Census metadata must remain bounded");

    void Add(U64& counter, U64 value) noexcept {
        if (value > std::numeric_limits<U64>::max() - counter) {
            counter = std::numeric_limits<U64>::max();
            state->frame.quality.counter_saturated = true;
        } else counter += value;
    }

    // CodexAstraLocal: Fixed linear dictionaries, no allocation/hash/clock in a
    // draw. A slot used this interval cannot be replaced, keeping its generation
    // and target-to-texture mask stable throughout the complete interval.
    template <class Key, std::size_t N>
    std::size_t Find(std::array<Slot<Key>, N>& slots, const Key& key) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            auto& slot = slots[i];
            if (slot.valid && slot.descriptor.key == key) {
                slot.used = true;
                return i;
            }
        }
        // Prefer empty slots before evicting an unused cached descriptor.
        std::size_t candidate = N;
        for (std::size_t i = 0; i < N; ++i) {
            if (!slots[i].valid) { candidate = i; break; }
            if (!slots[i].used && candidate == N) candidate = i;
        }
        if (candidate == N) return N;
        auto& slot = slots[candidate];
        if (slot.valid && slot.pending)
            Add(state->frame.quality.evicted_pending_descriptors, 1);
        else if (slot.valid && !slot.confirmed)
            Add(state->frame.quality.evicted_unconfirmed_descriptors, 1);
        Add(slot.descriptor.generation, 1);
        slot.descriptor.slot = static_cast<unsigned>(candidate);
        slot.descriptor.key = key;
        slot.bin = {};
        slot.bin.generation = slot.descriptor.generation;
        slot.valid = slot.used = slot.pending = true;
        slot.confirmed = false;
        return candidate;
    }

    template <class Sink, class Record> Delivery Deliver(Sink& sink, const Record& record) noexcept {
        Add(state->delivery.attempts, 1);
        Delivery delivery;
        try { delivery = sink(record); }
        catch (...) {
            Add(state->delivery.exceptions, 1);
            delivery = Delivery::Dropped;
        }
        if (delivery == Delivery::Dropped) Add(state->delivery.drops, 1);
        else if (delivery != Delivery::Accepted) Add(state->delivery.unconfirmed, 1);
        return delivery;
    }
    template <class Key, class Sink> unsigned Emit(Slot<Key>& slot, Sink& sink) noexcept {
        if (!slot.valid || !slot.pending) return 0;
        const auto delivery = Deliver(sink, slot.descriptor);
        slot.pending = delivery == Delivery::Dropped;
        slot.confirmed = delivery == Delivery::Accepted;
        return 1;
    }
    template <class Key, std::size_t N>
    void CopyBins(const std::array<Slot<Key>, N>& slots, std::array<Bin, N>& bins) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (slots[i].pending) Add(state->frame.quality.pending_descriptors, 1);
            if (!slots[i].used) continue;
            bins[i] = slots[i].bin;
            if (!slots[i].confirmed) Add(state->frame.quality.used_unconfirmed_descriptors, 1);
        }
    }
    template <class Key, std::size_t N> static void ResetBins(std::array<Slot<Key>, N>& slots) noexcept {
        for (auto& slot : slots) {
            slot.bin = {};
            slot.bin.generation = slot.descriptor.generation;
            slot.used = false;
        }
    }

    const bool requested;
    std::unique_ptr<State> state;
};

} // namespace VideoCore
