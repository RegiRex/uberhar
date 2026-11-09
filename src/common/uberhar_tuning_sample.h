// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace Common {

// CodexAstraLocal: Non-destructive, opt-in scene measurements. The renderer's
// performance overlay and ordinary capture never own or reset these samples.
// Guest vblank counts align progress only; the caller must identify the same scene.
class UberharTuningSample {
public:
    static constexpr std::uint32_t BinFrames = 120, MaxFrames = 7200;
    enum class Status { Idle, Armed, Collecting, Complete, Invalid };
    struct Result {
        Status status{Status::Idle};
        std::string reason, title_id, context;
        std::uint64_t run_id{}, launch_ns{}, start_ns{}, end_ns{}, submissions{};
        std::uint32_t feature_mask{}, target_frames{}, frames{}, resolution{};
        double low{}, mean{}, high{}, p95_ms{}, p99_ms{}, limit{};
        bool pacing_limited{};
    };

    static UberharTuningSample& Instance() {
        static UberharTuningSample instance;
        return instance;
    }

    // CodexAstraLocal: Driver identity comes from the actual Vulkan instance,
    // including fallbacks, rather than the Android driver's requested label.
    void SetDriver(std::string identity) {
        std::scoped_lock lock{mutex};
        driver = std::move(identity);
    }
    void SetLibrary(std::string identity) {
        std::scoped_lock lock{mutex};
        library = std::move(identity);
    }
    void BeginRun(std::uint64_t id, std::string title, std::string context,
                  std::uint32_t features, std::uint32_t resolution, bool eligible) {
        std::scoped_lock lock{mutex};
        observing.store(false, std::memory_order_relaxed);
        result = {};
        result.run_id = id;
        // CodexAstraLocal: The process-local ordinal resets after Android kills
        // the app; a monotonic launch stamp distinguishes resumed-session runs.
        result.launch_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        result.title_id = std::move(title);
        result.context = std::move(context) + ";actual_driver=" + driver + ";library=" + library;
        result.feature_mask = features;
        result.resolution = resolution;
        running = eligible && !driver.empty();
        if (!running) result.reason = "Native/Combo Vulkan launch with identified driver required";
    }
    bool Begin(std::uint32_t count) {
        std::scoped_lock lock{mutex};
        if (!running || observing.load(std::memory_order_relaxed) ||
            count < BinFrames * 3 || count > MaxFrames || count % BinFrames) return false;
        result.status = Status::Armed;
        result.reason.clear();
        result.target_frames = count;
        result.frames = 0;
        result.submissions = 0;
        result.start_ns = result.end_ns = 0;
        result.low = result.mean = result.high = result.p95_ms = result.p99_ms = 0;
        result.limit = 0;
        result.pacing_limited = false;
        wall_sum = guest_sum = bin_wall = bin_guest = 0;
        bins = capped_bins = 0;
        anchored = false;
        observing.store(true, std::memory_order_relaxed);
        return true;
    }
    void Cancel() {
        std::scoped_lock lock{mutex};
        observing.store(false, std::memory_order_relaxed);
        result.status = Status::Idle;
        result.reason = "Cancelled";
    }
    void EndRun(std::uint64_t id) {
        std::scoped_lock lock{mutex};
        if (id != result.run_id) return;
        running = false;
        if (observing.load(std::memory_order_relaxed)) Invalidate("Game ended before scene completed");
    }
    void Pause() {
        if (!observing.load(std::memory_order_relaxed)) return;
        std::scoped_lock lock{mutex};
        // CodexAstraLocal: Arming from the paused menu starts on resume. A later
        // pause invalidates this trial; it never inserts idle time into a score.
        if (result.status == Status::Collecting) Invalidate("Paused during measured scene");
    }
    bool Observing() const { return observing.load(std::memory_order_relaxed); }

    void Observe(std::uint64_t now_ns, std::int64_t guest_us, std::uint64_t submissions,
                 std::uint32_t resolution, double limit) {
        if (!Observing()) return;
        std::scoped_lock lock{mutex};
        if (!observing.load(std::memory_order_relaxed)) return;
        if (resolution != result.resolution) {
            Invalidate("Resolution changed during trial");
            return;
        }
        if (!anchored) {
            anchored = true;
            last_ns = result.start_ns = now_ns;
            last_guest = guest_us;
            first_submissions = last_submissions = submissions;
            result.limit = limit;
            result.status = Status::Collecting;
            return;
        }
        if (limit != result.limit || !std::isfinite(limit)) {
            Invalidate("Speed cap changed during trial");
            return;
        }
        if (now_ns <= last_ns || guest_us <= last_guest || submissions < last_submissions) {
            Invalidate("Guest or host timeline discontinuity");
            return;
        }
        const auto elapsed = now_ns - last_ns;
        const auto guest_elapsed = static_cast<std::uint64_t>(guest_us - last_guest);
        intervals[result.frames++] = static_cast<double>(elapsed) / 1e6;
        wall_sum += elapsed;
        guest_sum += guest_elapsed;
        bin_wall += elapsed;
        bin_guest += guest_elapsed;
        last_ns = result.end_ns = now_ns;
        last_guest = guest_us;
        last_submissions = submissions;
        result.submissions = submissions - first_submissions;
        if (result.frames % BinFrames == 0) {
            const double speed = static_cast<double>(bin_guest) * 100000.0 / bin_wall;
            if (bins++ == 0) result.low = result.high = speed;
            result.low = std::min(result.low, speed);
            result.high = std::max(result.high, speed);
            if (limit > 0 && speed >= limit * 0.97) ++capped_bins;
            bin_wall = bin_guest = 0;
        }
        if (result.frames == result.target_frames) {
            result.mean = static_cast<double>(guest_sum) * 100000.0 / wall_sum;
            // CodexAstraLocal: A speed-limited scene cannot rank throughput.
            // Preserve its evidence, but don't recommend a setting from a tied cap.
            result.pacing_limited = capped_bins > 0;
            result.status = result.submissions ? Status::Complete : Status::Invalid;
            if (!result.submissions) result.reason = "No guest graphics submissions in scene";
            observing.store(false, std::memory_order_relaxed);
        }
    }
    Result Snapshot() const {
        Result copy;
        std::array<double, MaxFrames> sorted;
        {
            std::scoped_lock lock{mutex};
            copy = result;
            std::copy_n(intervals.begin(), copy.frames, sorted.begin());
        }
        if (copy.frames) {
            // CodexAstraLocal: Percentiles are computed only on an explicit UI
            // read, never sorted or allocated on the emulation frame path.
            std::sort(sorted.begin(), sorted.begin() + copy.frames);
            copy.p95_ms = sorted[(copy.frames * 95 - 1) / 100];
            copy.p99_ms = sorted[(copy.frames * 99 - 1) / 100];
        }
        return copy;
    }

private:
    void Invalidate(const char* reason) {
        result.status = Status::Invalid;
        result.reason = reason;
        observing.store(false, std::memory_order_relaxed);
    }
    mutable std::mutex mutex;
    std::atomic<bool> observing{false};
    bool running{}, anchored{};
    std::string driver, library;
    Result result;
    std::array<double, MaxFrames> intervals{};
    std::uint64_t last_ns{}, first_submissions{}, last_submissions{}, wall_sum{}, guest_sum{},
        bin_wall{}, bin_guest{};
    std::int64_t last_guest{};
    std::uint32_t bins{}, capped_bins{};
};
} // namespace Common
