// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <iterator>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string_view> // AstraEH: Settings snapshot event labels.
#include <thread>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h" // AstraEH: Bounded run/frame diagnostics.
#include "common/logging/backend.h" // CodexAstraLocal: Cap-independent compact evidence.
#include "common/scm_rev.h" // AstraEH: Identify the exact build in each run snapshot.
#include "common/settings.h"
#include "common/uberhar_test_profile.h" // CodexAstraUlt: Report effective route capabilities.
#include "common/uberhar_tuning_sample.h" // CodexAstraLocal: Independent scene trial evidence.
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

// CodexAstraLocal: Bind recommendations to the actual launch configuration.
// Candidate flags are carried separately; quality, driver and mode never drift
// silently between baseline and trial. No persistent preference is changed here.
static std::string TuningContext() {
    std::ostringstream context;
    context.precision(17);
    context << "tuner_schema=1;build=" << Common::g_scm_rev << ';';
    const auto add = [&](const char* name, auto value) {
        context << name << '=' << static_cast<double>(value) << ';';
    };
    add("uberhar_test_mode", Settings::values.uberhar_test_mode.GetValue());
    add("graphics_api", Settings::values.graphics_api.GetValue());
    add("resolution_factor", Settings::values.resolution_factor.GetValue());
    add("cpu_clock_percentage", Settings::values.cpu_clock_percentage.GetValue());
    add("is_new_3ds", Settings::values.is_new_3ds.GetValue());
    add("turnip_pass_end_flush", Settings::values.turnip_pass_end_flush.GetValue());
    add("deterministic_async_operations", Settings::values.deterministic_async_operations.GetValue());
    add("use_cpu_jit", Settings::values.use_cpu_jit.GetValue());
    add("use_shader_jit", Settings::values.use_shader_jit.GetValue());
    add("use_hw_shader", Settings::values.use_hw_shader.GetValue());
    add("uberhar_cpu_vertex_only", Settings::values.uberhar_cpu_vertex_only.GetValue());
    add("uberhar_hybrid_tev", Settings::values.uberhar_hybrid_tev.GetValue());
    add("uberhar_force_tev", Settings::values.uberhar_force_tev.GetValue());
    add("uberhar_cpu_vertex_bridge", Settings::values.uberhar_cpu_vertex_bridge.GetValue());
    add("uberhar_native_full_fragments", Settings::values.uberhar_native_full_fragments.GetValue());
    add("uberhar_optimize_generic_shaders", Settings::values.uberhar_optimize_generic_shaders.GetValue());
    add("uberhar_gpu_observer", Settings::values.uberhar_gpu_observer.GetValue());
    add("uberhar_frame_workload_census", Settings::values.uberhar_frame_workload_census.GetValue());
    add("uberhar_early_right_eye", Settings::values.uberhar_early_right_eye.GetValue());
    add("uberhar_mono_guest_3d", Settings::values.uberhar_mono_guest_3d.GetValue());
    add("uberhar_indexed_cpu_output", Settings::values.uberhar_indexed_cpu_output.GetValue());
    add("uberhar_full_pipeline_bank_16", Settings::values.uberhar_full_pipeline_bank_16.GetValue());
    add("uberhar_prune_spirv", Settings::values.uberhar_prune_spirv.GetValue());
    add("uberhar_fast_vertex_conversion", Settings::values.uberhar_fast_vertex_conversion.GetValue());
    add("uberhar_vertex_memo", Settings::values.uberhar_vertex_memo.GetValue());
    add("uberhar_worker_cpu_upload", Settings::values.uberhar_worker_cpu_upload.GetValue());
    add("uberhar_cpu_clip_reject", Settings::values.uberhar_cpu_clip_reject.GetValue());
    add("uberhar_cpu_prefix_overlap", Settings::values.uberhar_cpu_prefix_overlap.GetValue());
    add("uberhar_parallel_texture_decode", Settings::values.uberhar_parallel_texture_decode.GetValue());
    add("uberhar_cpu_execution_census", Settings::values.uberhar_cpu_execution_census.GetValue());
    add("use_disk_shader_cache", Settings::values.use_disk_shader_cache.GetValue());
    add("frame_limit", Settings::values.frame_limit.GetValue());
    add("turbo_limit", Settings::values.turbo_limit.GetValue());
    add("async_shader_compilation", Settings::values.async_shader_compilation.GetValue());
    add("async_presentation", Settings::values.async_presentation.GetValue());
    add("use_vsync", Settings::values.use_vsync.GetValue());
    add("shaders_accurate_mul", Settings::values.shaders_accurate_mul.GetValue());
    add("spirv_shader_gen", Settings::values.spirv_shader_gen.GetValue());
    add("disable_spirv_optimizer", Settings::values.disable_spirv_optimizer.GetValue());
    add("texture_filter", Settings::values.texture_filter.GetValue());
    add("texture_sampling", Settings::values.texture_sampling.GetValue());
    add("filter_mode", Settings::values.filter_mode.GetValue());
    add("custom_textures", Settings::values.custom_textures.GetValue());
    add("preload_textures", Settings::values.preload_textures.GetValue());
    add("use_skip_duplicate_frames", Settings::values.use_skip_duplicate_frames.GetValue());
    add("delay_game_render_thread_us", Settings::values.delay_game_render_thread_us.GetValue());
    add("simulate_3ds_gpu_timings", Settings::values.simulate_3ds_gpu_timings.GetValue());
    add("disable_right_eye_render", Settings::values.disable_right_eye_render.GetValue());
    add("factor_3d", Settings::values.factor_3d.GetValue());
    add("render_3d", Settings::values.render_3d.GetValue());
    add("swap_screen", Settings::values.swap_screen.GetValue());
    add("layout_option", Settings::values.layout_option.GetValue());
    return context.str();
}

static u32 TuningFeatures() {
    const auto& v = Settings::values;
    return (u32{v.uberhar_changed_state_only.GetValue()} << 0) |
           (u32{v.uberhar_reuse_contained_pass.GetValue()} << 1) |
           (u32{v.uberhar_typed_primitive_append.GetValue()} << 2) |
           (u32{v.uberhar_include_scissor.GetValue()} << 3) |
           (u32{v.uberhar_adaptive_cpu_workers.GetValue()} << 4) |
           (u32{v.uberhar_prepare_displayed_eye_only.GetValue()} << 5);
}

bool PerfStats::game_frames_updated = true;

// AstraEH: Process-local run numbers join all new performance records within one log.
static std::atomic<u64> uberhar_next_session{0};
PerfStats::PerfStats(u64 title_id) : uberhar_session{++uberhar_next_session}, title_id(title_id) {
    Common::UberharActivity::Reset(uberhar_session); // AstraEH: No cross-game annotations.
    // AstraEH Log Line: One effective-settings snapshot per emulation run.
    // AstraEH: Frame-accounting schema is independent of the renderer diagnostics version.
    LOG_INFO(
        Core,
        "Uberhar run: session={} title={:016X} frame_diagnostics=3 mode={} api={} resolution={} "
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
    LogUberharSettings("start"); // AstraEH: Human-readable context after the run header.
    Common::UberharTuningSample::Instance().BeginRun(
        uberhar_session, fmt::format("{:016X}", title_id), TuningContext(), TuningFeatures(),
        Settings::values.resolution_factor.GetValue(), Common::g_uberhar_alpha_testing &&
        Settings::IsUberharTestProfile(Settings::values.uberhar_test_mode.GetValue()) &&
        Settings::GetWorkingGraphicsAPI() == Settings::GraphicsAPI::Vulkan);
    LogFrameTelemetryContext();
}

PerfStats::~PerfStats() {
    // CodexAstraLocal: Completed scene evidence remains readable after a normal exit.
    Common::UberharTuningSample::Instance().EndRun(uberhar_session);
    // AstraEH: Final completed-frame evidence survives even when CSV/overlay options are off.
    EndUberharPause();
    LogUberharSettings("final"); // AstraEH: Final settings survive incomplete change detail.
    LogUberharFrames("final_window", uberhar_frames.Window());
    LogFrameTelemetry(Common::FrameTelemetry::Final, true);
    LogUberharFrames("totals", uberhar_frames.Total());
    // AstraEH: At most three summaries; a 400% request is distinct from achieved
    // guest speed, and changing the speed limit does not mix the normal/fast totals.
    constexpr std::array band_names{"normal", "fast", "uncapped"};
    for (std::size_t i = 0; i < band_names.size(); ++i) {
        const auto& band = uberhar_frames.Bands()[i];
        if (band.frames == 0) {
            continue;
        }
        const double seconds = band.wall_ns / 1e9;
        // AstraEH Log Line: Fixed-size lifetime speed bands, emitted only on normal shutdown.
        LOG_INFO(Core,
                 "Uberhar speed band: session={} band={} frames={} game_submissions={} "
                 "observed_wall_ms={:.3f} speed_percent={:.3f} system_fps={:.3f} "
                 "game_fps={:.3f} work_ms={:.3f} max_interval_ms={:.3f} "
                 "limit_min={} limit_max={} temporary_frames={} transition_intervals={}",
                 uberhar_session, band_names[i], band.frames, band.game_frames, band.wall_ns / 1e6,
                 band.guest_us / (seconds * 10000.0), band.frames / seconds,
                 band.game_frames / seconds, band.work_ns / 1e6, band.max_interval_ns / 1e6,
                 band.limit_min, band.limit_max, band.temporary_limit_frames,
                 uberhar_frames.LimitTransitions());
    }
    // AstraEH: Five fixed summaries separate confirmed loading from unknown/mixed time.
    for (std::size_t i = 0; i < uberhar_frames.Phases().size(); ++i) {
        const auto& data = uberhar_frames.Phases()[i];
        if (!data.frames)
            continue;
        // AstraEH Log Line: At most five lifetime phase records per normal shutdown.
        LOG_INFO(Core,
                 "Uberhar phase totals: session={} phase={} frames={} wall_ms={:.3f} "
                 "speed_percent={:.3f} max_interval_ms={:.3f} reads={} requested_bytes={}",
                 uberhar_session, Common::UberharActivity::Names[i], data.frames,
                 data.wall_ns / 1e6, data.guest_us * 100000.0 / data.wall_ns,
                 data.max_interval_ns / 1e6, data.read_requests, data.requested_bytes);
    }
    for (const auto& frame : uberhar_frames.Worst()) {
        if (frame.interval_ns == 0)
            break;
        // AstraEH Log Line: At most eight frame hitches at shutdown, including late-session stalls.
        LOG_INFO(Core,
                 "Uberhar worst frame: session={} frame={} end_ms={:.3f} interval_ms={:.3f} "
                 "work_ms={:.3f} phase={} evidence={} reads={} requested_bytes={} "
                 "submissions={} display_timing=false",
                 uberhar_session, frame.frame, frame.end_ns / 1e6, frame.interval_ns / 1e6,
                 frame.work_ns / 1e6, Common::UberharActivity::Name(frame.phase),
                 Common::UberharActivity::Evidence(frame.reads, frame.bytes, frame.submissions,
                                                   frame.interval_ns),
                 frame.reads, frame.bytes, frame.submissions);
    }
    // AstraEH: These overlap global worst records; never sum them as additional hitches.
    for (const auto& bank : uberhar_frames.PhaseWorst()) {
        for (const auto& frame : bank) {
            if (!frame.interval_ns)
                break;
            // AstraEH Log Line: At most four events per phase, preserving gameplay after long
            // loads.
            LOG_INFO(Core,
                     "Uberhar phase worst: session={} phase={} frame={} end_ms={:.3f} "
                     "interval_ms={:.3f} evidence={} display_timing=false",
                     uberhar_session, Common::UberharActivity::Name(frame.phase), frame.frame,
                     frame.end_ns / 1e6, frame.interval_ns / 1e6,
                     Common::UberharActivity::Evidence(frame.reads, frame.bytes, frame.submissions,
                                                       frame.interval_ns));
        }
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

void PerfStats::EndSystemFrame(std::chrono::microseconds guest_time, s64 guest_ticks,
                              u32 effective_resolution) {
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
    // CodexAstraLocal: Dormant tuner costs one relaxed flag read. During an opt-in
    // trial, record real guest progress independently of resettable overlay stats.
    auto& tuning = Common::UberharTuningSample::Instance();
    if (tuning.Observing()) {
        tuning.Observe(duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count(),
                       guest_time.count(), uberhar_game_frames,
                       Settings::values.resolution_factor.GetValue(), Settings::GetFrameLimit());
    }
    const u64 work_ns =
        std::max<s64>(0, duration_cast<std::chrono::nanoseconds>(frame_time).count());
    const auto activity = Common::UberharActivity::Capture();
    // CodexAstraLocal: Share the existing monotonic observation; ticks retain paired
    // guest progress rather than inferring speed from nominal frame duration.
    if (telemetry_observed_generation != telemetry_generation.load(std::memory_order_relaxed))
        LogFrameTelemetryContext();
    frame_telemetry.Observe({now_ns, uberhar_game_frames, guest_ticks}, work_ns,
        {telemetry_observed_generation, telemetry_scene.load(std::memory_order_relaxed),
         static_cast<u32>(Settings::values.uberhar_test_mode.GetValue()),
         Settings::values.resolution_factor.GetValue(), effective_resolution, TuningFeatures(),
         Settings::GetFrameLimit(), Settings::is_temporary_frame_limit});
    if (activity.token != uberhar_phase_token) {
        uberhar_phase_token = activity.token;
        if (++uberhar_phase_details <= 64) {
            // AstraEH Log Line: Bound transitions; all intervals still enter lifetime phase totals.
            LOG_INFO(Core, "Uberhar phase: session={} phase={} generation={} observed_at_ms={:.3f}",
                     uberhar_session, Common::UberharActivity::Name(activity.GetPhase()),
                     activity.token >> 3, now_ns / 1e6);
        }
    }
    uberhar_frames.Observe(now_ns, guest_time.count(), uberhar_game_frames, work_ns,
                           Settings::GetFrameLimit(), Settings::is_temporary_frame_limit, activity,
                           // AstraPro: Two small setting samples disambiguate live 2x/4x
                           // changes without per-frame text or altered speed accounting.
                           {static_cast<u32>(Settings::values.uberhar_test_mode.GetValue()),
                            Settings::values.resolution_factor.GetValue()});
    if (uberhar_frames.Window().wall_ns >= 5'000'000'000ULL) {
        LogUberharSettings("sample"); // AstraEH: No additional per-frame settings work.
        LogUberharFrames("window", uberhar_frames.Window());
        LogFrameTelemetry(Common::FrameTelemetry::FiveSecondEnd);
        uberhar_frames.ResetWindow();
    }
}

void PerfStats::EndGameFrame() {
    std::scoped_lock lock{object_mutex};

    game_frames += 1;
    ++uberhar_game_frames; // AstraEH: Not reset by overlay polling.
    Common::UberharActivity::submissions.fetch_add(1, std::memory_order_relaxed);
    PerfStats::game_frames_updated = true;
}

// AstraEH: Include completed work only. Explicit waits and the crossing frame are excluded.
void PerfStats::BeginUberharPause(const char* reason) {
    std::scoped_lock lock{object_mutex};
    Common::UberharTuningSample::Instance().Pause(); // CodexAstraLocal: No idle score pollution.
    if (uberhar_pause_start != std::chrono::steady_clock::time_point{})
        return;
    uberhar_pause_start = std::chrono::steady_clock::now();
    ++uberhar_pause_count;
    // CodexAstraLocal: Timing boundaries remain truthful after the optional pause
    // detail quota. One-second sections nest inside the same completed frame window.
    LogUberharFrames("before_pause", uberhar_frames.Window());
    LogFrameTelemetry(Common::FrameTelemetry::Pause);
    uberhar_frames.ResetWindow();
    frame_telemetry.Break(Common::FrameTelemetry::Pause);
    if (uberhar_pause_count <= 32) {
        // AstraEH Log Line: First 32 actual waits; totals retain every pause and its duration.
        LOG_INFO(Core, "Uberhar pause: session={} event=begin ordinal={} reason={}",
                 uberhar_session, uberhar_pause_count, reason);
    }
    uberhar_frames.BreakInterval();
}

void PerfStats::LogFrameTelemetryContext() {
    Common::FrameTelemetry::Record record;
    record.kind = Common::FrameTelemetry::Kind::Context;
    record.run_ns = duration_cast<std::chrono::nanoseconds>(uberhar_start.time_since_epoch()).count();
    record.session = uberhar_session;
    record.title = title_id;
    record.sequence = ++telemetry_sequence;
    record.context_generation = telemetry_generation.load(std::memory_order_relaxed);
    record.anchor_monotonic_ns = duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    record.wall_clock_ns = duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // CodexAstraLocal TEST-ONLY LOG: Resolved numeric context is serialized only
    // at launch/application boundaries. Remove with the compact telemetry feature.
    const auto text = TuningContext() + "feature_mask=" + std::to_string(TuningFeatures()) +
                      ";context_scope=resolved_values_not_backend_activation;";
    const auto length = std::min(text.size(), record.context_text.size() - 1);
    std::copy_n(text.data(), length, record.context_text.data());
    record.text_truncated = length != text.size();
    telemetry_observed_generation = record.context_generation;
    if (!Common::Log::SubmitFrameTelemetry(record)) ++telemetry_submit_failures;
}

void PerfStats::LogFrameTelemetry(u32 reason, bool final) {
    Common::FrameTelemetry::Record record;
    record.kind = final ? Common::FrameTelemetry::Kind::Final : Common::FrameTelemetry::Kind::Window;
    record.run_ns = duration_cast<std::chrono::nanoseconds>(uberhar_start.time_since_epoch()).count();
    record.session = uberhar_session;
    record.title = title_id;
    record.sequence = ++telemetry_sequence;
    record.context_generation = telemetry_observed_generation;
    frame_telemetry.Finish(record, reason);
    if (!Common::Log::SubmitFrameTelemetry(record)) ++telemetry_submit_failures;
    // CodexAstraLocal TEST-ONLY LOG: Final enqueue is not a durability claim; the
    // compact worker emits a separate written-and-flushed acknowledgment.
    if (final) LOG_INFO(Core, "Uberhar frame telemetry final: session={} sequence={} "
                       "submit_failures={} delivery=worker_ack_required",
                       uberhar_session, telemetry_sequence, telemetry_submit_failures);
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
    LogUberharSettings("resume"); // AstraEH: Catch changes made in the paused settings menu.
    if (uberhar_pause_count <= 32) {
        // AstraEH Log Line: End of the observed wait, which can also end because of shutdown.
        LOG_INFO(Core, "Uberhar pause: session={} event=end ordinal={} duration_ms={:.3f}",
                 uberhar_session, uberhar_pause_count, ns / 1e6);
    }
}

// AstraEH: Record effective settings values, not a claim that every setting is
// hot-reloaded by its backend. Between-sample changes may be missed. Existing
// per-frame limit accounting remains authoritative for fast-forward transitions.
// AstraPro: The ready-GPU policy is independent of the legacy hw_vertex setting.
void PerfStats::LogUberharSettings(const char* event) {
    const auto& values = Settings::values;
    const auto mode = values.uberhar_test_mode.GetValue();
    const auto api = Settings::GetWorkingGraphicsAPI();
    // CodexAstraUlt: Append the persisted diagnostic mode without relabeling older log modes.
    // CodexAstraLocal: Identify explicit CPU graphics in ordinary run evidence.
    // CodexAstraLocal: IDs remain stable; migrations run before session settings are logged.
    const std::array mode_names{"Custom", "Native", "Retired", "Automatic", "Retired", "Retired"};
    const std::array api_names{"Software", "OpenGL", "Vulkan"};
    const auto mode_index = static_cast<std::size_t>(mode);
    const auto api_index = static_cast<std::size_t>(api);
    const auto text = fmt::format(
        "mode={} api={} resolution_setting={} resolution_mode={} cpu_clock_percent={} "
        "cpu_jit={} hw_vertex={} hybrid={} force_tev={} bridge_requested={} disk_cache={} "
        "base_frame_limit={} turbo_limit={} async_shaders={} async_presentation={} "
        "vsync_setting={} accurate_mul={} spirv_generator={} optimizer_disabled={} "
        "texture_filter={} texture_sampling={} custom_textures={} preload_textures={} "
        "skip_duplicate_frames={} render_thread_delay_us={} simulate_gpu_timings={} "
        "ready_gpu_vertex_policy={} ready_gpu_fragment_policy={} ready_cpu_fragment_policy={} "
        "static_cpu_fragment_policy={} cpu_execution_census_requested={}",
        mode_index < mode_names.size() ? mode_names[mode_index] : "Unknown",
        api_index < api_names.size() ? api_names[api_index] : "Unknown",
        values.resolution_factor.GetValue(), values.resolution_factor.GetValue() ? "fixed" : "auto",
        values.cpu_clock_percentage.GetValue(), values.use_shader_jit.GetValue(),
        values.use_hw_shader.GetValue(), values.uberhar_hybrid_tev.GetValue(),
        values.uberhar_force_tev.GetValue(), values.uberhar_cpu_vertex_bridge.GetValue(),
        values.use_disk_shader_cache.GetValue(), values.frame_limit.GetValue(),
        values.turbo_limit.GetValue(), values.async_shader_compilation.GetValue(),
        values.async_presentation.GetValue(), values.use_vsync.GetValue(),
        values.shaders_accurate_mul.GetValue(), values.spirv_shader_gen.GetValue(),
        values.disable_spirv_optimizer.GetValue(),
        static_cast<u32>(values.texture_filter.GetValue()),
        static_cast<u32>(values.texture_sampling.GetValue()), values.custom_textures.GetValue(),
        values.preload_textures.GetValue(), values.use_skip_duplicate_frames.GetValue(),
        values.delay_game_render_thread_us.GetValue(), values.simulate_3ds_gpu_timings.GetValue(),
        // CodexAstraUlt: Replace AstraPro's Automatic-only labels with the shared policies;
        // ID4 retains GPU vertices but cannot warm/select optional specialized fragments.
        Settings::UsesReadyGpuVertices(mode) ? "independent_lists_v2" : "disabled",
        Settings::UsesReadyGpuVertices(mode)
            ? (Settings::AllowsSpecializedFragments(mode) && !values.uberhar_force_tev.GetValue()
                   ? "specialized_ready_v1" : "generic_control")
            : "disabled",
        // CodexAstraLocal: Record the configured CPU-fragment policy separately
        // from GPU vertex admission. This instant settings sample does not prove
        // compatible ready PSOs or draw coverage; renderer counters report those.
        api == Settings::GraphicsAPI::Vulkan && values.uberhar_hybrid_tev.GetValue() &&
                Settings::AllowsFullCpuFragments(mode, values.uberhar_native_full_fragments.GetValue())
            ? "software_specialized_ready_v1" : "disabled",
        // CodexAstraLocal: Native's new static-TEV tier is independent of full
        // specialization and GPU vertices. Log configured eligibility separately;
        // only renderer selections establish actual execution or performance.
        api == Settings::GraphicsAPI::Vulkan && values.uberhar_hybrid_tev.GetValue() &&
                Settings::AllowsStaticCpuTev(mode)
            ? "software_static_tev_ready_v1" : "disabled",
        // CodexAstraLocal: Existing budget startup record identifies whether a restart applied this request.
        values.uberhar_cpu_execution_census.GetValue());
    const bool changed = text != uberhar_settings;
    if (changed && !uberhar_settings.empty())
        ++uberhar_settings_changes;
    uberhar_settings = text;
    const bool final = std::string_view{event} == "final";
    if (!final && (!changed || uberhar_settings_changes > 32))
        return;
    // AstraEH Log Line: Startup, first 32 observed changes and final; no per-frame text.
    LOG_INFO(Core,
             "Uberhar settings: schema=1 session={} title={:016X} event={} "
             "observed_changes={} build={} sample_scope=instant {}",
             uberhar_session, title_id, event, uberhar_settings_changes, Common::g_scm_rev, text);
}

void PerfStats::LogUberharFrames(const char* kind,
                                 const UberharFrameDiagnostics::Counters& data) const {
    if (data.frames == 0)
        return;
    const double seconds = data.wall_ns / 1e9;
    const auto& h = data.intervals;
    // AstraEH Log Line: Once per five observed seconds, bounded pause details, and normal shutdown.
    // AstraPro Log Line: Frame-end setting ranges reveal mixed scale/mode windows;
    // these are not backend surface sizes or physical-display measurements.
    LOG_INFO(
        Core,
        "Uberhar frames {}: session={} mode={} resolution={} frame_limit={} temporary_limit={} "
        "frames={} game_submissions={} observed_wall_ms={:.3f} system_fps={:.3f} game_fps={:.3f} "
        "speed_percent={:.3f} work_ms={:.3f} max_work_ms={:.3f} max_interval_ms={:.3f} "
        "interval_bins=[{},{},{},{},{},{},{},{}] pauses={} paused_ms={:.3f} excluded={} "
        "clock_discontinuities={} display_timing=false "
        "mode_min={} mode_max={} resolution_min={} resolution_max={} "
        "render_context_changes={} render_unknown_frames={} render_context_source=frame_end_settings",
        kind, uberhar_session, static_cast<u32>(Settings::values.uberhar_test_mode.GetValue()),
        Settings::values.resolution_factor.GetValue(), Settings::GetFrameLimit(),
        Settings::is_temporary_frame_limit, data.frames, data.game_frames, data.wall_ns / 1e6,
        data.frames / seconds, data.game_frames / seconds, data.guest_us / (seconds * 10000.0),
        data.work_ns / 1e6, data.max_work_ns / 1e6, data.max_interval_ns / 1e6, h[0], h[1], h[2],
        h[3], h[4], h[5], h[6], h[7], uberhar_pause_count, uberhar_paused_ns / 1e6,
        uberhar_frames.ExcludedIntervals(), uberhar_frames.Discontinuities(), data.render_context.mode_min,
        data.render_context.mode_max, data.render_context.resolution_min,
        data.render_context.resolution_max, data.render_context.changes,
        data.render_context.unknown_frames);
    // AstraEH Log Line: Same bounded cadence; I/O is evidence, never confirmation of
    // non-interactivity.
    const auto& phases = data.phase_frames;
    LOG_INFO(Core,
             "Uberhar activity {}: session={} phase_frames=[{},{},{},{},{}] "
             "reads={} requested_bytes={} evidence={} automatic_confirmed=false",
             kind, uberhar_session, phases[0], phases[1], phases[2], phases[3], phases[4],
             data.read_requests, data.requested_bytes,
             Common::UberharActivity::Evidence(data.read_requests, data.requested_bytes,
                                               data.game_frames, data.wall_ns));
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
