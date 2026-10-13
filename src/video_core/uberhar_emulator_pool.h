// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include "common/thread.h"
#include "common/uberhar_perf_hint.h"
#include "common/uberhar_parallel_work.h"

namespace VideoCore {

// Claude: One pool for every synchronous fork-join issued from the emulator
// thread (vertex batches, texture decoding). Separate per-feature pools each
// grew up to processors-1 sleeping threads; sharing keeps one set. The pool's
// reentrancy guard runs an overlapping job on its own owner instead of waiting.
// Created on first use and intentionally never destroyed: its workers sleep
// between jobs, and process exit cannot join them while emulation still runs.
inline Common::Uberhar::ParallelWork& EmulatorPool() {
    static Common::Uberhar::ParallelWork* const pool = new Common::Uberhar::ParallelWork{
        Common::Uberhar::AvailableProcessors(),
        +[] {
            Common::SetCurrentThreadName("UberharWorker");
            Common::Uberhar::PerfHint::RegisterCurrentThread();
        }};
    return *pool;
}

} // namespace VideoCore
