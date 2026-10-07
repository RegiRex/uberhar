// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#include "video_core/pica/uberhar_vertex_timing.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <fmt/format.h>
#include <json.hpp>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#if defined(__linux__)
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif
#if defined(ANDROID)
#include "common/android_utils.h"
#endif

#ifndef UBERHAR_TIMING_VERSION
#define UBERHAR_TIMING_VERSION "unversioned-host-probe"
#endif

namespace Pica::VertexTiming {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::string_view, 9> StopNames{
    "none", "window_elapsed", "budget_exhausted", "arm_timeout", "identity_changed",
    "phase_changed", "clock_unavailable", "teardown", "policy_changed"};
constexpr std::array<std::string_view, 3> ModeNames{"off", "boundary", "detailed"};
constexpr std::array<std::string_view, StageCount> StageNames{
    "lookup", "input", "shader_engine_run", "output_conversion", "cache_selection_store", "submit"};

// CodexAstraLocal: Linux/Android support is explicit. Other hosts can run injected
// policy tests but never silently replace owner CPU time with wall/process time.
#if defined(__linux__)
bool ReadClock(clockid_t id, u64& value) noexcept {
    timespec stamp{};
    if (clock_gettime(id, &stamp) != 0 || stamp.tv_sec < 0 || stamp.tv_nsec < 0 ||
        stamp.tv_nsec >= 1'000'000'000 ||
        static_cast<u64>(stamp.tv_sec) > (std::numeric_limits<u64>::max() -
            static_cast<u64>(stamp.tv_nsec)) / 1'000'000'000)
        return false;
    value = static_cast<u64>(stamp.tv_sec) * 1'000'000'000 + stamp.tv_nsec;
    return true;
}
u64 Resolution(clockid_t id) noexcept {
    timespec value{};
    return clock_getres(id, &value) == 0 && value.tv_sec >= 0 && value.tv_nsec >= 0
        ? static_cast<u64>(value.tv_sec) * 1'000'000'000 + value.tv_nsec : 0;
}
#endif

u64 Hex(const Json& value) {
    if (!value.is_string()) throw std::runtime_error("hex type");
    const auto& s = value.get_ref<const std::string&>();
    if (s.size() != 16) throw std::runtime_error("hex length");
    u64 result{};
    const auto [last, error] = std::from_chars(s.data(), s.data() + s.size(), result, 16);
    if (error != std::errc{} || last != s.data() + s.size())
        throw std::runtime_error("hex value");
    return result;
}
u32 Number(const Json& root, const char* key, u32 fallback, u32 low, u32 high) {
    if (!root.contains(key)) return fallback;
    const auto& value = root.at(key);
    if (!value.is_number_unsigned()) throw std::runtime_error("integer required");
    const auto n = value.get<u64>();
    if (n < low || n > high) throw std::runtime_error("integer bound");
    return static_cast<u32>(n);
}

// CodexAstraLocal: The deterministic offset spreads coverage within an admitted
// draw; it does not turn periodic first-eligible draw selection into random CPU sampling.
u64 Mix(u64 value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

struct Writer {
    std::span<char> bytes;
    std::size_t used{};
    bool good{true};
    template <typename... Args>
    void Put(fmt::format_string<Args...> format, Args&&... args) {
        if (!good) return;
        const auto remaining = bytes.size() - used;
        const auto result = fmt::format_to_n(bytes.data() + used, remaining, format,
                                             std::forward<Args>(args)...);
        if (result.size > remaining) { good = false; return; }
        used += result.size;
    }
    void String(std::string_view value) {
        Put("\"");
        for (const unsigned char c : value) {
            if (c == '"' || c == '\\') Put("\\{}", static_cast<char>(c));
            else if (c < 32) Put("\\u{:04x}", c);
            else Put("{}", static_cast<char>(c));
        }
        Put("\"");
    }
};
} // namespace

Clock HostClock() noexcept {
    Clock result;
#if defined(__linux__)
    // CodexAstraLocal: Exactly the representation used by PicaCore's existing
    // batch_start; do not assume two separately chosen wall clocks share an epoch.
    result.wall = [](void*, u64& value) noexcept {
        const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now < 0) return false;
        value = static_cast<u64>(now);
        return true;
    };
    result.cpu = [](void*, u64& value) noexcept {
        return ReadClock(CLOCK_THREAD_CPUTIME_ID, value);
    };
    result.thread_id = [](void*) noexcept { return static_cast<u64>(syscall(SYS_gettid)); };
    result.wall_resolution_ns = Resolution(CLOCK_MONOTONIC);
    result.cpu_resolution_ns = Resolution(CLOCK_THREAD_CPUTIME_ID);
#endif
    return result;
}

// CodexAstraLocal: One flat, bounded sidecar rejects duplicates, unknown keys,
// floats/bools as numbers and path traversal before creating any diagnostic owner.
std::optional<Config> ParseConfig(std::string_view bytes, u64 title) noexcept {
    try {
        if (bytes.size() > MaxConfigBytes || !title) return {};
        std::array<std::string, 14> keys;
        u32 count{}, nodes{};
        auto root = Json::parse(bytes.begin(), bytes.end(),
            [&](int depth, Json::parse_event_t event, Json& value) {
                if (++nodes > 64 || depth > 1) throw std::runtime_error("structure bound");
                if (event == Json::parse_event_t::key) {
                    const auto& key = value.get_ref<const std::string&>();
                    if (count == keys.size() ||
                        std::find(keys.begin(), keys.begin() + count, key) != keys.begin() + count)
                        throw std::runtime_error("duplicate key");
                    keys[count++] = key;
                }
                return true;
            });
        if (!root.is_object() || !root.contains("enabled") ||
            !root.at("enabled").is_boolean() || !root.at("enabled").get<bool>()) return {};
        constexpr std::array allowed{"schema", "enabled", "mode", "diagnostic_id", "title_id",
            "trigger", "delay_ms", "duration_ms", "period_ms", "chunk_vertices", "max_chunks",
            "max_vertices", "seed"};
        for (const auto& [key, value] : root.items())
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return {};
        if (Number(root, "schema", 0, 1, 1) != 1 ||
            root.at("trigger") != "next_gameplay_transition") return {};
        Config cfg;
        const auto mode = root.at("mode").get<std::string>();
        if (mode == "boundary") cfg.mode = Mode::Boundary;
        else if (mode == "detailed") cfg.mode = Mode::Detailed;
        else return {};
        cfg.title = Hex(root.at("title_id"));
        if (cfg.title != title) return {};
        cfg.id = root.at("diagnostic_id").get<std::string>();
        if (cfg.id.empty() || cfg.id.size() > 48 ||
            !std::all_of(cfg.id.begin(), cfg.id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '-' || c == '_';
            })) return {};
        cfg.delay_ms = Number(root, "delay_ms", 0, 0, 180000);
        cfg.duration_ms = Number(root, "duration_ms", 10000, 1, 10000);
        cfg.period_ms = Number(root, "period_ms", 100, 50, 1000);
        cfg.chunk_vertices = Number(root, "chunk_vertices", 32, 1, MaxChunkVertices);
        cfg.max_chunks = Number(root, "max_chunks", 32, 1, MaxChunks);
        cfg.max_vertices = Number(root, "max_vertices", 1024, 1, MaxVertices);
        cfg.seed = Number(root, "seed", 0, 0, std::numeric_limits<u32>::max());
        return cfg;
    } catch (...) { return {}; }
}

Session::Session(Config config_, Clock clock_, std::string engine_)
    : config{std::move(config_)}, clock{clock_}, engine{std::move(engine_)} {
    // CodexAstraLocal: The public injection seam cannot bypass production caps
    // or activate an off request. Invalid configuration stays terminal/untimed.
    if ((config.mode != Mode::Boundary && config.mode != Mode::Detailed) ||
        !config.title || config.id.empty() || config.id.size() > 48 ||
        config.delay_ms > 180000 || !config.duration_ms || config.duration_ms > 10000 ||
        config.period_ms < 50 || config.period_ms > 1000 ||
        !config.chunk_vertices || config.chunk_vertices > MaxChunkVertices ||
        !config.max_chunks || config.max_chunks > MaxChunks ||
        !config.max_vertices || config.max_vertices > MaxVertices || engine.size() > 64)
        summary.stop = Stop::Policy;
}
Session::~Session() { Close(); Export(); }

std::unique_ptr<Session> Session::Load(u64 title, const char* engine) noexcept {
    try {
        const std::string file = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) +
                                 "uberhar_vertex_timing.json";
        if (!FileUtil::Exists(file)) return {};
        FileUtil::IOFile input{file, "rb"};
        if (!input.IsOpen() || input.GetSize() > MaxConfigBytes) return {};
        std::array<char, MaxConfigBytes + 1> data{};
        const auto size = input.ReadBytes(data.data(), data.size());
        if (size > MaxConfigBytes) return {};
        auto config = ParseConfig({data.data(), size}, title);
        if (!config) return {};
        // CodexAstraLocal: These adapters discard exclusive fopen mode. Disable
        // only this optional diagnostic before it can overwrite old evidence.
#if defined(_WIN32) || defined(HAVE_LIBRETRO_VFS)
        return {};
#elif defined(ANDROID)
        if (!AndroidUtils::CanUseRawFS()) return {};
#endif
        const std::string path = FileUtil::GetUserPath(FileUtil::UserPath::DumpDir) +
            "uberhar_vertex_timing/" + config->id + ".json";
        if (FileUtil::Exists(path)) return {};
        auto result = std::make_unique<Session>(std::move(*config), HostClock(), engine);
        result->path = path;
        return result;
    } catch (...) { return {}; }
}

void Session::Fail(Stop reason) noexcept {
    if (summary.stop == Stop::None) summary.stop = reason;
    measuring = false;
    if (current < summary.records) records[current].clocks_valid = false;
}
bool Session::Owner() noexcept {
    if (std::this_thread::get_id() == owner) return true;
    ++summary.identity_failures; Fail(Stop::Identity); return false;
}
bool Session::Wall(u64& value) noexcept {
    ++summary.wall_reads;
    if (clock.wall && clock.wall(clock.context, value)) return true;
    ++summary.clock_failures; Fail(Stop::Clock); return false;
}
bool Session::Cpu(u64& value) noexcept {
    ++summary.cpu_reads;
    if (clock.cpu && clock.cpu(clock.context, value)) return true;
    ++summary.clock_failures; Fail(Stop::Clock); return false;
}
bool Session::ReadPoint(Point& point) noexcept {
    if (!Wall(point.wall_before) || !Cpu(point.cpu) || !Wall(point.wall_after)) return false;
    if (point.wall_after >= point.wall_before) return true;
    ++summary.clock_failures; Fail(Stop::Clock); return false;
}

// CodexAstraLocal: Reuse the existing CPU-batch wall timestamp for selection.
// The first actual draw binds run/thread identity after PerfStats resets activity.
void Session::Poll(u64 now, Common::UberharActivity::Snapshot activity) noexcept {
    if (!Polling()) return;
    if (!initialized) {
        initialized = true; owner = std::this_thread::get_id(); first_poll = now;
        summary.run = activity.run; previous_token = activity.token;
        summary.owner_tid = clock.thread_id ? clock.thread_id(clock.context) : 0;
        if (!summary.run || !summary.owner_tid) { Fail(Stop::Identity); return; }
    }
    if (!Owner()) return;
    if (activity.run != summary.run || now < summary.last_poll_ns) {
        ++summary.identity_failures; Fail(Stop::Identity); return;
    }
    summary.last_poll_ns = now;
    ++summary.cpu_batches;
    ++summary.phases[static_cast<u32>(activity.GetPhase())];
    if (!armed) {
        if (now - first_poll >= ArmTimeoutNs) { Fail(Stop::ArmTimeout); return; }
        const bool transition = (previous_token & 3U) != 3 && (activity.token & 3U) == 3 &&
                                !(activity.token & 4U) && activity.token != previous_token;
        previous_token = activity.token;
        if (!transition) { ++summary.waiting_batches; return; }
        armed = true;
        summary.armed_at_ns = now;
        const u64 lifetime = static_cast<u64>(config.delay_ms + config.duration_ms) * 1'000'000;
        if (now > std::numeric_limits<u64>::max() - lifetime) {
            ++summary.clock_failures; Fail(Stop::Clock); return;
        }
        summary.window_begin_ns = now + static_cast<u64>(config.delay_ms) * 1'000'000;
        summary.window_end_ns = summary.window_begin_ns +
                                static_cast<u64>(config.duration_ms) * 1'000'000;
        next_chunk = summary.window_begin_ns;
    }
    if (activity.token != previous_token) {
        ++summary.phase_failures; Fail(Stop::Phase); return;
    }
    if (now >= summary.window_end_ns) { Fail(Stop::Window); return; }
    if (now >= summary.window_begin_ns) ++summary.in_window_batches;
    else ++summary.waiting_batches;
}

void Session::Unsupported() noexcept {
    if (Polling()) ++summary.unsupported_batches;
}
std::optional<Range> Session::Select(const Draw& draw) noexcept {
    if (!Polling()) return {};
    ++summary.eligible_batches;
    if (!draw.count) { ++summary.empty_batches; return {}; }
    // CodexAstraLocal: Prefix/suffix execute the enabled runner too. Bound their
    // full-draw scope separately from the much smaller number of timed inputs.
    if (draw.count > MaxDrawInputs) { ++summary.oversized_batches; return {}; }
    if (!armed || summary.last_poll_ns < summary.window_begin_ns) return {};
    if (summary.last_poll_ns < next_chunk) { ++summary.period_filtered; return {}; }
    if (summary.records >= config.max_chunks || summary.reserved_vertices >= config.max_vertices) {
        ++summary.budget_dropped; Fail(Stop::Budget); return {};
    }
    const u32 count = std::min({draw.count, config.chunk_vertices,
                               config.max_vertices - summary.reserved_vertices});
    const u32 first = static_cast<u32>(Mix(config.seed ^ summary.cpu_batches) %
                                       (static_cast<u64>(draw.count) - count + 1));
    current = summary.records++;
    auto& row = records[current];
    row.draw = draw; row.draw_ordinal = summary.cpu_batches; row.range = {first, count};
    summary.reserved_vertices += count;
    summary.selected_draw_inputs += draw.count;
    const u64 period = static_cast<u64>(config.period_ms) * 1'000'000;
    if (summary.last_poll_ns > std::numeric_limits<u64>::max() - period) {
        ++summary.clock_failures; Fail(Stop::Clock); return {};
    }
    next_chunk = summary.last_poll_ns + period;
    return row.range;
}

// CodexAstraLocal: Calibration reports measured empty envelopes without treating
// their cost as a correctable constant. It is outside every retained chunk.
bool Session::Calibrate() noexcept {
    for (u32 i = 0; i < 8; ++i) {
        Point begin{}, end{};
        if (!ReadPoint(begin) || !ReadPoint(end)) return false;
        if (end.cpu < begin.cpu || end.wall_before < begin.wall_after) {
            ++summary.clock_failures; Fail(Stop::Clock); return false;
        }
        ++calibration.pairs;
        calibration.cpu_ns += end.cpu - begin.cpu;
        calibration.outer_wall_ns += end.wall_after - begin.wall_before;
        u64 first{}, last{};
        if (!Wall(first) || !Wall(last)) return false;
        if (last < first) { ++summary.clock_failures; Fail(Stop::Clock); return false; }
        ++calibration.wall_pairs;
        calibration.wall_pair_ns += last - first;
        calibration.maximum_wall_pair_ns = std::max(calibration.maximum_wall_pair_ns, last - first);
    }
    calibrated = true;
    return true;
}

bool Session::Begin(Common::UberharActivity::Snapshot activity) noexcept {
    if (!Polling() || current >= summary.records || !Owner()) return false;
    auto& row = records[current];
    row.token_before = activity.token;
    if (activity.run != summary.run || activity.token != previous_token) {
        ++summary.phase_failures; Fail(Stop::Phase); return false;
    }
    if (!calibrated && !Calibrate()) return false;
    if (!ReadPoint(row.begin)) return false;
    if (row.begin.wall_after >= summary.window_end_ns) { Fail(Stop::Window); return false; }
    if (row.begin.wall_before < summary.window_begin_ns) {
        ++summary.clock_failures; Fail(Stop::Clock); return false;
    }
    row.start_offset_ns = row.begin.wall_before - summary.window_begin_ns;
    row.begun = row.clocks_valid = measuring = true;
    last_mark = row.begin.wall_after;
    return true;
}

// CodexAstraLocal: Detailed intervals partition only the observed loop operations.
// They include timer/counter overhead and never claim per-stage CPU time.
void Session::Mark(Stage stage) noexcept {
    if (current >= summary.records) return;
    auto& row = records[current];
    const u32 index = static_cast<u32>(stage);
    ++row.stage_calls[index];
    if (!measuring || !Detailed()) return;
    u64 now{};
    if (!Wall(now)) return;
    if (now < last_mark) { ++summary.clock_failures; Fail(Stop::Clock); return; }
    row.stage_ns[index] += now - last_mark;
    last_mark = now;
}
void Session::Input(bool hit) noexcept {
    if (current >= summary.records) return;
    auto& row = records[current];
    ++row.observed;
    ++(hit ? row.hits : row.misses);
}
void Session::InputRoute(bool fused) noexcept {
    if (current < summary.records)
        ++(fused ? records[current].fused_misses : records[current].legacy_misses);
}
void Session::End(Common::UberharActivity::Snapshot activity) noexcept {
    if (current >= summary.records) return;
    auto& row = records[current];
    row.completed = row.observed == row.range.count;
    row.token_after = activity.token;
    row.phase_stable = activity.run == summary.run && activity.token == row.token_before;
    if (measuring && Owner() && ReadPoint(row.end)) {
        row.clocks_valid = row.end.cpu >= row.begin.cpu &&
                           row.end.wall_before >= row.begin.wall_after &&
                           row.end.wall_before >= last_mark;
        if (!row.clocks_valid) { ++summary.clock_failures; Fail(Stop::Clock); }
        row.overhang = row.end.wall_after > summary.window_end_ns;
    }
    measuring = false;
    if (!row.phase_stable) { ++summary.phase_failures; Fail(Stop::Phase); }
    current = MaxChunks;
    if (Polling() && (summary.records == config.max_chunks ||
                     summary.reserved_vertices == config.max_vertices))
        Fail(Stop::Budget);
}
void Session::Close() noexcept {
    if (Polling()) Fail(Stop::Teardown);
}

std::optional<std::size_t> Session::Serialize(std::span<char> bytes) const noexcept {
    try {
        if (config.mode != Mode::Boundary && config.mode != Mode::Detailed) return {};
        Writer out{bytes.first(std::min(bytes.size(), MaxOutputBytes))};
        out.Put("{{\"schema\":1,\"diagnostic_id\":"); out.String(config.id);
        out.Put(",\"mode\":\"{}\",\"title_id\":\"{:016x}\",\"run\":{},\"owner_tid\":{},"
                "\"version\":", ModeNames[static_cast<u32>(config.mode)], config.title,
                summary.run, summary.owner_tid);
        out.String(UBERHAR_TIMING_VERSION); out.Put(",\"revision\":"); out.String(Common::g_scm_rev);
        out.Put(",\"engine\":"); out.String(engine);
        out.Put(",\"selection\":\"periodic_first_eligible_cpu_draw_hashed_contiguous_input_offset\","
                "\"scope\":\"actual_observed_no_gs_loop_only_no_extrapolation\","
                "\"clock_bias\":\"raw_instrumented_cost_no_calibration_subtraction\","
                "\"stop\":\"{}\",\"config\":{{\"delay_ms\":{},\"duration_ms\":{},"
                "\"period_ms\":{},\"chunk_vertices\":{},\"max_chunks\":{},\"max_vertices\":{},"
                "\"seed\":{},\"max_output_bytes\":{},\"max_draw_inputs\":{}}},\"clock\":{{"
                "\"cpu\":\"CLOCK_THREAD_CPUTIME_ID\",\"wall\":\"std::chrono::steady_clock\","
                "\"wall_resolution_ns\":{},\"cpu_resolution_ns\":{},"
                "\"wall_reads\":{},\"cpu_reads\":{}}},\"calibration\":{{"
                "\"pairs\":{},\"cpu_ns\":{},\"outer_wall_ns\":{},\"wall_pairs\":{},"
                "\"wall_pair_ns\":{},\"maximum_wall_pair_ns\":{}}},\"summary\":{{",
                StopNames[static_cast<u32>(summary.stop)], config.delay_ms, config.duration_ms,
                config.period_ms, config.chunk_vertices, config.max_chunks, config.max_vertices,
                config.seed, MaxOutputBytes, MaxDrawInputs, clock.wall_resolution_ns, clock.cpu_resolution_ns,
                summary.wall_reads, summary.cpu_reads, calibration.pairs, calibration.cpu_ns,
                calibration.outer_wall_ns, calibration.wall_pairs, calibration.wall_pair_ns,
                calibration.maximum_wall_pair_ns);
        out.Put("\"cpu_batches\":{},\"eligible_batches\":{},\"waiting_batches\":{},"
                "\"in_window_batches\":{},\"period_filtered\":{},\"empty_batches\":{},"
                "\"oversized_batches\":{},\"selected_draw_inputs\":{},"
                "\"unsupported_batches\":{},\"budget_dropped\":{},\"clock_failures\":{},"
                "\"identity_failures\":{},\"phase_failures\":{},\"sparse_suppressed\":{},"
                "\"reserved_vertices\":{},\"records\":{},\"armed_at_ns\":{},"
                "\"window_begin_ns\":{},\"window_end_ns\":{},\"last_poll_ns\":{},\"phases\":[",
                summary.cpu_batches, summary.eligible_batches, summary.waiting_batches,
                summary.in_window_batches, summary.period_filtered, summary.empty_batches,
                summary.oversized_batches, summary.selected_draw_inputs,
                summary.unsupported_batches, summary.budget_dropped, summary.clock_failures,
                summary.identity_failures, summary.phase_failures, summary.sparse_suppressed,
                summary.reserved_vertices, summary.records, summary.armed_at_ns,
                summary.window_begin_ns, summary.window_end_ns, summary.last_poll_ns);
        for (u32 i = 0; i < summary.phases.size(); ++i)
            out.Put("{}{}", i ? "," : "", summary.phases[i]);
        out.Put("]}},\"stage_order\":[");
        for (u32 s = 0; s < StageCount; ++s) {
            if (s) out.Put(","); out.String(StageNames[s]);
        }
        out.Put("],\"records\":[");
        for (u32 i = 0; i < summary.records; ++i) {
            const auto& r = records[i];
            out.Put("{}{{\"draw_ordinal\":{},\"program_hash\":\"{:016x}\","
                    "\"swizzle_hash\":\"{:016x}\",\"entry\":{},\"draw_count\":{},"
                    "\"vs_input_attributes\":{},\"topology\":{},\"indexed\":{},\"fused_plan\":{},"
                    "\"first_input\":{},\"input_count\":{},\"observed_inputs\":{},\"hits\":{},"
                    "\"misses\":{},\"fused_misses\":{},\"legacy_misses\":{},"
                    "\"start_offset_ns\":{},\"token_before\":{},\"token_after\":{},"
                    "\"begun\":{},\"completed\":{},\"clocks_valid\":{},\"phase_stable\":{},"
                    "\"overhang\":{},\"begin\":[{},{},{}],\"end\":[{},{},{}],\"stage_calls\":[",
                    i ? "," : "", r.draw_ordinal, r.draw.program, r.draw.swizzle, r.draw.entry,
                    r.draw.count, r.draw.inputs, r.draw.topology, r.draw.indexed, r.draw.fused,
                    r.range.first, r.range.count, r.observed, r.hits, r.misses,
                    r.fused_misses, r.legacy_misses, r.start_offset_ns,
                    r.token_before, r.token_after, r.begun, r.completed, r.clocks_valid,
                    r.phase_stable, r.overhang, r.begin.wall_before, r.begin.cpu, r.begin.wall_after,
                    r.end.wall_before, r.end.cpu, r.end.wall_after);
            for (u32 s = 0; s < StageCount; ++s)
                out.Put("{}{}", s ? "," : "", r.stage_calls[s]);
            out.Put("],\"stage_wall_ns\":[");
            for (u32 s = 0; s < StageCount; ++s)
                out.Put("{}{}", s ? "," : "", r.stage_ns[s]);
            out.Put("]}}");
        }
        out.Put("]}}\n");
        return out.good ? std::optional{out.used} : std::nullopt;
    } catch (...) { return {}; }
}

// CodexAstraLocal: Export only during teardown, after all observed operations.
// A failed or partial file is retained, never retried or overwritten.
void Session::Export() noexcept {
    if (exported || path.empty()) return;
    exported = true;
    try {
        std::array<char, MaxOutputBytes> bytes{};
        const auto size = Serialize(bytes);
        if (!size || FileUtil::Exists(path) || !FileUtil::CreateFullPath(path))
            throw std::runtime_error("timing artifact unavailable or already exists");
#if defined(_WIN32) || defined(HAVE_LIBRETRO_VFS)
        return;
#elif defined(ANDROID)
        if (!AndroidUtils::CanUseRawFS()) return;
#endif
        FileUtil::IOFile file{path, "wbx"};
        bool ok = file.IsOpen() && file.WriteBytes(bytes.data(), *size) == *size;
        ok &= file.Flush(); ok &= file.Close();
        // CodexAstraLocal Log Line: One finite opt-in retention status; no per-draw output.
        LOG_INFO(Render_Vulkan, "Uberhar vertex timing final: id={} mode={} records={} "
                 "vertices={} stop={} written={} bytes={} path={}", config.id,
                 ModeNames[static_cast<u32>(config.mode)], summary.records,
                 summary.reserved_vertices, StopNames[static_cast<u32>(summary.stop)], ok,
                 *size, path);
    } catch (...) {
        // CodexAstraLocal: Optional retention failure cannot interrupt normal game exit.
        try {
            // CodexAstraLocal Log Line: One bounded write failure, without a retry
            // or a valid-looking empty replacement for missing observations.
            LOG_WARNING(Render_Vulkan, "Uberhar vertex timing write unavailable: id={}", config.id);
        } catch (...) {
        }
    }
}
static_assert(sizeof(Session) + MaxOutputBytes < 128 * 1024);
} // namespace Pica::VertexTiming
