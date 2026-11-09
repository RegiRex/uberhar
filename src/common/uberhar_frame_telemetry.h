// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace Common::FrameTelemetry {
// CodexAstraLocal: Fixed storage for measured system-end intervals, not display
// presents. No allocation, formatting, or clock read occurs in this accumulator.
constexpr std::uint64_t GuestTicksPerSecond = 268111856;
constexpr std::uint64_t SectionTargetNs = 1'000'000'000;
constexpr std::size_t ExtremeCount = 6, MaxSections = 16;
enum Flag : std::uint32_t {
    Duration = 1, FiveSecondEnd = 2, Pause = 4, Final = 8,
    ContextBoundary = 16, Discontinuity = 32, SceneBoundary = 64,
};
struct Context {
    std::uint64_t generation{1}, scene{}; // 0 scene means unclassified, never inferred gameplay.
    std::uint32_t mode{}, configured_resolution{}, effective_resolution{}, features{};
    double limit{};
    bool temporary{};
    bool operator==(const Context&) const = default;
};
struct Endpoint {
    std::uint64_t ns{}, submissions{};
    std::int64_t guest_ticks{};
};
struct Interval {
    Endpoint begin{}, end{};
    std::uint64_t ordinal{}, work_ns{}, context_generation{}, scene{};
    std::uint32_t flags{};
    std::uint64_t DurationNs() const { return end.ns - begin.ns; }
};
struct Extremes {
    std::array<Interval, ExtremeCount> shortest{}, longest{};
    std::uint32_t count{};
    void Add(const Interval& value) {
        const auto insert = [&](auto& bank, bool ascending) {
            std::size_t at = 0;
            const auto size = std::min<std::size_t>(count, ExtremeCount);
            while (at < size) {
                const auto a = value.DurationNs(), b = bank[at].DurationNs();
                if ((ascending ? a < b : a > b) || (a == b && value.ordinal < bank[at].ordinal))
                    break;
                ++at;
            }
            if (at == ExtremeCount) return;
            for (std::size_t i = std::min(size, ExtremeCount - 1); i > at; --i)
                bank[i] = bank[i - 1];
            bank[at] = value;
        };
        insert(shortest, true);
        insert(longest, false);
        count = std::min<std::uint32_t>(count + 1, ExtremeCount);
    }
};
struct Totals {
    std::uint64_t intervals{}, wall_ns{}, guest_ticks{}, submissions{}, work_ns{};
    std::array<std::uint64_t, 8> histogram{};
    void Add(const Interval& value) {
        ++intervals;
        wall_ns += value.DurationNs();
        guest_ticks += static_cast<std::uint64_t>(value.end.guest_ticks - value.begin.guest_ticks);
        submissions += value.end.submissions - value.begin.submissions;
        work_ns += value.work_ns;
        constexpr std::array<std::uint64_t, 7> edges{
            16'666'667, 33'333'334, 50'000'000, 100'000'000, 250'000'000, 500'000'000, 1'000'000'000};
        std::size_t bin = 0;
        while (bin < edges.size() && value.DurationNs() >= edges[bin]) ++bin;
        ++histogram[bin];
    }
};
struct Section {
    std::uint64_t sequence{}, first_ordinal{}, last_ordinal{};
    Endpoint begin{}, end{};
    Context context_begin{}, context_end{};
    Totals totals{};
    Extremes extremes{};
    std::uint32_t flags{};
};
enum class Kind : std::uint32_t { Context, Window, Final };
struct Record {
    // CodexAstraLocal: A record is POD so the nonblocking producer copies bounded
    // data only. Context text is populated solely at settings/lifecycle boundaries.
    Kind kind{Kind::Window};
    std::uint64_t run_ns{}, session{}, title{}, sequence{}, context_generation{}, wall_clock_ns{},
        anchor_monotonic_ns{};
    std::uint64_t transport_sequence{}, producer_drops{}, excluded{}, discontinuities{};
    std::uint64_t section_overflow{}, overflow_intervals{}, overflow_wall_ns{}, overflow_guest_ticks{};
    std::uint32_t flags{}, section_count{};
    bool text_truncated{};
    std::array<char, 6144> context_text{};
    Totals totals{}, session_totals{};
    Extremes session_extremes{};
    std::array<Section, MaxSections> sections{};
};
static_assert(std::is_trivially_copyable_v<Record>);

class Accumulator {
public:
    void Observe(Endpoint now, std::uint64_t work_ns, Context context) {
        if (!anchored || now.ns <= last.ns || now.guest_ticks < last.guest_ticks ||
            now.submissions < last.submissions || !std::isfinite(context.limit) || context.limit < 0) {
            if (anchored) {
                Close(Discontinuity);
                ++discontinuities;
            }
            ++excluded;
            anchored = std::isfinite(context.limit) && context.limit >= 0;
        } else {
            const bool changed = !(context == last_context);
            const auto flags = changed ? std::uint32_t(ContextBoundary |
                (context.scene != last_context.scene ? SceneBoundary : 0)) : 0;
            if (changed) Close(flags);
            const Interval interval{last, now, ++ordinal, work_ns, last_context.generation,
                                    changed ? 0 : context.scene, flags};
            window.Add(interval);
            total.Add(interval);
            extremes.Add(interval);
            if (!section.totals.intervals) {
                section.sequence = ++section_sequence;
                section.begin = last;
                section.first_ordinal = ordinal;
                section.context_begin = last_context;
            }
            section.end = now;
            section.last_ordinal = ordinal;
            section.context_end = context;
            section.totals.Add(interval);
            section.extremes.Add(interval);
            section.flags |= flags;
            if (changed) Close(flags); // Mixed crossing retained, never silently assigned to either side.
            else if (section.totals.wall_ns >= SectionTargetNs) Close(Duration);
        }
        last = now;
        last_context = context;
    }
    void Break(std::uint32_t reason) {
        Close(reason);
        anchored = false;
    }
    void Finish(Record& record, std::uint32_t reason) {
        Close(reason);
        record.flags = reason;
        record.totals = window;
        record.session_totals = total;
        record.session_extremes = extremes;
        record.section_count = section_count;
        std::copy_n(sections.begin(), section_count, record.sections.begin());
        record.section_overflow = overflow;
        record.overflow_intervals = overflow_totals.intervals;
        record.overflow_wall_ns = overflow_totals.wall_ns;
        record.overflow_guest_ticks = overflow_totals.guest_ticks;
        record.excluded = excluded;
        record.discontinuities = discontinuities;
        window = {};
        section_count = 0;
        overflow = 0;
        overflow_totals = {};
    }
    const Totals& Total() const { return total; }
private:
    void Close(std::uint32_t reason) {
        if (!section.totals.intervals) return;
        section.flags |= reason;
        if (section_count < MaxSections) sections[section_count++] = section;
        else {
            ++overflow;
            overflow_totals.intervals += section.totals.intervals;
            overflow_totals.wall_ns += section.totals.wall_ns;
            overflow_totals.guest_ticks += section.totals.guest_ticks;
        }
        section = {};
    }
    bool anchored{};
    Endpoint last{};
    Context last_context{};
    Section section{};
    std::array<Section, MaxSections> sections{};
    std::uint32_t section_count{};
    Totals window{}, total{}, overflow_totals{};
    Extremes extremes{};
    std::uint64_t ordinal{}, section_sequence{}, excluded{}, discontinuities{}, overflow{};
};
} // namespace Common::FrameTelemetry
