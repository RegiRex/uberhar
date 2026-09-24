// Copyright 2021 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono>
#include <future> // AstraEH: A queued export barrier waits for buffered log data.
#include <memory>

#include "common/logging/types.h"

namespace Common::Log {

/**
 * A log entry. Log entries are store in a structured format to permit more varied output
 * formatting on different frontends, as well as facilitating filtering and aggregation.
 */
struct Entry {
    std::chrono::microseconds timestamp;
    Class log_class{};
    Level log_level{};
    const char* filename = nullptr;
    u32 line_num = 0;
    std::string function;
    std::string message;
    // AstraEH: Present only on a flush request; ordinary entries allocate no promise.
    std::shared_ptr<std::promise<void>> flush_request;
};

} // namespace Common::Log
