// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace Common::Log::Retention {

// AstraPro: Modify only the final filename suffix, never a directory containing
// ".txt". Preserve the existing current/.old.txt names; .older.txt is disk-only.
inline std::string GenerationName(const std::string& filename, std::string_view generation) {
    const auto end = filename.ends_with(".txt") ? filename.size() - 4 : filename.size();
    return filename.substr(0, end) + "." + std::string{generation} + ".txt";
}

enum class Result { NoData, Rotated, Failed };

// AstraPro: Rotate before opening the writer, independently of orderly shutdown.
// Missing/empty current files do not evict useful history. On failure the caller
// MUST append (never truncate): preserving the current crash evidence takes
// precedence over separating sessions. Each successful rename is independently
// useful after an interrupted startup; this is not a multi-file transaction.
// Files is a narrow adapter so tests can exercise the production algorithm with
// filesystem faults. Android continues through FileUtil's raw/SAF dispatch.
template <typename Files>
Result Rotate(Files& files, const std::string& current) {
    if (current.empty() || !files.Exists(current))
        return Result::NoData;
    if (files.IsDirectory(current))
        return Result::Failed;
    if (files.Size(current) == 0)
        return Result::NoData;

    const auto old = GenerationName(current, "old");
    const auto older = GenerationName(current, "older");
    if (files.Exists(old)) {
        if (files.IsDirectory(old))
            return Result::Failed;
        // AstraPro: Delete only the oldest generation, and stop if any
        // operation fails. Never delete old/current to make a rename fit.
        if (!files.Delete(older) || !files.Rename(old, older))
            return Result::Failed;
    }
    return files.Rename(current, old) ? Result::Rotated : Result::Failed;
}

// AstraPro: Reuse entry timestamps, avoiding a clock syscall per log line.
// Flush the first written entry and periodically while queued records are being
// consumed. This limits stdio buffering, not records still in the async queue,
// and is not fsync/power-loss durability or a guaranteed final crash backtrace.
class FlushPolicy {
public:
    bool Due(std::chrono::microseconds timestamp) {
        if (!started || timestamp >= last + std::chrono::seconds{1}) {
            started = true;
            last = timestamp;
            return true;
        }
        return false;
    }

private:
    bool started = false;
    std::chrono::microseconds last{};
};

} // namespace Common::Log::Retention
