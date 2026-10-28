// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "common/settings.h"
#include "right_eye_disabler.h"
#include "video_core/gpu.h"
#include "video_core/gpu_impl.h"

namespace VideoCore {
// CodexAstraLocal TEST-ONLY LOG: Remove the bounded observer after the owner completes
// Native/Combo suppression comparisons. This does not change the inherited heuristic.
RightEyeDisabler::~RightEyeDisabler() {
    ReportDiagnostics(true);
}

void RightEyeDisabler::ReportDiagnostics(bool final) noexcept {
    const bool requested = Settings::values.disable_right_eye_render.GetValue();
    const auto now = std::chrono::steady_clock::now();
    const bool changed = !diagnostics_reported || requested != last_reported_requested ||
                         enabled != last_reported_allowed;
    if (!final && !changed && now - last_diagnostic_report < std::chrono::seconds{5}) {
        return;
    }
    diagnostics_reported = true;
    last_reported_requested = requested;
    last_reported_allowed = enabled;
    last_diagnostic_report = now;
    // CodexAstraLocal TEST-ONLY LOG: Optional periodic records must not stall rendering under
    // logger load. Preserve state/final records, but contain allocation/format failures so this
    // observer cannot interrupt heuristic state updates or terminate GPU destruction.
    try {
        const auto delivery = final || changed ? Common::Log::Delivery::Reliable
                                              : Common::Log::Delivery::Diagnostic;
        LOG_INFO_WITH_DELIVERY(
            Render, delivery,
            "Right-eye heuristic: requested={} compatibility_allowed={} frame_active={} "
            "callbacks={} lists_seen={} lists_evaluated={} lists_skipped={} "
            "transfers_seen={} transfers_evaluated={} transfers_skipped={} "
            "last_guessed_viewport_height={:08X} last_guessed_color_buffer={:08X} final={}",
            requested, enabled, enabled && enable_for_frame, end_frame_callbacks,
            command_lists_seen, command_lists_evaluated, command_lists_skipped,
            display_transfers_seen, display_transfers_evaluated, display_transfers_skipped,
            last_guessed_viewport_height, last_guessed_color_buffer, final);
    } catch (...) {
        // CodexAstraLocal TEST-ONLY LOG: Dropping observer output is preferable to changing gameplay.
    }
}

bool RightEyeDisabler::ShouldAllowCmdQueueTrigger(PAddr addr, u32 size) {
    // CodexAstraLocal TEST-ONLY LOG: Count decision calls even with suppression OFF; remove
    // these counters after owner comparison. No per-command clock or logging work is added.
    ++command_lists_seen;
    if (!enabled || !enable_for_frame)
        return true;
    ++command_lists_evaluated;

    constexpr u32 top_screen_size = 0x00469000;

    if (report_end_frame_pending) {
        ReportEndFrame();
        report_end_frame_pending = false;
    }
    cmd_queue_trigger_happened = true;

    auto guess = gpu.impl->pica.GuessCmdRenderProperties(addr, size);
    // CodexAstraLocal TEST-ONLY LOG: Reuse the existing guess; it is not proof of an eye or duplicate.
    last_guessed_viewport_height = guess.vp_height;
    last_guessed_color_buffer = guess.paddr;
    if (guess.vp_height == top_screen_size && !top_screen_blocked) {
        if (top_screen_buf == 0) {
            top_screen_buf = guess.paddr;
        }
        top_screen_drawn = true;
        if (top_screen_transfered) {
            cmd_trigger_blocked = true;
            // CodexAstraLocal TEST-ONLY LOG: Count actual whole-list suppression decisions.
            ++command_lists_skipped;
            return false;
        }
    }

    cmd_trigger_blocked = false;
    return true;
}
bool RightEyeDisabler::ShouldAllowDisplayTransfer(PAddr src_address, size_t size) {
    // CodexAstraLocal TEST-ONLY LOG: Count transfer decisions; size is the caller's input
    // height, not bytes. Remove with the comparison observer, preserving all decisions.
    ++display_transfers_seen;
    if (!enabled || !enable_for_frame)
        return true;
    ++display_transfers_evaluated;

    if (size >= 400 && !top_screen_blocked) {
        if (top_screen_drawn && src_address == top_screen_buf) {
            top_screen_transfered = true;
        }

        if (src_address == top_screen_buf && cmd_trigger_blocked) {
            top_screen_blocked = true;
            // CodexAstraLocal TEST-ONLY LOG: Count actual display-transfer suppression decisions.
            ++display_transfers_skipped;
            return false;
        }
    }

    if (cmd_queue_trigger_happened)
        display_tranfer_happened = true;
    return true;
}
void RightEyeDisabler::ReportEndFrame() {
    // CodexAstraLocal TEST-ONLY LOG: Observe even compatibility-disallowed titles. This callback
    // also runs from a pending command-list fallback, so its count is not completed frames.
    ++end_frame_callbacks;
    if (enabled) enable_for_frame = Settings::values.disable_right_eye_render.GetValue();
    ReportDiagnostics();
    if (!enabled)
        return;

    if (display_tranfer_happened) {
        top_screen_drawn = false;
        top_screen_transfered = false;
        top_screen_blocked = false;
        cmd_queue_trigger_happened = false;
        cmd_trigger_blocked = false;
        display_tranfer_happened = false;
        top_screen_buf = 0;
    } else {
        report_end_frame_pending = true;
    }
}
} // namespace VideoCore
