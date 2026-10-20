#!/usr/bin/env python3
# AstraEH: Run the production FileBackend against real stdio files, including a killed
# helper process and the real idle-flush worker. Android provider/device coverage is separate.
from pathlib import Path
import signal  # CodexAstraUlt: Verify fatal subprocess signal termination, not a timeout.
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/common/logging/backend.cpp').read_text()
start = source.index('class FileBackend final : public Backend {')
end = source.index('\n};', start) + 3
production = source[start:end]
# CodexAstraUlt: Include the production omission reporter used by the worker.
worker_start = source.index('    void ReportDiagnosticOmissions(bool force) {')
worker_end = source.index('\n    void ForEachBackend(', worker_start)
worker = source[worker_start:worker_end]
# CodexAstraUlt: Exercise production sink serialization, not a replacement test mutex.
fanout_start = worker_end
fanout_end = source.index('\n    static void Deleter(', fanout_start)
fanout = source[fanout_start:fanout_end]
# CodexAstraUlt: Reuse the real callback backend and Linux fatal dispatch for the
# platform regressions, while replacing only the unsafe legacy helper with a sentinel.
libretro_start = source.index('class LibRetroBackend : public Backend {')
libretro_end = source.index('\n};', libretro_start) + 3
libretro = source[libretro_start:libretro_end]
signal_start = source.index('    [[noreturn]] static void HandleSignal(int sig) {')
signal_end = source.index('\n    [[noreturn]] void InstanceHandleSignal(', signal_start)
signal_dispatch = source[signal_start:signal_end]
prefix = r'''
#include <algorithm> // CodexAstraUlt: Count complete records in concurrent output.
#include <array>
#include <atomic>
#include <future>
#include <thread>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "common/logging/uberhar_log_retention.h"
#include "common/logging/diagnostic_queue.h"
#include "common/logging/backend_access.h"
#include "common/bounded_threadsafe_queue.h"
#ifdef HAVE_LIBRETRO
#include <libretro.h>
#endif
// CodexAstraUlt: Format the real production omission record with the pinned fmt headers.
#define FMT_HEADER_ONLY
#include <fmt/format.h>
namespace Common { void SetCurrentThreadName(const char*) {} }
namespace fs = std::filesystem;
namespace Common::Literals {
constexpr unsigned long long operator""_MiB(unsigned long long n) { return n * 1024 * 1024; }
}
namespace FileUtil {
bool Exists(const std::string& p) { return fs::exists(p); }
bool IsDirectory(const std::string& p) { return fs::is_directory(p); }
u64 GetSize(const std::string& p) { return fs::file_size(p); }
bool Delete(const std::string& p) { return !fs::exists(p) || fs::remove(p); }
bool Rename(const std::string& a, const std::string& b) { fs::rename(a,b); return true; }
struct IOFile {
    // CodexAstraUlt: Pause a real worker write deterministically to overlap a
    // synchronous producer and another optional enqueue without scheduling guesses.
    static inline std::atomic<bool> pause_next_write{false};
    static inline std::promise<void>* write_entered = nullptr;
    static inline std::shared_future<void> write_release;
    FILE* file;
    std::string name;
    IOFile(const std::string& p, const char* mode, int) : file{fopen(p.c_str(), mode)}, name{p} {}
    ~IOFile() { Close(); }
    bool IsOpen() const { return file != nullptr; }
    size_t GetSize() const { return file && fs::is_regular_file(name) ? fs::file_size(name) : 0; }
    size_t WriteString(const std::string& s) {
        if (pause_next_write.exchange(false)) {
            write_entered->set_value();
            write_release.wait();
        }
        return file ? fwrite(s.data(),1,s.size(),file) : 0;
    }
    bool Flush() { return file && fflush(file)==0; }
    void Close() { if(file) fclose(file); file=nullptr; }
};
}
namespace Common::Log {
struct Entry {
    Level log_level; std::chrono::microseconds timestamp; std::string message;
    const char* filename = "test";
    std::shared_ptr<std::promise<void>> flush_request;
};
std::string FormatLogMessage(const Entry& entry) { return entry.message; }
struct Backend {
    virtual ~Backend() = default;
    virtual void Write(const Entry&)=0;
    virtual void Flush()=0;
    virtual void Close()=0;
    virtual void EnableForStacktrace()=0;
};
constexpr int _SH_DENYWR = 0;
// CodexAstraUlt: Inactive host console/debug sinks keep production fan-out intact;
// all tested output and contested state belong to the actual FileBackend below.
struct SilentBackend : Backend {
    void Write(const Entry&) override {}
    void Flush() override {}
    void Close() override {}
    void EnableForStacktrace() override {}
};
'''
worker_prefix = r'''
struct Logger {
    FileBackend file_backend;
    SilentBackend debugger_backend;
    SilentBackend color_console_backend;
#ifdef HAVE_LIBRETRO
    // CodexAstraUlt: The callback is the actual primary sink in this configuration.
    LibRetroBackend libretro_backend;
#endif
    std::mutex backend_mutex; // CodexAstraUlt: Owned by production fan-out/idle flush.
    struct Filter { bool IsDebug() { return false; } } filter;
    Common::MPSCQueue<Entry> message_queue;
    // CodexAstraUlt: Production reporter state and formatting adapter only; the
    // reporter and worker below are extracted unchanged from backend.cpp.
    DiagnosticDropCounter diagnostic_omissions;
    std::chrono::steady_clock::time_point time_origin{std::chrono::steady_clock::now()};
    static const char* TrimSourcePath(const char* path) { return path; }
    static Entry CreateEntry(Class, Level level, const char*, unsigned int, const char*,
                             std::string message, std::chrono::steady_clock::time_point origin) {
        return {level, std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - origin), std::move(message)};
    }
    std::jthread backend_thread;
#ifdef HAVE_LIBRETRO
    Logger(const std::string& file, retro_log_printf_t callback = nullptr)
        : file_backend{file, false}, libretro_backend{callback} {}
#else
    Logger(const std::string& file) : file_backend{file, false} {}
#endif
'''
worker_suffix = '\n};\n'
suffix = r'''
}
using namespace Common::Log;
std::string read(const fs::path& p) { std::ifstream f(p); return {std::istreambuf_iterator<char>{f}, {}}; }
int main(int argc, char** argv) {
    const fs::path root{argv[1]};
    fs::create_directories(root);
    const auto legacy = (root/"legacy.txt").string();
    const auto journal = (root/"session-a.txt").string();
    {
        FileBackend a{legacy};
        FileBackend b{journal, false};
        Entry entry{Level::Info, std::chrono::microseconds{1}, "session A"};
        a.Write(entry); b.Write(entry); a.Flush(); b.Flush();
        assert(a.Healthy() && b.Healthy());
    }
    for (int i=0; i<6; ++i) {
        FileBackend normal{legacy};
        FileBackend unique{(root/("session-"+std::to_string(i)+".txt")).string(), false};
        Entry entry{Level::Info, std::chrono::microseconds{1}, "other session"};
        normal.Write(entry); unique.Write(entry);
    }
    assert(read(journal)=="session A\n");
    assert(!fs::exists(root/"session-a.old.txt"));
    {
        FileBackend appended{journal, false};
        appended.Write({Level::Error, std::chrono::microseconds{1}, "preserved append"});
    }
    assert(read(journal)=="session A\npreserved append\n");
    int ready[2]; assert(pipe(ready)==0);
    pid_t child=fork(); assert(child>=0);
    const auto killed=(root/"killed-session.txt").string();
    if(child==0) {
        close(ready[0]);
        FileBackend writer{killed, false};
        writer.Write({Level::Info, std::chrono::microseconds{1}, "written before kill"});
        writer.Flush();
        assert(writer.Healthy());
        char c=1; assert(write(ready[1],&c,1)==1);
        for(;;) pause();
    }
    close(ready[1]); char c; assert(read(ready[0],&c,1)==1); close(ready[0]);
    assert(kill(child,SIGKILL)==0); int status; assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    assert(read(killed)=="written before kill\n");
    {
        FileBackend missing{(root/"absent"/"log.txt").string(), false};
        assert(!missing.Healthy());
        FileBackend full{"/dev/full", false};
        full.Write({Level::Error, std::chrono::microseconds{1}, "storage error"});
        assert(!full.Healthy());
    }
    const auto quiet = (root/"quiet.txt").string();
    {
        Logger logger{quiet};
        logger.StartBackendThread();
        logger.message_queue.EmplaceWait(Entry{Level::Info, std::chrono::microseconds{1}, "first"});
        logger.message_queue.EmplaceWait(Entry{Level::Info, std::chrono::microseconds{2}, "quiet tail"});
        // The second record is deliberately below the per-entry flush threshold. It must
        // reach the file without another record, an export barrier or a Java timer.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        while (read(quiet).find("quiet tail") == std::string::npos &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        assert(read(quiet)=="first\nquiet tail\n");
        Entry barrier{}; barrier.filename = nullptr;
        barrier.flush_request = std::make_shared<std::promise<void>>();
        auto done = barrier.flush_request->get_future();
        logger.message_queue.EmplaceWait(barrier);
        assert(done.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
        logger.StopBackendThread(); // No double acknowledgement of the previous barrier.
    }
    const auto capped=root/"capped.txt";
    std::ofstream(capped).close(); fs::resize_file(capped,100ULL*1024*1024);
    { FileBackend writer{capped.string(),false}; assert(!writer.Healthy()); }
    // CodexAstraUlt: Omissions reach the actual file before a flush barrier is
    // acknowledged, even when the ordinary five-second report cadence is pending.
    const auto omitted = (root/"omissions.txt").string();
    {
        Logger logger{omitted};
        logger.StartBackendThread();
        auto barrier = [&] {
            Entry request{}; request.filename = nullptr;
            request.flush_request = std::make_shared<std::promise<void>>();
            auto done = request.flush_request->get_future();
            logger.message_queue.EmplaceWait(request);
            assert(done.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
        };
        logger.diagnostic_omissions.RecordDrop();
        barrier();
        assert(read(omitted).find("optional_records=1") != std::string::npos);
        assert(logger.diagnostic_omissions.Pending() == 0);
        logger.diagnostic_omissions.RecordDrop();
        logger.diagnostic_omissions.RecordDrop();
        barrier();
        assert(read(omitted).find("optional_records=2") != std::string::npos);
        assert(logger.diagnostic_omissions.Pending() == 0);
        logger.diagnostic_omissions.RecordDrop();
        logger.diagnostic_omissions.RecordDrop();
        logger.diagnostic_omissions.RecordDrop();
        logger.StopBackendThread();
        assert(read(omitted).find("optional_records=3") != std::string::npos);
        assert(logger.diagnostic_omissions.Pending() == 0);
    }
    // CodexAstraUlt: A missing primary sink cannot silently acknowledge omitted
    // diagnostics, including during the final worker drain and backend shutdown.
    {
        Logger missing{(root/"absent"/"omissions.txt").string()};
        missing.diagnostic_omissions.RecordDrop();
        missing.StartBackendThread();
        missing.StopBackendThread();
        assert(missing.diagnostic_omissions.Pending() == 1);
    }
    // CodexAstraUlt: A paused backend write holds only the sink lock. A reliable
    // synchronous producer must serialize with it; optional producers still return
    // immediately, and their flush barrier acknowledges only after output resumes.
    const auto concurrent = (root/"concurrent.txt").string();
    {
        Logger logger{concurrent};
        std::promise<void> write_entered;
        std::promise<void> release_write;
        FileUtil::IOFile::write_entered = &write_entered;
        FileUtil::IOFile::write_release = release_write.get_future().share();
        FileUtil::IOFile::pause_next_write.store(true);
        auto synchronous_write = [&](const Entry& entry) {
            logger.ForEachBackend([&](Backend& backend) { backend.Write(entry); backend.Flush(); });
        };
        logger.StartBackendThread();
        SubmitEntry(logger.message_queue, Entry{Level::Info, {}, "worker diagnostic"},
                    Delivery::Diagnostic, true, logger.diagnostic_omissions, synchronous_write);
        write_entered.get_future().wait();
        std::promise<void> synchronous_entered;
        auto synchronous = std::async(std::launch::async, [&] {
            synchronous_entered.set_value();
            SubmitEntry(logger.message_queue, Entry{Level::Info, {}, "reliable synchronous"},
                        Delivery::Reliable, true, logger.diagnostic_omissions, synchronous_write);
        });
        synchronous_entered.get_future().wait();
        const bool serialized = synchronous.wait_for(std::chrono::milliseconds{50}) ==
                                std::future_status::timeout;
        auto optional = std::async(std::launch::async, [&] {
            SubmitEntry(logger.message_queue, Entry{Level::Info, {}, "queued diagnostic"},
                        Delivery::Diagnostic, true, logger.diagnostic_omissions, synchronous_write);
        });
        const bool optional_returned = optional.wait_for(std::chrono::seconds{1}) ==
                                       std::future_status::ready;
        Entry request{}; request.filename = nullptr;
        request.flush_request = std::make_shared<std::promise<void>>();
        auto done = request.flush_request->get_future();
        assert(logger.message_queue.TryEmplace(request));
        const bool flush_waited = done.wait_for(std::chrono::milliseconds{50}) ==
                                  std::future_status::timeout;
        release_write.set_value();
        synchronous.get(); optional.get();
        assert(done.wait_for(std::chrono::seconds{1}) == std::future_status::ready);
        logger.StopBackendThread();
        assert(serialized && optional_returned && flush_waited);
        assert(logger.diagnostic_omissions.Pending() == 0);
        const auto contents = read(concurrent);
        assert(contents.find("worker diagnostic\n") != std::string::npos);
        assert(contents.find("reliable synchronous\n") != std::string::npos);
        assert(contents.find("queued diagnostic\n") != std::string::npos);
        assert(std::count(contents.begin(), contents.end(), '\n') == 3);
    }
    puts("PASS: production file backend/worker: SIGKILL, rotation, append, quiet-tail flush, barrier shutdown, omission summaries, unavailable sink accounting, concurrent synchronous/optional delivery, storage faults and size cap");
}
'''
# CodexAstraUlt: A valid callback acknowledges a delivered summary even with no
# file backend; a missing callback must retain exactly the same pending evidence.
libretro_suffix = r'''
}
using namespace Common::Log;
std::string captured;
void callback(retro_log_level level, const char* message, ...) {
    assert(level == RETRO_LOG_WARN);
    captured += message;
}
int main() {
    Logger active{"", callback};
    assert(!active.file_backend.Healthy());
    active.diagnostic_omissions.RecordDrop();
    active.ReportDiagnosticOmissions(true);
    assert(captured.find("optional_records=1") != std::string::npos);
    assert(active.diagnostic_omissions.Pending() == 0);
    Logger absent{"", nullptr};
    absent.diagnostic_omissions.RecordDrop();
    absent.ReportDiagnosticOmissions(true);
    assert(absent.diagnostic_omissions.Pending() == 1);
    puts("PASS: libretro omission summaries use available callback, retain absent-sink counts");
}
'''
# CodexAstraUlt: The production Linux dispatch runs in bounded subprocesses. A
# fault inside production sink fan-out must abort instead of entering the helper;
# ordinary faults still reach the original helper path, represented by exit 73.
signal_prefix = r'''
#include "common/logging/backend_access.h"
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <sys/resource.h>
#include <unistd.h>
namespace Common::Log {
struct Backend { void Write() { raise(SIGABRT); } };
struct SignalHarness {
    std::mutex backend_mutex;
    Backend debugger_backend, color_console_backend, file_backend;
    static inline SignalHarness* instance = nullptr;
    [[noreturn]] void InstanceHandleSignal(int) { _exit(73); }
'''
signal_suffix = r'''
};
}
int main(int argc, char**) {
    // CodexAstraUlt: Deliberate regression crashes must not create workspace cores.
    rlimit core_limit{0, 0};
    setrlimit(RLIMIT_CORE, &core_limit);
    Common::Log::SignalHarness logger;
    logger.instance = &logger;
    signal(SIGABRT, &Common::Log::SignalHarness::HandleSignal);
    if (argc > 1) {
        logger.ForEachBackend([](auto& backend) { backend.Write(); });
    } else {
        raise(SIGABRT);
    }
    return 74;
}
'''
with tempfile.TemporaryDirectory(prefix='uberhar-session-backend-') as temporary:
    directory = Path(temporary)
    cpp = directory / 'test.cpp'
    cpp.write_text(prefix + production + worker_prefix + worker + fanout + worker_suffix + suffix)
    # CodexAstraUlt: Use only the repository's existing fmt dependency for the new reporter.
    subprocess.run(['c++', '-std=c++20', '-O2', '-I' + str(root / 'src'),
                    '-I' + str(root / 'externals/fmt/include'), str(cpp),
                    '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test'), str(directory / 'files')], check=True, timeout=15)
    # CodexAstraUlt: Compile platform variants independently of the host build flags.
    retro_cpp = directory / 'retro.cpp'
    retro_cpp.write_text(prefix + libretro + production + worker_prefix + worker + fanout +
                        worker_suffix + libretro_suffix)
    subprocess.run(['c++', '-std=c++20', '-O2', '-DHAVE_LIBRETRO', '-I' + str(root / 'src'),
                    '-I' + str(root / 'externals/fmt/include'),
                    '-I' + str(root / 'externals/libretro-common/libretro-common/include'),
                    str(retro_cpp), '-o', str(directory / 'retro')], check=True)
    subprocess.run([str(directory / 'retro')], check=True, timeout=5)
    signal_cpp = directory / 'signal.cpp'
    signal_cpp.write_text(signal_prefix + fanout + signal_dispatch + signal_suffix)
    subprocess.run(['c++', '-std=c++20', '-O2', '-DCITRA_LINUX_GCC_BACKTRACE',
                    '-I' + str(root / 'src'), str(signal_cpp),
                    '-o', str(directory / 'signal')], check=True)
    normal = subprocess.run([str(directory / 'signal')], timeout=5)
    guarded = subprocess.run([str(directory / 'signal'), 'guarded'], timeout=5)
    assert normal.returncode == 73, normal.returncode
    assert guarded.returncode == -signal.SIGABRT, guarded.returncode
    print('PASS: Linux locked-sink fault aborts promptly; ordinary fault retains helper path')
