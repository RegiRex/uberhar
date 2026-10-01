// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraPro: Exercise the production rotation policy, including restarts after
// every interrupted mutation. No actual game crash or Android device is needed.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include "common/logging/uberhar_log_retention.h"

using namespace Common::Log::Retention;
using namespace std::chrono_literals;
static std::size_t checks{};
static void Require(bool result, const char* message) {
    ++checks;
    if (!result) throw std::runtime_error(message);
}

struct FakeFiles {
    FakeFiles() = default;
    explicit FakeFiles(std::map<std::string, std::string> initial) : files(std::move(initial)) {}
    std::map<std::string, std::string> files;
    std::string directory;
    std::vector<std::string> mutations;
    int fail_at = -1;
    int stop_after = -1;
    int calls{};
    bool Exists(const std::string& p) { return files.contains(p) || p == directory; }
    bool IsDirectory(const std::string& p) { return p == directory; }
    std::size_t Size(const std::string& p) { return files.at(p).size(); }
    bool Before() { return calls++ != fail_at; }
    void After() { if (calls == stop_after) throw std::runtime_error("simulated process exit"); }
    bool Delete(const std::string& p) {
        if (!Before() || IsDirectory(p)) return false;
        mutations.push_back("delete:" + p);
        files.erase(p);
        After();
        return true;
    }
    bool Rename(const std::string& a, const std::string& b) {
        if (!Before() || !files.contains(a) || Exists(b)) return false;
        mutations.push_back("rename:" + a + ":" + b);
        files[b] = files.at(a);
        files.erase(a);
        After();
        return true;
    }
};

static const std::string Current = "log/azahar_log.txt";
static const auto Old = GenerationName(Current, "old");
static const auto Older = GenerationName(Current, "older");

// AstraPro: The real disk adapter deliberately refuses rename-over-existing,
// exercising the stricter document-provider behavior as well as native paths.
struct DiskFiles {
    bool Exists(const std::string& p) { return std::filesystem::exists(p); }
    bool IsDirectory(const std::string& p) { return std::filesystem::is_directory(p); }
    std::uintmax_t Size(const std::string& p) { return std::filesystem::file_size(p); }
    bool Delete(const std::string& p) {
        if (!Exists(p)) return true;
        if (IsDirectory(p)) return false;
        std::error_code ec;
        return std::filesystem::remove(p, ec) && !ec;
    }
    bool Rename(const std::string& a, const std::string& b) {
        if (!Exists(a) || Exists(b)) return false;
        std::error_code ec;
        std::filesystem::rename(a, b, ec);
        return !ec;
    }
};
static void Append(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::app);
    file << text;
    file.flush();
    Require(file.good(), "disk append failed");
}
static std::string Read(const std::string& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int main() {
    Require(Old == "log/azahar_log.old.txt", "existing old name changed");
    Require(Older == "log/azahar_log.older.txt", "older name");
    Require(GenerationName("/parent.txt/azahar_log.txt", "old") ==
            "/parent.txt/azahar_log.old.txt", "parent path rewritten");
    Require(GenerationName("/log/no_extension", "older") ==
            "/log/no_extension.older.txt", "extensionless log aliases source");
    FakeFiles f;
    f.files[Old] = "B";
    f.files[Older] = "A";
    Require(Rotate(f, Current) == Result::NoData, "missing current");
    Require(f.mutations.empty(), "missing current evicted backups");
    f.files[Current] = "";
    Require(Rotate(f, Current) == Result::NoData, "empty current");
    Require(f.mutations.empty(), "empty current evicted backups");
    f.files[Current] = "C";
    Require(Rotate(f, Current) == Result::Rotated, "normal rotation");
    Require(!f.Exists(Current) && f.files[Old] == "C" && f.files[Older] == "B", "rotation order");
    Require(f.mutations == std::vector<std::string>{"delete:" + Older,
            "rename:" + Old + ":" + Older, "rename:" + Current + ":" + Old}, "mutation sequence");
    f.files[Current] = "D";
    Require(Rotate(f, Current) == Result::Rotated, "second restart");
    Require(f.files[Old] == "D" && f.files[Older] == "C", "crash history lost on second restart");

    // AstraPro: Any reported mutation failure retains the current crash file.
    // Retrying after partial success must recover without truncating either of
    // the two newest original logs. Only the oldest is an intended eviction.
    for (int failure = 0; failure < 3; ++failure) {
        FakeFiles fault{{{Current,"crash"},{Old,"previous"},{Older,"oldest"}}};
        fault.fail_at = failure;
        Require(Rotate(fault, Current) == Result::Failed, "fault not reported");
        Require(fault.files[Current] == "crash", "fault destroyed current log");
        Require(fault.files.contains(Old) ? fault.files[Old] == "previous" :
                fault.files[Older] == "previous", "fault destroyed previous log");
        fault.files[Current] += "\nnew session"; // Mirrors FileBackend's append-only open.
        fault.fail_at = -1;
        Require(Rotate(fault, Current) == Result::Rotated, "retry failed");
        Require(fault.files[Old] == "crash\nnew session" &&
                fault.files[Older] == "previous", "retry lost evidence");
    }
    for (int interrupt = 1; interrupt <= 3; ++interrupt) {
        FakeFiles fault{{{Current,"crash"},{Old,"previous"},{Older,"oldest"}}};
        fault.stop_after = interrupt;
        try { Rotate(fault, Current); } catch (const std::runtime_error&) {}
        fault.stop_after = -1;
        Rotate(fault, Current);
        Require(fault.files[Old] == "crash" && fault.files[Older] == "previous",
                "restart after interrupted rotation lost evidence");
    }
    for (const auto& blocked : {Current, Old, Older}) {
        FakeFiles fault{{{Current,"crash"},{Old,"previous"}}};
        fault.files.erase(blocked);
        fault.directory = blocked;
        Require(Rotate(fault, Current) == Result::Failed, "directory should block rotation");
        Require(blocked == Current || fault.files[Current] == "crash", "directory fault lost current");
    }
    FakeFiles gap{{{Current,"crash"},{Older,"previous"}}};
    Require(Rotate(gap, Current) == Result::Rotated, "old-absent recovery failed");
    Require(gap.files[Older] == "previous", "old-absent recovery evicted older");

    FlushPolicy flush;
    Require(flush.Due(0us), "first entry not flushed");
    Require(!flush.Due(1us) && !flush.Due(999999us), "per-entry flush regression");
    Require(flush.Due(1s) && !flush.Due(1000001us), "one-second flush bound");
    Require(!flush.Due(500000us) && flush.Due(2s), "out-of-order timestamp handling");
    for (int i = 1; i <= 10000; ++i)
        Require(flush.Due(2s + std::chrono::milliseconds{i}) == (i % 1000 == 0), "flush cadence drift");

    DiskFiles disk;
    const auto root = std::filesystem::temp_directory_path() /
        ("uberhar-log-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root,ec); } } cleanup{root};
    const auto current = (root / "azahar_log.txt").string();
    const auto old = GenerationName(current,"old"), older = GenerationName(current,"older");
    Require(Rotate(disk,current) == Result::NoData,"first launch");
    for (int session = 0; session < 5; ++session) {
        if (session) Require(Rotate(disk,current) == Result::Rotated,"disk rotation");
        Append(current,"session " + std::to_string(session));
        Require(Read(current) == "session " + std::to_string(session),"current contents");
        if (session >= 1) Require(Read(old) == "session " + std::to_string(session-1),"old contents");
        if (session >= 2) Require(Read(older) == "session " + std::to_string(session-2),"older contents");
    }
    Require(std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator{}) == 3,
            "unbounded log files");
    std::printf("PASS: %zu checks; current/old/older rotation, fault/interruption recovery, append preservation, "
                "flush cadence, five real-filesystem sessions; no Android/SAF device validation claimed\n",checks);
}
