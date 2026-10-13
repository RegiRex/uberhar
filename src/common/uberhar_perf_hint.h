// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <cstdint>

// Claude: Android Dynamic Performance Framework (APerformanceHint, Android 13+).
// The emulator thread reports how long each frame's work took against the
// frame's target, so the kernel raises CPU clocks and placement for the
// registered threads before a frame runs late instead of after several do.
// Older Android versions and other hosts compile or resolve to no-ops.
namespace Common::Uberhar::PerfHint {
#if defined(UBERHAR_ATRACE)
// Add the calling thread to the hint session once; it leaves at thread exit.
void RegisterCurrentThread();
// Called once per emulated frame on the emulator thread, which registers itself.
void ReportFrame(std::int64_t work_ns, std::int64_t target_ns);
#else
inline void RegisterCurrentThread() {}
inline void ReportFrame(std::int64_t, std::int64_t) {}
#endif
} // namespace Common::Uberhar::PerfHint
