// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace Vulkan::UberharCacheDiagnostics {
// AstraEH: Read-only, title-scoped startup inventory. File presence is distinct
// from compatible module reuse; opaque driver-internal caches remain unknown.
struct Inventory {
    std::uint64_t files{}, bytes{}, examined{};
    bool complete{true}, capped{};

    void Add(const Inventory& other) {
        files += other.files;
        bytes += other.bytes;
        complete &= other.complete;
        capped |= other.capped;
    }
};

inline Inventory InspectFile(const std::filesystem::path& path) {
    Inventory result;
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return result;
    if (error || !std::filesystem::is_regular_file(status)) {
        result.complete = false;
        return result;
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        result.complete = false;
        return result;
    }
    result.files = 1;
    result.bytes = bytes;
    return result;
}

// AstraEH: Scan metadata once before any cache loading/writing, with a hard entry
// cap. An inaccessible/truncated scan must never claim that a cache was empty.
inline Inventory InspectGeneric(const std::filesystem::path& directory, std::string_view title,
                                std::size_t limit = 8192) {
    Inventory result;
    std::error_code error;
    std::filesystem::directory_iterator it{directory, error}, end;
    if (error == std::errc::no_such_file_or_directory)
        return result;
    if (error) {
        result.complete = false;
        return result;
    }
    const auto prefix = std::string{title} + "-uber-";
    for (; it != end; it.increment(error)) {
        if (error)
            break;
        if (result.examined == limit) {
            result.complete = false;
            result.capped = true;
            break;
        }
        ++result.examined;
        const auto name = it->path().filename().string();
        if (name.starts_with(prefix) && name.ends_with(".spv"))
            result.Add(InspectFile(it->path()));
    }
    if (error)
        result.complete = false;
    return result;
}

struct Snapshot {
    Inventory generic, driver, specialized;
    bool recorded{}, enabled{}, specialized_active{};

    const char* State() const {
        if (!recorded)
            return "not_observed";
        if (!enabled)
            return "disabled";
        Inventory active = generic;
        active.Add(driver);
        if (specialized_active)
            active.Add(specialized);
        // AstraEH: Preserve uncertainty even when some files were successfully counted.
        if (!active.complete)
            return "unknown";
        return active.bytes == 0 ? "empty_files" : "present_files";
    }
};

inline Snapshot Inspect(const std::filesystem::path& pipeline_directory,
                        const std::filesystem::path& driver_file,
                        const std::filesystem::path& transferable_directory, std::string_view title,
                        bool enabled, bool specialized_active) {
    Snapshot result;
    result.recorded = true;
    result.enabled = enabled;
    result.specialized_active = specialized_active;
    if (!enabled)
        return result;
    // AstraEH: An unavailable virtual-filesystem mapping is not an empty directory.
    if (pipeline_directory.empty() || driver_file.empty() || transferable_directory.empty()) {
        result.generic.complete = result.driver.complete = result.specialized.complete = false;
        return result;
    }
    result.generic = InspectGeneric(pipeline_directory, title);
    result.driver = InspectFile(driver_file);
    for (const auto stage : {"vs", "fs", "gs", "pl"}) {
        result.specialized.Add(
            InspectFile(transferable_directory / (std::string{title} + "_" + stage + ".vkch")));
    }
    return result;
}

// AstraEH: Observed reuse only, never a claim about unseen families or driver hits.
inline const char* ReuseState(bool enabled, std::uint64_t hits, std::uint64_t misses) {
    if (!enabled)
        return "disabled";
    if (!hits && !misses)
        return "not_observed";
    if (hits && misses)
        return "mixed";
    return hits ? "warm_encountered" : "cold_encountered";
}
} // namespace Vulkan::UberharCacheDiagnostics
