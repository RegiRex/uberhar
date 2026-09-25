// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <iterator>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h" // AstraEH: Bounded run/frame diagnostics.
#include "common/settings.h"
#include "core/core_timing.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"

using namespace std::chrono_literals;
using DoubleSecs = std::chrono::duration<double, std::chrono::seconds::period>;
using std::chrono::duration_cast;
using std::chrono::microseconds;

constexpr double FRAME_LENGTH = 1.0 / SCREEN_REFRESH_RATE;
// Purposefully ignore the first five frames, as there's a significant amount of overhead in
// booting that we shouldn't account for
constexpr std::size_t IgnoreFrames = 5;

namespace Core {

bool PerfStats::game_frames_updated = true;

// AstraEH: Process-local run numbers join all new performance records within one log.
static std::atomic<u64> uberhar_next_session{0};
PerfStats::PerfStats(u64 title_id) : uberhar_session{++uberhar_next_session}, title_id(title_id) {
    // AstraEH Log Line: One effective-settings snapshot per emulation run.
    LOG_INFO(
        Core,
        "Uberhar run: session={} title={:016X} diagnostics=9 mode={} api={} resolution={} "
        "cpu_jit={} hw_vertex={} hybrid={} force_tev={} bridge_requested={} disk_cache={} "
        "frame_limit={} "
        "cpu_clock_percent={}",
        uberhar_session, title_id, static_cast<u32>(Settings::values.uberhar_test_mode.GetValue()),
        static_cast<u32>(Settings::GetWorkingGraphicsAPI()),
        Settings::values.resolution_factor.GetValue(), Settings::values.use_shader_jit.GetValue(),
        Settings::values.use_hw_shader.GetValue(), Settings::values.uberhar_hybrid_tev.GetValue(),
        Settings::values.uberhar_force_tev.GetValue(),
        Settings::values.uberhar_cpu_vertex_bridge.GetValue(),
        Settings::values.use_disk_shader_cache.GetValue(), Settings::GetFrameLimit(),
        Settings::values.cpu_clock_percentage.GetValue());
}

PerfStats::~PerfStats() {
    // AstraEH: Final completed-frame evidence survives even when CSV/overlay options are off.
    EndUberharPause();
    LogUberharFrames("final_window", uberhar_frames.Window());
    LogUberharFrames("totals", uberhar_frames.Total());
    for (const auto& frame : uberhar_frames.Worst()) {
        if (frame.interval_ns == 0)
            break;
        // AstraEH Log Line: At most eight frame hitches at shutdown, including late-session stalls.
        LOG_INFO(Core,
                 "Uberhar worst frame: session={} frame={} end_ms={:.3f} interval_ms={:.3f} "
                 "work_ms={:.3f} display_timing=false",
                 uberhar_session, frame.frame, frame.end_ns / 1e6, frame.interval_ns / 1e6,
                 frame.work_ns / 1e6);
    }
    // AstraEH Log Line: Always close the run, including an exit before the first sampled frame.
    LOG_INFO(
        Core,
        "Uberhar run end: session={} lifetime_ms={:.3f} observed_frames={} pauses={} "
        "paused_ms={:.3f} excluded={}",
        uberhar_session,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uberhar_start)
            .count(),
        uberhar_frames.Total().frames, uberhar_pause_count, uberhar_paused_ns / 1e6,
        uberhar_frames.ExcludedIntervals());
    if (!Settings::values.record_frame_times || title_id == 0) {
        return;
    }

    const std::time_t t = std::time(nullptr);
    std::ostringstream stream;
    std::copy(perf_history.begin() + IgnoreFrames, perf_history.begin() + current_index,
              std::ostream_iterator<double>(stream, "\n"));
    const std::string& path = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
    // %F Date format expanded is "%Y-%m-%d"
    const std::string filename =
        fmt::format("{}/{:%F-%H-%M}_{:016X}.csv", path, *std::localtime(&t), title_id);
    FileUtil::IOFile file(filename, "w");
    file.WriteString(stream.str());
}

void PerfStats::BeginSVCProcessing() {
    start_svc_time = Clock::now();
}

void PerfStats::EndSVCProcessing() {
    accumulated_svc_time += (Clock::now() - start_svc_time);
}

void PerfStats::BeginIPCProcessing() {
    start_ipc_time = Clock::now();
}

void PerfStats::EndIPCProcessing() {
    accumulated_ipc_time += (Clock::now() - start_ipc_time);
}

void PerfStats::BeginGPUProcessing() {
    start_gpu_time = Clock::now();
}

void PerfStats::EndGPUProcessing() {
    accumulated_gpu_time += (Clock::now() - start_gpu_time);
}

void PerfStats::StartSwap() {
    start_swap_time = Clock::now();
}

void PerfStats::EndSwap() {
    accumulated_swap_time += (Clock::now() - start_swap_time);
}

void PerfStats::BeginSystemFrame() {
    std::scoped_lock lock{object_mutex};

    frame_begin = Clock::now();
}

void PerfStats::EndSystemFrame(std::chrono::microseconds guest_time) {
    std::scoped_lock lock{object_mutex};

    auto frame_end = Clock::now();
    const auto frame_time = frame_end - frame_begin;
    if (current_index < perf_history.size()) {
        perf_history[current_index++] =
            std::chrono::duration<double, std::milli>(frame_time).count();
    }
    accumulated_frametime += frame_time;
    system_frames += 1;

    // TODO: Track previous frame times in a less stupid way. -OS
    previous_previous_frame_length = previous_frame_length;

    previous_frame_length = frame_end - previous_frame_end;
    previous_frame_end = frame_end;

    // AstraEH: One monotonic clock read per system frame; no dynamic storage or per-frame text.
    const auto now = std::chrono::steady_clock::now();
    const u64 now_ns = duration_cast<std::chrono::nanoseconds>(now - uberhar_start).count();
    const u64 work_ns =
        std::max<s64>(0, duration_cast<std::chrono::nanoseconds>(frame_time).count());
    uberhar_frames.Observe(now_ns, guest_time.count(), uberhar_game_frames, work_ns,
                           Settings::GetFrameLimit(), Settings::is_temporary_frame_limit);
    if (uberhar_frames.Window().wall_ns >= 5'000'000'000ULL) {
        LogUberharFrames("window", uberhar_frames.Window());
        uberhar_frames.ResetWindow();
    }
}

void PerfStats::EndGameFrame() {
    std::scoped_lock lock{object_mutex};

    game_frames += 1;
    ++uberhar_game_frames; // AstraEH: Not reset by overlay polling.
    PerfStats::game_frames_updated = true;
}

// AstraEH: Include completed work only. Explicit waits and the crossing frame are excluded.
void PerfStats::BeginUberharPause(const char* reason) {
    std::scoped_lock lock{object_mutex};
    if (uberhar_pause_start != std::chrono::steady_clock::time_point{})
        return;
    uberhar_pause_start = std::chrono::steady_clock::now();
    ++uberhar_pause_count;
    if (uberhar_pause_count <= 32) {
        LogUberharFrames("before_pause", uberhar_frames.Window());
        uberhar_frames.ResetWindow();
        // AstraEH Log Line: First 32 actual waits; totals retain every pause and its duration.
        LOG_INFO(Core, "Uberhar pause: session={} event=begin ordinal={} reason={}",
                 uberhar_session, uberhar_pause_count, reason);
    }
    uberhar_frames.BreakInterval();
}

void PerfStats::EndUberharPause() {
    std::scoped_lock lock{object_mutex};
    if (uberhar_pause_start == std::chrono::steady_clock::time_point{})
        return;
    const u64 ns = duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                           uberhar_pause_start)
                       .count();
    uberhar_paused_ns += ns;
    uberhar_pause_start = {};
    if (uberhar_pause_count <= 32) {
        // AstraEH Log Line: End of the observed wait, which can also end because of shutdown.
        LOG_INFO(Core, "Uberhar pause: session={} event=end ordinal={} duration_ms={:.3f}",
                 uberhar_session, uberhar_pause_count, ns / 1e6);
    }
}

void PerfStats::LogUberharFrames(const char* kind,
                                 const UberharFrameDiagnostics::Counters& data) const {
    if (data.frames == 0)
        return;
    const double seconds = data.wall_ns / 1e9;
    const auto& h = data.intervals;
    // AstraEH Log Line: Once per five observed seconds, bounded pause details, and normal shutdown.
    LOG_INFO(
        Core,
        "Uberhar frames {}: session={} mode={} resolution={} frame_limit={} temporary_limit={} "
        "frames={} game_submissions={} observed_wall_ms={:.3f} system_fps={:.3f} game_fps={:.3f} "
        "speed_percent={:.3f} work_ms={:.3f} max_work_ms={:.3f} max_interval_ms={:.3f} "
        "interval_bins=[{},{},{},{},{},{},{},{}] pauses={} paused_ms={:.3f} excluded={} "
        "clock_discontinuities={} display_timing=false",
        kind, uberhar_session, static_cast<u32>(Settings::values.uberhar_test_mode.GetValue()),
        Settings::values.resolution_factor.GetValue(), Settings::GetFrameLimit(),
        Settings::is_temporary_frame_limit, data.frames, data.game_frames, data.wall_ns / 1e6,
        data.frames / seconds, data.game_frames / seconds, data.guest_us / (seconds * 10000.0),
        data.work_ns / 1e6, data.max_work_ns / 1e6, data.max_interval_ns / 1e6, h[0], h[1], h[2],
        h[3], h[4], h[5], h[6], h[7], uberhar_pause_count, uberhar_paused_ns / 1e6,
        uberhar_frames.ExcludedIntervals(), uberhar_frames.Discontinuities());
    // AstraEH Log Line: Same report cadence; identifies windows that include fast-forward changes.
    LOG_INFO(Core, "Uberhar frame limits {}: session={} min={} max={} temporary_frames={}", kind,
             uberhar_session, data.limit_min, data.limit_max, data.temporary_limit_frames);
}

double PerfStats::GetMeanFrametime() const {
    std::scoped_lock lock{object_mutex};

    if (current_index <= IgnoreFrames) {
        return 0;
    }

    const double sum = std::accumulate(perf_history.begin() + IgnoreFrames,
                                       perf_history.begin() + current_index, 0.0);
    return sum / static_cast<double>(current_index - IgnoreFrames);
}

PerfStats::Results PerfStats::GetAndResetStats(microseconds current_system_time_us) {
    std::scoped_lock lock{object_mutex};

    const auto now = Clock::now();
    // Walltime elapsed since stats were reset
    const auto interval = duration_cast<DoubleSecs>(now - reset_point).count();

    const auto system_us_per_second = (current_system_time_us - reset_point_system_us) / interval;

    last_stats.system_fps = static_cast<double>(system_frames) / interval;
    last_stats.game_fps = static_cast<double>(game_frames) / interval;
    last_stats.time_vblank_interval =
        system_frames ? (duration_cast<DoubleSecs>(accumulated_frametime).count() /
                         static_cast<double>(system_frames))
                      : 0;
    last_stats.time_hle_svc =
        system_frames
            ? (duration_cast<DoubleSecs>(accumulated_svc_time - accumulated_ipc_time).count() /
               static_cast<double>(system_frames))
            : 0;
    last_stats.time_hle_ipc =
        system_frames
            ? (duration_cast<DoubleSecs>(accumulated_ipc_time - accumulated_gpu_time).count() /
               static_cast<double>(system_frames))
            : 0;
    last_stats.time_gpu = system_frames ? (duration_cast<DoubleSecs>(accumulated_gpu_time).count() /
                                           static_cast<double>(system_frames))
                                        : 0;
    last_stats.time_swap = system_frames
                               ? (duration_cast<DoubleSecs>(accumulated_swap_time).count() /
                                  static_cast<double>(system_frames))
                               : 0;

    last_stats.time_remaining =
        system_frames ? (duration_cast<DoubleSecs>((accumulated_frametime - accumulated_svc_time) -
                                                   accumulated_swap_time)
                             .count() /
                         static_cast<double>(system_frames))
                      : 0;
    last_stats.emulation_speed = system_us_per_second.count() / 1'000'000.0;
    last_stats.artic_transmitted = static_cast<double>(artic_transmitted) / interval;
    last_stats.artic_events.raw = artic_events.raw | prev_artic_event.raw;

    // Reset counters
    reset_point = now;
    reset_point_system_us = current_system_time_us;
    accumulated_frametime = Clock::duration::zero();
    system_frames = 0;
    accumulated_svc_time = Clock::duration::zero();
    accumulated_ipc_time = Clock::duration::zero();
    accumulated_gpu_time = Clock::duration::zero();
    accumulated_swap_time = Clock::duration::zero();
    game_frames = 0;
    artic_transmitted = 0;
    prev_artic_event.raw &= artic_events.raw;

    return last_stats;
}

PerfStats::Results PerfStats::GetLastStats() {
    std::scoped_lock lock{object_mutex};

    return last_stats;
}

double PerfStats::GetLastFrameTimeScale() const {
    std::scoped_lock lock{object_mutex};

    return duration_cast<DoubleSecs>(previous_frame_length).count() / FRAME_LENGTH;
}

double PerfStats::GetStableFrameTimeScale() const {
    std::scoped_lock lock{object_mutex};

    const double stable_previous_frame_length =
        (duration_cast<DoubleSecs>(previous_frame_length).count() +
         duration_cast<DoubleSecs>(previous_previous_frame_length).count()) /
        2;
    return stable_previous_frame_length / FRAME_LENGTH;
}

void FrameLimiter::WaitOnce() {
    if (frame_advancing_enabled) {
        // Frame advancing is enabled: wait on event instead of doing framelimiting
        frame_advance_event.Wait();
        frame_advance_event.Reset();
    }
}

void FrameLimiter::DoFrameLimiting(microseconds current_system_time_us) {
    if (frame_advancing_enabled) {
        // Frame advancing is enabled: wait on event instead of doing framelimiting
        frame_advance_event.Wait();
        frame_advance_event.Reset();
        return;
    }

    auto now = Clock::now();
    double sleep_scale = Settings::GetFrameLimit() / 100.0;

    if (Settings::GetFrameLimit() == 0) {
        return;
    }

    // Max lag caused by slow frames. Shouldn't be more than the length of a frame at the current
    // speed percent or it will clamp too much and prevent this from properly limiting to that
    // percent. High values means it'll take longer after a slow frame to recover and start limiting
    const microseconds max_lag_time_us = duration_cast<microseconds>(
        std::chrono::duration<double, std::chrono::microseconds::period>(25ms / sleep_scale));
    frame_limiting_delta_err += duration_cast<microseconds>(
        std::chrono::duration<double, std::chrono::microseconds::period>(
            (current_system_time_us - previous_system_time_us) / sleep_scale));
    frame_limiting_delta_err -= duration_cast<microseconds>(now - previous_walltime);
    frame_limiting_delta_err =
        std::clamp(frame_limiting_delta_err, -max_lag_time_us, max_lag_time_us);

    if (frame_limiting_delta_err > microseconds::zero()) {
        std::this_thread::sleep_for(frame_limiting_delta_err);
        auto now_after_sleep = Clock::now();
        frame_limiting_delta_err -= duration_cast<microseconds>(now_after_sleep - now);
        now = now_after_sleep;
    }

    previous_system_time_us = current_system_time_us;
    previous_walltime = now;
}

bool FrameLimiter::IsFrameAdvancing() const {
    return frame_advancing_enabled;
}

void FrameLimiter::SetFrameAdvancing(bool value) {
    const bool was_enabled = frame_advancing_enabled.exchange(value);
    if (was_enabled && !value) {
        // Set the event to let emulation continue
        frame_advance_event.Set();
    }
}

void FrameLimiter::AdvanceFrame() {
    frame_advance_event.Set();
}

} // namespace Core
