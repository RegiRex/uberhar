// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include "common/file_util.h"
#include "common/logging/log.h"

namespace FileSys {

// CodexAstraLocal: External CIA imports stage beside each destination so key, input,
// disk and rename failures cannot overwrite an existing title before validation.
// Existing files move to backups without copying multi-gigabyte installed content.
class CIAInstallTransaction {
public:
    CIAInstallTransaction() {
        static std::atomic<unsigned long long> sequence{};
        suffix = ".uberhar-import-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "-" + std::to_string(sequence++);
    }

    ~CIAInstallTransaction() {
        if (!committed) {
            Rollback();
        }
        for (const auto& entry : entries) {
            if (FileUtil::Exists(entry.staged)) {
                FileUtil::Delete(entry.staged);
            }
        }
    }

    // CodexAstraLocal: Reuse an owned staging path; refuse collisions without touching their data.
    std::string Stage(const std::string& destination, bool metadata = false) {
        for (const auto& entry : entries) {
            if (entry.destination == destination) {
                return entry.staged;
            }
        }
        Entry entry{destination, destination + suffix + ".tmp",
                    destination + suffix + ".bak", metadata};
        if (FileUtil::Exists(entry.staged) || FileUtil::Exists(entry.backup)) {
            LOG_ERROR(Service_AM, "CIA staging path collision; install aborted");
            return {};
        }
        entries.push_back(entry);
        return entry.staged;
    }

    // CodexAstraLocal: Publish TMD last and reverse successful renames if promotion fails.
    bool Commit() {
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry& a, const Entry& b) { return a.metadata < b.metadata; });
        for (auto& entry : entries) {
            if (!FileUtil::Exists(entry.staged)) {
                Rollback();
                return false;
            }
            if (FileUtil::Exists(entry.destination)) {
                if (!FileUtil::Rename(entry.destination, entry.backup)) {
                    Rollback();
                    return false;
                }
                entry.backed_up = true;
            }
            if (!FileUtil::Rename(entry.staged, entry.destination)) {
                Rollback();
                return false;
            }
            entry.promoted = true;
        }
        committed = true;
        for (auto& entry : entries) {
            if (entry.backed_up && !FileUtil::Delete(entry.backup)) {
                LOG_WARNING(Service_AM, "CIA committed; obsolete backup retained at {}", entry.backup);
            }
        }
        return true;
    }

private:
    struct Entry {
        std::string destination;
        std::string staged;
        std::string backup;
        bool metadata{};
        bool backed_up{};
        bool promoted{};
    };

    // CodexAstraLocal: Keep a backup on disk if recovery itself fails; never delete the last copy.
    void Rollback() {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->promoted) {
                if (!FileUtil::Rename(it->destination, it->staged)) {
                    LOG_ERROR(Service_AM, "CIA rollback could not remove new file {}; backup retained",
                              it->destination);
                    continue;
                }
                it->promoted = false;
            }
            if (it->backed_up) {
                if (!FileUtil::Rename(it->backup, it->destination)) {
                    LOG_ERROR(Service_AM, "CIA rollback needs recovery from {}", it->backup);
                    continue;
                }
                it->backed_up = false;
            }
        }
    }

    std::string suffix;
    std::vector<Entry> entries;
    bool committed = false;
};

} // namespace FileSys
