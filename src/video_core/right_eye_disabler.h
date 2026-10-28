// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include "common/common_types.h"

namespace VideoCore {
class GPU;

class RightEyeDisabler {
public:
    RightEyeDisabler(GPU& gpu) : gpu{gpu} {}
    // CodexAstraLocal TEST-ONLY LOG: Final counts complete the owner right-eye comparison;
    // remove this observer and its counters after that comparison is accepted.
    ~RightEyeDisabler();

    bool ShouldAllowCmdQueueTrigger(PAddr addr, u32 size);
    bool ShouldAllowDisplayTransfer(PAddr src_address, size_t size);

    void ReportEndFrame();

    void SetEnabled(bool enable) {
        enabled = enable;
    }

private:
    // CodexAstraLocal TEST-ONLY LOG: Bounded observer state counts decisions, never inferred
    // draws/eyes or saved time. Remove with the owner right-eye comparison diagnostics.
    void ReportDiagnostics(bool final = false) noexcept;
    u64 command_lists_seen{};
    u64 command_lists_evaluated{};
    u64 command_lists_skipped{};
    u64 display_transfers_seen{};
    u64 display_transfers_evaluated{};
    u64 display_transfers_skipped{};
    u64 end_frame_callbacks{};
    u32 last_guessed_viewport_height{};
    PAddr last_guessed_color_buffer{};
    bool diagnostics_reported{};
    bool last_reported_requested{};
    bool last_reported_allowed{};
    std::chrono::steady_clock::time_point last_diagnostic_report{};

    bool enabled = true;
    // Enabled by ReportEndFrame() once the user setting has been read.
    bool enable_for_frame = false;

    bool top_screen_drawn = false;
    bool top_screen_transfered = false;
    bool top_screen_blocked = false;
    bool cmd_trigger_blocked = false;
    PAddr top_screen_buf = 0;

    bool cmd_queue_trigger_happened = false;
    bool display_tranfer_happened = false;
    bool report_end_frame_pending = false;

    GPU& gpu;
};
} // namespace VideoCore
