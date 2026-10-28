// CodexAstraLocal: The production transaction runs unchanged against actual
// temporary files. Fault injection affects only named filesystem operations.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "core/file_sys/cia_install_transaction.h"

namespace fs = std::filesystem;
struct RenameCall {
    std::string from, to;
    bool success;
};
static std::vector<RenameCall> rename_calls;
static std::set<unsigned> failed_calls;
static std::set<std::string> failed_sources;
static unsigned failed_sources_after_call = 0;
static unsigned assertions = 0;

namespace FileUtil {
bool Exists(const std::string& filename) {
    std::error_code error;
    return fs::exists(filename, error);
}
bool Delete(const std::string& filename) {
    std::error_code error;
    return fs::remove(filename, error);
}
bool Rename(const std::string& from, const std::string& to) {
    const bool fail = failed_calls.contains(static_cast<unsigned>(rename_calls.size()) + 1) ||
                      (rename_calls.size() >= failed_sources_after_call && failed_sources.contains(from));
    std::error_code error;
    if (!fail) fs::rename(from, to, error);
    rename_calls.push_back({from, to, !fail && !error});
    return !fail && !error;
}
}

static void Check(bool ok, const char* description) {
    ++assertions;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", description);
        std::exit(1);
    }
}
static void Write(const std::string& path, const std::string& bytes) {
    // CodexAstraLocal: Plain stdio avoids introducing unrelated virtual stream
    // endpoints while retaining actual file creation, contents and close errors.
    auto* file = std::fopen(path.c_str(), "wb");
    Check(file != nullptr, "test file opened");
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    const auto closed = std::fclose(file);
    Check(written == bytes.size() && closed == 0, "test file written");
}
static std::string Read(const std::string& path) {
    auto* file = std::fopen(path.c_str(), "rb");
    if (!file) return {};
    std::string bytes;
    std::array<char, 256> buffer;
    while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file)) {
        bytes.append(buffer.data(), count);
    }
    Check(std::ferror(file) == 0, "test file read");
    std::fclose(file);
    return bytes;
}
static std::string Backup(const std::string& staged) {
    Check(staged.ends_with(".tmp"), "staging path has expected suffix");
    return staged.substr(0, staged.size() - 4) + ".bak";
}
static fs::path Scenario(const fs::path& root, const std::string& name) {
    rename_calls.clear();
    failed_calls.clear();
    failed_sources.clear();
    failed_sources_after_call = 0;
    auto path = root / name;
    Check(fs::create_directory(path), "scenario directory created");
    return path;
}
static void NoStagingFiles(const fs::path& path) {
    auto* directory = opendir(path.c_str());
    Check(directory != nullptr, "test directory opened");
    while (const auto* entry = readdir(directory)) {
        Check(std::string(entry->d_name).find(".uberhar-import-") == std::string::npos,
              "completed transaction removes temporary files");
    }
    closedir(directory);
}

static void CheckSuccess(const fs::path& root) {
    const auto path = Scenario(root, "success");
    const auto app = (path / "main.app").string(), metadata = (path / "title.tmd").string();
    const auto optional = (path / "optional.app").string();
    Write(app, "old-app"); Write(metadata, "old-tmd"); Write(optional, "old-optional");
    {
        FileSys::CIAInstallTransaction transaction;
        const auto staged_tmd = transaction.Stage(metadata, true);
        const auto staged_app = transaction.Stage(app);
        Check(transaction.Stage(app) == staged_app, "same destination reuses staging path");
        Write(staged_tmd, "new-tmd"); Write(staged_app, "new-app");
        Check(transaction.Commit(), "complete import commits");
        std::vector<std::string> promotions;
        for (const auto& call : rename_calls) {
            if (call.success && call.from.ends_with(".tmp")) promotions.push_back(call.to);
        }
        Check(promotions == std::vector<std::string>{app, metadata}, "metadata published last");
    }
    Check(Read(app) == "new-app" && Read(metadata) == "new-tmd", "successful import replaces old files");
    Check(Read(optional) == "old-optional", "omitted optional content preserved");
    NoStagingFiles(path);
}

static void CheckAbort(const fs::path& root) {
    const auto path = Scenario(root, "abort");
    const auto app = (path / "main.app").string(), added = (path / "added.app").string();
    Write(app, "old-app");
    {
        FileSys::CIAInstallTransaction transaction;
        Write(transaction.Stage(app), "new-app");
        Write(transaction.Stage(added), "new-added");
    }
    Check(Read(app) == "old-app" && !FileUtil::Exists(added), "abort preserves installed files");
    Check(rename_calls.empty(), "uncommitted staged files never promoted");
    NoStagingFiles(path);
}

static void CheckEveryRenameFailure(const fs::path& root, bool existing) {
    for (unsigned failed = 1; failed <= (existing ? 6U : 3U); ++failed) {
        const auto path = Scenario(root, std::string(existing ? "replace-" : "new-") + std::to_string(failed));
        std::array<std::string, 3> destinations{
            (path / "first.app").string(), (path / "second.app").string(), (path / "title.tmd").string()};
        if (existing) for (const auto& destination : destinations) Write(destination, "old:" + destination);
        {
            FileSys::CIAInstallTransaction transaction;
            for (std::size_t index = 0; index < destinations.size(); ++index) {
                Write(transaction.Stage(destinations[index], index == 2), "new:" + destinations[index]);
            }
            failed_calls.insert(failed);
            Check(!transaction.Commit(), "injected rename failure rejects commit");
            for (const auto& destination : destinations) {
                Check(existing ? Read(destination) == "old:" + destination : !FileUtil::Exists(destination),
                      "existing file restored after rename failure");
            }
        }
        NoStagingFiles(path);
    }
}

static void CheckMissingStage(const fs::path& root) {
    const auto path = Scenario(root, "missing-stage");
    const auto app = (path / "main.app").string(), tmd = (path / "title.tmd").string();
    Write(app, "old-app"); Write(tmd, "old-tmd");
    {
        FileSys::CIAInstallTransaction transaction;
        Write(transaction.Stage(app), "new-app");
        Check(!transaction.Stage(tmd, true).empty(), "missing-stage path allocated");
        Check(!transaction.Commit(), "missing staged metadata rejects import");
        Check(Read(app) == "old-app" && Read(tmd) == "old-tmd", "missing stage rolls back promoted content");
    }
    NoStagingFiles(path);
}

static void CheckRollbackFailure(const fs::path& root, bool removal_fails) {
    const auto path = Scenario(root, removal_fails ? "rollback-remove-fails" : "rollback-restore-fails");
    const auto first = (path / "first.app").string(), second = (path / "second.app").string();
    Write(first, "old-first"); Write(second, "old-second");
    std::string staged_first, staged_second, backup_first, backup_second;
    {
        FileSys::CIAInstallTransaction transaction;
        staged_first = transaction.Stage(first);
        staged_second = transaction.Stage(second);
        backup_first = Backup(staged_first); backup_second = Backup(staged_second);
        Write(staged_first, "new-first"); Write(staged_second, "new-second");
        failed_calls.insert(4);
        failed_sources.insert(removal_fails ? first : backup_second);
        failed_sources_after_call = 4;
        Check(!transaction.Commit(), "rollback failure still rejects commit");
    }
    if (removal_fails) {
        Check(Read(first) == "new-first" && Read(backup_first) == "old-first",
              "failed removal retains old backup and new destination");
        Check(Read(second) == "old-second", "other rollback restores destination");
    } else {
        Check(!FileUtil::Exists(second) && Read(backup_second) == "old-second",
              "failed restore retains last old copy in backup");
        Check(Read(first) == "old-first", "earlier promotion still rolled back");
    }
    Check(!FileUtil::Exists(staged_first) && !FileUtil::Exists(staged_second),
          "rollback failure discards staged new data only");
}

static void CheckCollisions(const fs::path& root) {
    for (bool backup_collision : {false, true}) {
        const auto path = Scenario(root, backup_collision ? "backup-collision" : "temp-collision");
        const auto anchor = (path / "anchor.app").string(), target = (path / "target.app").string();
        std::string collision;
        Write(target, "installed");
        {
            FileSys::CIAInstallTransaction transaction;
            const auto staged_anchor = transaction.Stage(anchor);
            const auto suffix = staged_anchor.substr(anchor.size(), staged_anchor.size() - anchor.size() - 4);
            collision = target + suffix + (backup_collision ? ".bak" : ".tmp");
            Write(collision, "belongs-to-another-operation");
            Check(transaction.Stage(target).empty(), "existing transaction path collision refused");
        }
        Check(Read(target) == "installed" && Read(collision) == "belongs-to-another-operation",
              "collision preserves both existing files");
        Check(rename_calls.empty(), "collision does not rename unowned files");
    }
}

int main(int argc, char** argv) {
    Check(argc == 2, "temporary filesystem root supplied");
    const fs::path root(argv[1]);
    Check(fs::create_directory(root), "temporary filesystem root created");
    CheckSuccess(root);
    CheckAbort(root);
    CheckEveryRenameFailure(root, true);
    CheckEveryRenameFailure(root, false);
    CheckMissingStage(root);
    CheckRollbackFailure(root, false);
    CheckRollbackFailure(root, true);
    CheckCollisions(root);
    std::printf("PASS CIA transaction (%u assertions)\n", assertions);
}
