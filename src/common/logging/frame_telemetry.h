// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <atomic>
#include <iterator>
#include <string>
#include <fmt/format.h>
#include "common/bounded_threadsafe_queue.h"
#include "common/uberhar_frame_telemetry.h"

namespace Common::Log {
struct FrameTelemetryStatus {
    std::uint64_t attempted{}, queued{}, written{}, queue_drops{}, write_failures{},
        final_requested{}, final_written{};
};
// CodexAstraLocal: Required timing evidence has its own bounded queue, independent
// of optional Diagnostic contention, filters, instant-debug IO and the ordinary cap.
// Indefinite sink failure cannot be lossless with bounded memory: expose every failure.
class FrameTelemetryChannel {
public:
    bool Submit(FrameTelemetry::Record record) {
        record.transport_sequence = attempted.fetch_add(1, std::memory_order_relaxed) + 1;
        record.producer_drops = queue_drops.load(std::memory_order_relaxed);
        if (record.kind == FrameTelemetry::Kind::Final)
            final_requested.store(record.transport_sequence, std::memory_order_relaxed);
        if (!queue.TryEmplace(record)) {
            queue_drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        queued.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    template <typename Writer>
    void Drain(Writer&& writer) {
        if (queued.load(std::memory_order_relaxed) <= processed) return;
        FrameTelemetry::Record record;
        // CodexAstraLocal: One bounded drain per worker iteration; never starve errors.
        for (unsigned i = 0; i < 16 && queue.TryPop(record); ++i) {
            ++processed;
            if (writer(record)) {
                written.fetch_add(1, std::memory_order_relaxed);
                if (record.kind == FrameTelemetry::Kind::Final)
                    final_written.store(record.transport_sequence, std::memory_order_relaxed);
            } else write_failures.fetch_add(1, std::memory_order_relaxed);
        }
    }
    FrameTelemetryStatus Status() const {
        return {attempted.load(), queued.load(), written.load(), queue_drops.load(),
                write_failures.load(), final_requested.load(), final_written.load()};
    }
    void RecordSinkFailure() { write_failures.fetch_add(1, std::memory_order_relaxed); }
private:
    MPSCQueue<FrameTelemetry::Record, 16> queue;
    std::uint64_t processed{}; // Existing logger worker only.
    std::atomic<std::uint64_t> attempted{}, queued{}, written{}, queue_drops{}, write_failures{},
        final_requested{}, final_written{};
};

// CodexAstraLocal TEST-ONLY LOG: Compact interval evidence for performance diagnosis.
// Formatting executes only on the existing log worker. Remove with the versioned
// telemetry feature, never reinterpret these system intervals as display presents.
inline std::string FormatFrameTelemetry(const FrameTelemetry::Record& r) {
    using namespace FrameTelemetry;
    fmt::memory_buffer out;
    const auto write = [&](auto format, const auto&... values) {
        fmt::format_to(std::back_inserter(out), fmt::runtime(format), values...);
    };
    const auto endpoint = [&](const Endpoint& e) {
        write("[{},{},{}]", e.ns, e.guest_ticks, e.submissions);
    };
    const auto context = [&](const Context& c) {
        write("[{},{},{},{},{},{},{},{}]", c.generation, c.scene, c.mode,
              c.configured_resolution, c.effective_resolution, c.features,
              std::isfinite(c.limit) ? c.limit : 0, c.temporary ? 1 : 0);
    };
    const auto totals = [&](const Totals& t) {
        write("{{\"intervals\":{},\"wall_ns\":{},\"guest_ticks\":{},\"submissions\":{},"
              "\"work_ns\":{},\"speed_percent\":{},\"histogram\":[", t.intervals, t.wall_ns,
              t.guest_ticks, t.submissions, t.work_ns,
              t.wall_ns ? 100.0e9 * static_cast<double>(t.guest_ticks) /
                              GuestTicksPerSecond / static_cast<double>(t.wall_ns) : 0);
        for (std::size_t i = 0; i < t.histogram.size(); ++i)
            write("{}{}", i ? "," : "", t.histogram[i]);
        write("]}}");
    };
    const auto extremes = [&](const Extremes& e) {
        const auto bank = [&](const auto& values) {
            write("[");
            for (std::size_t i = 0; i < e.count; ++i) {
                const auto& v = values[i];
                write("{}[{},{},{},{},{},{},{},{},{},{},{}]", i ? "," : "", v.ordinal,
                      v.begin.ns, v.end.ns, v.begin.guest_ticks, v.end.guest_ticks,
                      v.begin.submissions, v.end.submissions, v.work_ns,
                      v.context_generation, v.scene, v.flags);
            }
            write("]");
        };
        write("{{\"shortest_ns\":{},\"longest_ns\":{},\"shortest6\":",
              e.count ? e.shortest[0].DurationNs() : 0, e.count ? e.longest[0].DurationNs() : 0);
        bank(e.shortest);
        write(",\"longest6\":");
        bank(e.longest);
        write("}}");
    };
    write("{{\"schema\":1,\"kind\":\"{}\",\"run_ns\":{},\"session\":{},\"title\":\"{:016X}\","
          "\"sequence\":{},\"transport_sequence\":{},\"producer_drops\":{},\"wall_clock_ns\":{},"
          "\"anchor_monotonic_ns\":{},"
          "\"guest_tick_hz\":{},\"system_intervals_not_display_presents\":true,\"flags\":{},"
          "\"excluded\":{},\"discontinuities\":{},\"context_generation\":{},\"context_text\":\"",
          r.kind == Kind::Context ? "context" : r.kind == Kind::Final ? "final" : "window",
          r.run_ns, r.session, r.title, r.sequence, r.transport_sequence, r.producer_drops, r.wall_clock_ns,
          r.anchor_monotonic_ns,
          GuestTicksPerSecond, r.flags, r.excluded, r.discontinuities, r.context_generation);
    for (const unsigned char c : r.context_text) {
        if (!c) break;
        if (c == '"' || c == '\\') write("\\{}", static_cast<char>(c));
        else if (c < 32) write("\\u{:04x}", c);
        else out.push_back(static_cast<char>(c));
    }
    write("\",\"text_truncated\":{},\"section_overflow\":{},\"overflow_intervals\":{},"
          "\"overflow_wall_ns\":{},\"overflow_guest_ticks\":{},\"totals\":",
          r.text_truncated, r.section_overflow, r.overflow_intervals,
          r.overflow_wall_ns, r.overflow_guest_ticks);
    totals(r.totals);
    write(",\"session_totals\":"); totals(r.session_totals);
    write(",\"session_extremes\":"); extremes(r.session_extremes);
    write(",\"one_second_sections\":[");
    for (std::size_t i = 0; i < r.section_count; ++i) {
        const auto& s = r.sections[i];
        write("{}{{\"sequence\":{},\"target_ns\":{},\"partial\":{},\"overshoot_ns\":{},"
              "\"missed_deadlines\":{},\"flags\":{},\"first_ordinal\":{},\"last_ordinal\":{},\"begin\":",
              i ? "," : "", s.sequence, SectionTargetNs, s.totals.wall_ns < SectionTargetNs,
              s.totals.wall_ns > SectionTargetNs ? s.totals.wall_ns - SectionTargetNs : 0,
              s.totals.wall_ns / SectionTargetNs > 0 ? s.totals.wall_ns / SectionTargetNs - 1 : 0,
              s.flags, s.first_ordinal, s.last_ordinal);
        endpoint(s.begin); write(",\"end\":"); endpoint(s.end);
        write(",\"context_begin\":"); context(s.context_begin);
        write(",\"context_end\":"); context(s.context_end);
        write(",\"totals\":"); totals(s.totals);
        write(",\"extremes\":"); extremes(s.extremes);
        write("}}");
    }
    write("]}}\n");
    return fmt::to_string(out);
}
} // namespace Common::Log
