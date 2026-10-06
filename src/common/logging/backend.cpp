// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <mutex> // CodexAstraUlt: Serialize worker and explicit synchronous sink access.
#include <boost/regex.hpp>

#include <fmt/format.h>

#ifdef _WIN32
#include <share.h>   // For _SH_DENYWR
#include <windows.h> // For OutputDebugStringW
#else
#define _SH_DENYWR 0
#endif

#ifdef CITRA_LINUX_GCC_BACKTRACE
#define BOOST_STACKTRACE_USE_BACKTRACE
#include <boost/stacktrace.hpp>
#undef BOOST_STACKTRACE_USE_BACKTRACE
#include <signal.h>
#endif

#include "common/bounded_threadsafe_queue.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/literals.h"
#include "common/logging/backend.h"
#include "common/logging/backend_access.h" // CodexAstraUlt: Signal-aware sink serialization.
#include "common/logging/diagnostic_queue.h" // CodexAstraUlt: Explicit optional delivery policy.
#include "common/logging/log.h"
#include "common/logging/log_entry.h"
#include "common/logging/text_formatter.h"
#include "common/logging/uberhar_log_retention.h"
#include "common/polyfill_thread.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "common/thread.h"

namespace Common::Log {

namespace {

/**
 * Interface for logging backends.
 */
class Backend {
public:
    virtual ~Backend() = default;

    virtual void Write(const Entry& entry) = 0;

    virtual void EnableForStacktrace() = 0;

    virtual void Flush() = 0;

    virtual void Close() = 0;
};

#ifdef HAVE_LIBRETRO
/**
 * LibRetro backend
 */
class LibRetroBackend : public Backend {
public:
    explicit LibRetroBackend() {}
    explicit LibRetroBackend(retro_log_printf_t callback) : callback(callback) {}

    ~LibRetroBackend() override = default;

    void Write(const Entry& entry) override {
        if (callback == nullptr) {
            return;
        }
        retro_log_level log_level;

        switch (entry.log_level) {
        case Common::Log::Level::Trace:
            log_level = retro_log_level::RETRO_LOG_DEBUG;
            break;
        case Common::Log::Level::Debug:
            log_level = retro_log_level::RETRO_LOG_DEBUG;
            break;
        case Common::Log::Level::Info:
            log_level = retro_log_level::RETRO_LOG_INFO;
            break;
        case Common::Log::Level::Warning:
            log_level = retro_log_level::RETRO_LOG_WARN;
            break;
        case Common::Log::Level::Error:
            log_level = retro_log_level::RETRO_LOG_ERROR;
            break;
        case Common::Log::Level::Critical:
            log_level = retro_log_level::RETRO_LOG_ERROR;
            break;
        default:
            log_level = retro_log_level::RETRO_LOG_DUMMY;
        }

        auto str = FormatLogMessage(entry).append(1, '\n');
        callback(log_level, str.c_str());
    }

    void Flush() override {}

    void Close() override {}

    void EnableForStacktrace() override {}

    // CodexAstraUlt: Libretro's callback is its only sink; an intentionally absent
    // file backend cannot determine whether omission records were delivered.
    bool Available() const {
        return callback != nullptr;
    }

private:
    retro_log_printf_t callback = nullptr;
};
#endif

/**
 * Backend that writes to stderr and with color
 */
class ColorConsoleBackend final : public Backend {
public:
    explicit ColorConsoleBackend() = default;

    ~ColorConsoleBackend() override = default;

    void Write(const Entry& entry) override {
        if (enabled.load(std::memory_order_relaxed)) {
            PrintColoredMessage(entry);
        }
    }

    void Flush() override {
        std::fflush(stderr);
    }

    void Close() override {
        enabled = false;
    }

    void EnableForStacktrace() override {
        enabled = true;
    }

    void SetEnabled(bool enabled_) {
        enabled = enabled_;
    }

private:
    std::atomic_bool enabled{false};
};

/**
 * Backend that writes to a file passed into the constructor
 */
class FileBackend final : public Backend {
public:
    explicit FileBackend(const std::string& filename, bool rotate = true) {
        // AstraPro: Keep two prior process logs using the same filesystem path
        // adapter as the existing writer, including Android document providers.
        struct Files {
            bool Exists(const std::string& path) { return FileUtil::Exists(path); }
            bool IsDirectory(const std::string& path) { return FileUtil::IsDirectory(path); }
            u64 Size(const std::string& path) { return FileUtil::GetSize(path); }
            bool Delete(const std::string& path) { return FileUtil::Delete(path); }
            bool Rename(const std::string& from, const std::string& to) {
                return FileUtil::Rename(from, to);
            }
        } files;
        // AstraEH: Recovery failure suppresses rotation; append preserves the original evidence.
        const auto rotation =
            rotate ? Retention::Rotate(files, filename) : Retention::Result::NoData;
        if (rotation == Retention::Result::Failed) {
            // AstraPro Log Line: Startup logging is not initialized yet; never
            // recurse through LOG_WARNING here. Keep the source file by appending.
            std::fputs("Uberhar: log rotation incomplete; preserving current log in append mode\n",
                       stderr);
        }
        // AstraPro: Append is also safe after a successful rename (new file).
        // A provider/permission failure must never lead to truncating crash data.
        // _SH_DENYWR allows readers on Windows and is 0 on other platforms.
        file = std::make_unique<FileUtil::IOFile>(filename, "a", _SH_DENYWR);
        bytes_written = file->GetSize();
        enabled = file->IsOpen() && bytes_written < 100 * 1024 * 1024;
        healthy.store(enabled, std::memory_order_relaxed);
    }

    ~FileBackend() override = default;

    void Write(const Entry& entry) override {
        if (!enabled) {
            return;
        }

        const auto text = FormatLogMessage(entry).append(1, '\n');
        const auto written = file->WriteString(text);
        bytes_written += written;
        dirty = dirty || written > 0;
        if (written != text.size()) {
            healthy.store(false, std::memory_order_relaxed);
            enabled = false;
        }

        using namespace Common::Literals;
        // Prevent logs from exceeding a set maximum size in the event that log entries are spammed.
        const auto write_limit = 100_MiB;
        const bool write_limit_exceeded = bytes_written > write_limit;
        // AstraPro: No per-line flush in the hot path; retain existing error and
        // size-limit flushing and add bounded progress for abnormal process exits.
        const bool periodic_flush = flush_policy.Due(entry.timestamp);
        if (entry.log_level >= Level::Error || write_limit_exceeded || periodic_flush) {
            if (write_limit_exceeded) {
                // Stop writing after the write limit is exceeded.
                // Don't close the file so we can print a stacktrace if necessary
                enabled = false;
                healthy.store(false, std::memory_order_relaxed);
            }
            Flush();
        }
    }

    void Flush() override {
        // AstraEH: Idle timer wakeups cost no file operation when there are no new bytes.
        if (dirty) {
            if (!file->Flush())
                healthy.store(false, std::memory_order_relaxed);
            dirty = false;
        }
    }

    void Close() override {
        file->Close();
        enabled = false;
    }

    void EnableForStacktrace() override {
        enabled = true;
        bytes_written = 0;
    }

    bool Healthy() const {
        return healthy.load(std::memory_order_relaxed);
    }

private:
    std::unique_ptr<FileUtil::IOFile> file;
    bool enabled = true;
    bool dirty = false;
    std::size_t bytes_written = 0;
    Retention::FlushPolicy flush_policy;
    std::atomic_bool healthy{false};
};

/**
 * Backend that writes to Visual Studio's output window
 */
class DebuggerBackend final : public Backend {
public:
    explicit DebuggerBackend() = default;

    ~DebuggerBackend() override = default;

    void Write(const Entry& entry) override {
#ifdef _WIN32
        ::OutputDebugStringW(UTF8ToUTF16W(FormatLogMessage(entry).append(1, '\n')).c_str());
#endif
    }

    void Flush() override {}

    void Close() override {}

    void EnableForStacktrace() override {}
};

#ifdef ANDROID
/**
 * Backend that writes to the Android logcat
 */
class LogcatBackend : public Backend {
public:
    explicit LogcatBackend() = default;

    ~LogcatBackend() override = default;

    void Write(const Entry& entry) override {
        PrintMessageToLogcat(entry);
    }

    void Flush() override {}

    void Close() override {}

    void EnableForStacktrace() override {}
};
#endif

bool initialization_in_progress_suppress_logging = true;
bool logging_initialized = false;

#ifdef CITRA_LINUX_GCC_BACKTRACE
[[noreturn]] void SleepForever() {
    while (true) {
        pause();
    }
}
#endif

/**
 * Static state as a singleton.
 */
class Impl {
public:
    static Impl& Instance() {
        if (!instance) {
            throw std::runtime_error("Using Logging instance before its initialization");
        }
        return *instance;
    }
#ifdef HAVE_LIBRETRO
    static void Initialize(retro_log_printf_t callback) {
        if (instance) {
            LOG_WARNING(Log, "Reinitializing logging backend");
            return;
        }
        initialization_in_progress_suppress_logging = true;
        Filter filter;
        filter.ParseFilterString(Settings::values.log_filter.GetValue());
        instance = std::unique_ptr<Impl, decltype(&Deleter)>(new Impl(callback, filter), Deleter);
        initialization_in_progress_suppress_logging = false;
        logging_initialized = true;
    }
#endif
    static void Initialize(std::string_view log_file, bool rotate) {
        if (instance) {
            LOG_WARNING(Log, "Reinitializing logging backend");
            return;
        }
        initialization_in_progress_suppress_logging = true;
        const auto& log_dir = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
        void(FileUtil::CreateFullPath(log_dir));
        Filter filter;
        filter.ParseFilterString(Settings::values.log_filter.GetValue());
        instance = std::unique_ptr<Impl, decltype(&Deleter)>(
            new Impl(fmt::format("{}{}", log_dir, log_file), filter, rotate), Deleter);
        initialization_in_progress_suppress_logging = false;
        logging_initialized = true;
    }

    static void Start() {
        instance->StartBackendThread();
    }

    static bool FileHealthy() {
        return instance && instance->file_backend.Healthy();
    }

    static void Stop() {
        instance->StopBackendThread();
    }

    // AstraEH: Execute flushes on the logger thread, after all earlier queued messages.
    // A shared promise remains alive even if an export times out before it is processed.
    static bool Flush() {
        if (!logging_initialized || !instance) {
            return false;
        }
        Entry barrier{};
        barrier.flush_request = std::make_shared<std::promise<void>>();
        auto done = barrier.flush_request->get_future();
        // CodexAstraUlt-2: Both queue locks are attempted without waiting, so a stalled
        // producer cannot block crash handling before the five-second barrier timeout.
        if (!instance->message_queue.TryEmplace(std::move(barrier))) {
            return false;
        }
        return done.wait_for(std::chrono::seconds{5}) == std::future_status::ready;
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void SetGlobalFilter(const Filter& f) {
        filter = f;
    }

    bool SetRegexFilter(const std::string& regex) {
        if (regex.empty()) {
            regex_filter = boost::regex();
            return true;
        }
        regex_filter = boost::regex(regex, boost::regex_constants::no_except);
        if (regex_filter.status() != 0) {
            regex_filter = boost::regex();
            return false;
        }
        return true;
    }

    const Filter& GetFilter() const {
        return filter;
    }

    void SetColorConsoleBackendEnabled(bool enabled) {
        color_console_backend.SetEnabled(enabled);
    }

    void PushEntry(Class log_class, Level log_level, const char* filename, unsigned int line_num,
                   const char* function, std::string message, Delivery delivery) {
        Entry new_entry = CreateEntry(log_class, log_level, filename, line_num, function,
                                      std::move(message), time_origin);
        if (!regex_filter.empty() &&
            !boost::regex_search(FormatLogMessage(new_entry), regex_filter)) {
            return;
        }
        // CodexAstraUlt: Optional progress records cannot stall a rendering producer
        // behind slow sinks. Reliable messages keep existing FIFO/synchronous behavior.
        SubmitEntry(message_queue, std::move(new_entry), delivery,
                    Settings::values.instant_debug_log.GetValue(), diagnostic_omissions,
                    [this](const Entry& entry) {
                        ForEachBackend([&entry](Backend& backend) {
                            backend.Write(entry);
                            backend.Flush();
                        });
                    });
    }

    static Entry CreateEntry(Class log_class, Level log_level, const char* filename,
                             unsigned int line_nr, const char* function, std::string&& message,
                             const std::chrono::steady_clock::time_point& time_origin) {
        using std::chrono::duration_cast;
        using std::chrono::microseconds;
        using std::chrono::steady_clock;

        return {
            .timestamp = duration_cast<microseconds>(steady_clock::now() - time_origin),
            .log_class = log_class,
            .log_level = log_level,
            .filename = filename,
            .line_num = line_nr,
            .function = function,
            .message = std::move(message),
        };
    }

private:
#ifdef HAVE_LIBRETRO
    Impl(retro_log_printf_t callback, const Filter& filter_)
        : filter{filter_}, file_backend{""}, libretro_backend{callback} {}
#endif
    Impl(const std::string& file_backend_filename, const Filter& filter_, bool rotate)
        : filter{filter_}, file_backend{file_backend_filename, rotate} {
#ifdef CITRA_LINUX_GCC_BACKTRACE
        int waker_pipefd[2];
        int done_printing_pipefd[2];
        if (pipe2(waker_pipefd, O_CLOEXEC) || pipe2(done_printing_pipefd, O_CLOEXEC)) {
            abort();
        }
        backtrace_thread_waker_fd = waker_pipefd[1];
        backtrace_done_printing_fd = done_printing_pipefd[0];
        std::thread([this, wait_fd = waker_pipefd[0], done_fd = done_printing_pipefd[1]] {
            Common::SetCurrentThreadName("citra:Crash");
            for (u8 ignore = 0; read(wait_fd, &ignore, 1) != 1;)
                ;
            const int sig = received_signal;
            if (sig <= 0) {
                abort();
            }
            backend_thread.request_stop();
            backend_thread.join();
            const auto signal_entry = CreateEntry(
                Class::Log, Level::Critical, "?", 0, "?",
                fmt::vformat("Received signal {}", fmt::make_format_args(sig)), time_origin);
            ForEachBackend([&signal_entry](Backend& backend) {
                backend.EnableForStacktrace();
                backend.Write(signal_entry);
            });
            const auto backtrace =
                boost::stacktrace::stacktrace::from_dump(backtrace_storage.data(), 4096);
            for (const auto& frame : backtrace.as_vector()) {
                auto line = boost::stacktrace::detail::to_string(&frame, 1);
                if (line.empty()) {
                    abort();
                }
                line.pop_back(); // Remove newline
                const auto frame_entry = CreateEntry(Class::Log, Level::Critical, "?", 0, "?",
                                                     std::move(line), time_origin);
                ForEachBackend([&frame_entry](Backend& backend) { backend.Write(frame_entry); });
            }
            using namespace std::literals;
            const auto rip_entry =
                CreateEntry(Class::Log, Level::Critical, "?", 0, "?", "RIP"s, time_origin);
            ForEachBackend([&rip_entry](Backend& backend) {
                backend.Write(rip_entry);
                backend.Flush();
            });
            for (const u8 anything = 0; write(done_fd, &anything, 1) != 1;)
                ;
            // Abort on original thread to help debugging
            SleepForever();
        }).detach();
        signal(SIGSEGV, &HandleSignal);
        signal(SIGABRT, &HandleSignal);
#endif
    }

    ~Impl() {
#ifdef CITRA_LINUX_GCC_BACKTRACE
        if (int zero_or_ignore = 0;
            !received_signal.compare_exchange_strong(zero_or_ignore, SIGKILL)) {
            SleepForever();
        }
#endif
    }

    // CodexAstraUlt: Report optional omissions directly from the existing log worker,
    // never through a producer or an extra thread. An unavailable primary sink retains
    // its pending count; acknowledgement excludes any new omissions during this write.
    void ReportDiagnosticOmissions(bool force) {
        // CodexAstraUlt: Check the actual primary sink on each platform; a null
        // libretro callback retains counts just like an unavailable ordinary file.
        const auto sink_available = [this] {
#ifdef HAVE_LIBRETRO
            return libretro_backend.Available();
#else
            return file_backend.Healthy();
#endif
        };
        if (!diagnostic_omissions.Pending() || !sink_available()) {
            return;
        }
        const auto count = diagnostic_omissions.Due(std::chrono::steady_clock::now(), force);
        if (!count) {
            return;
        }
        // CodexAstraUlt Log Line: At most one per five seconds plus explicit flush/stop;
        // only opted-in diagnostics were omitted, not lifecycle/error records or draws.
        const auto report = CreateEntry(
            Class::Log, Level::Warning, TrimSourcePath(__FILE__), __LINE__, __func__,
            fmt::format("Uberhar log omissions: schema=1 optional_records={} "
                        "reason=queue_full_or_contended scope=since_previous_report",
                        count),
            time_origin);
        ForEachBackend([&report](Backend& backend) { backend.Write(report); });
        if (sink_available()) {
            diagnostic_omissions.Acknowledge(count);
        }
    }

    void StartBackendThread() {
        backend_thread = std::jthread([this](std::stop_token stop_token) {
            Common::SetCurrentThreadName("citra:Log");
            Entry entry;
            const auto write_logs = [this, &entry]() {
                // CodexAstraUlt: A flush/export includes pending omission evidence
                // before its barrier acknowledges; ordinary records keep the cadence.
                ReportDiagnosticOmissions(static_cast<bool>(entry.flush_request));
                // AstraEH: Barriers are control messages, never formatted as log lines.
                if (entry.flush_request) {
                    ForEachBackend([](Backend& backend) { backend.Flush(); });
                    entry.flush_request->set_value();
                    return;
                }
                ForEachBackend([&entry](Backend& backend) { backend.Write(entry); });
            };
            while (!stop_token.stop_requested()) {
                // AstraEH: A cancelled PopWait leaves its output untouched; clear the
                // previous entry so shutdown cannot acknowledge a flush barrier twice.
                entry = {};
                if (!message_queue.PopWaitFor(entry, stop_token, std::chrono::seconds{1})) {
                    // CodexAstraUlt: Reuse the existing quiet-tail wakeup for summaries.
                    ReportDiagnosticOmissions(false);
                    // CodexAstraUlt: Optional records remain queued in synchronous debug
                    // mode, so its producers and this idle flush share the sink lock.
                    BackendAccessGuard lock{backend_mutex};
                    file_backend.Flush();
                    continue;
                }
                // Only write the log if something was actually popped (entry.filename != nullptr)
                // (for example, when the stop token is signaled).
                if (entry.filename != nullptr || entry.flush_request) {
                    write_logs();
                }
            }
            // Drain the logging queue. Only writes out up to MAX_LOGS_TO_WRITE to prevent a
            // case where a system is repeatedly spamming logs even on close.
            int max_logs_to_write = filter.IsDebug() ? INT_MAX : 100;
            while (max_logs_to_write-- && message_queue.TryPop(entry)) {
                write_logs();
            }
            // CodexAstraUlt: Final accounting precedes the existing backend flush/close.
            ReportDiagnosticOmissions(true);
        });
    }

    void StopBackendThread() {
        backend_thread.request_stop();
        if (backend_thread.joinable()) {
            backend_thread.join();
        }

        ForEachBackend([](Backend& backend) {
            backend.Flush();
            backend.Close();
        });
    }

    void ForEachBackend(auto lambda) {
        // CodexAstraUlt: Protect sink state from explicit synchronous producers and
        // the async worker. Never enqueue or wait for queue capacity under this lock.
        BackendAccessGuard lock{backend_mutex};
#ifdef HAVE_LIBRETRO
        lambda(static_cast<Backend&>(libretro_backend));
#else
        lambda(static_cast<Backend&>(debugger_backend));
        lambda(static_cast<Backend&>(color_console_backend));
        lambda(static_cast<Backend&>(file_backend));
#ifdef ANDROID
        lambda(static_cast<Backend&>(lc_backend));
#endif // ANDROID
#endif // HAVE_LIBRETRO
    }

    static void Deleter(Impl* ptr) {
        delete ptr;
    }

#ifdef CITRA_LINUX_GCC_BACKTRACE
    [[noreturn]] static void HandleSignal(int sig) {
        signal(SIGABRT, SIG_DFL);
        signal(SIGSEGV, SIG_DFL);
        // CodexAstraUlt: A thread faulting while acquiring/holding the sink lock
        // cannot park for a helper that joins/locks the same backend. Abort on the
        // original thread without any IO; this exceptional path loses the custom
        // text backtrace but leaves OS/core collection possible, never guaranteed.
        if (backend_access_active) {
            abort();
        }
        if (sig <= 0) {
            abort();
        }
        instance->InstanceHandleSignal(sig);
    }

    [[noreturn]] void InstanceHandleSignal(int sig) {
        if (int zero_or_ignore = 0; !received_signal.compare_exchange_strong(zero_or_ignore, sig)) {
            if (received_signal == SIGKILL) {
                abort();
            }
            SleepForever();
        }
        // Don't restart like boost suggests. We want to append to the log file and not lose dynamic
        // symbols. This may segfault if it unwinds outside C/C++ code but we'll just have to fall
        // back to core dumps.
        boost::stacktrace::safe_dump_to(backtrace_storage.data(), 4096);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        for (const int anything = 0; write(backtrace_thread_waker_fd, &anything, 1) != 1;)
            ;
        for (u8 ignore = 0; read(backtrace_done_printing_fd, &ignore, 1) != 1;)
            ;
        abort();
    }
#endif

    static inline std::unique_ptr<Impl, decltype(&Deleter)> instance{nullptr, Deleter};

    Filter filter;
    boost::regex regex_filter;
    DebuggerBackend debugger_backend{};
    ColorConsoleBackend color_console_backend{};
    FileBackend file_backend;
#ifdef ANDROID
    LogcatBackend lc_backend{};
#endif
#ifdef HAVE_LIBRETRO
    LibRetroBackend libretro_backend;
#endif

    MPSCQueue<Entry> message_queue{};
    DiagnosticDropCounter diagnostic_omissions; // CodexAstraUlt: Fixed-size optional-loss evidence.
    std::mutex backend_mutex; // CodexAstraUlt: Optional producers never acquire this mutex.
    std::chrono::steady_clock::time_point time_origin{std::chrono::steady_clock::now()};
    std::jthread backend_thread;

#ifdef CITRA_LINUX_GCC_BACKTRACE
    std::atomic_int received_signal{0};
    std::array<u8, 4096> backtrace_storage{};
    int backtrace_thread_waker_fd;
    int backtrace_done_printing_fd;
#endif
};
} // namespace

#ifdef HAVE_LIBRETRO
void LibRetroStart(retro_log_printf_t callback) {
    Impl::Initialize(callback);
    Impl::Start();
}
#endif

void Initialize(std::string_view log_file, bool rotate) {
    Impl::Initialize(log_file.empty() ? LOG_FILE : log_file, rotate);
}

bool FileHealthy() {
    return Impl::FileHealthy();
}

void Start() {
    Impl::Start();
}

// AstraEH: Public export barrier; the logging instance owns its queue and file handles.
bool Flush() {
    return Impl::Flush();
}

void Stop() {
    if (logging_initialized) {
        Impl::Stop();
    }
}

void DisableLoggingInTests() {
    initialization_in_progress_suppress_logging = true;
}

void SetGlobalFilter(const Filter& filter) {
    Impl::Instance().SetGlobalFilter(filter);
}

bool SetRegexFilter(const std::string& regex) {
    return Impl::Instance().SetRegexFilter(regex);
}

void SetColorConsoleBackendEnabled(bool enabled) {
    Impl::Instance().SetColorConsoleBackendEnabled(enabled);
}

void FmtLogMessageImpl(Class log_class, Level log_level, const char* filename,
                       unsigned int line_num, const char* function, fmt::string_view format,
                       const fmt::format_args& args) {
    // CodexAstraUlt: Retain the original reliable ABI used by existing callers and
    // host probe stubs; the delivery extension does not replace their entry point.
    FmtLogMessageWithDeliveryImpl(log_class, log_level, filename, line_num, function, format, args,
                                  Delivery::Reliable);
}

// CodexAstraUlt: Filtering and record formatting remain shared by both deliveries.
void FmtLogMessageWithDeliveryImpl(Class log_class, Level log_level, const char* filename,
                                   unsigned int line_num, const char* function,
                                   fmt::string_view format, const fmt::format_args& args,
                                   Delivery delivery) {
    if (initialization_in_progress_suppress_logging && log_level < Level::Critical) [[unlikely]] {
        return;
    }

    if (logging_initialized) [[likely]] {
        if (!Impl::Instance().GetFilter().CheckMessage(log_class, log_level)) {
            return;
        }
        Impl::Instance().PushEntry(log_class, log_level, filename, line_num, function,
                                   fmt::vformat(format, args), delivery);
    } else {
        // In the rare case that logging occurs before initialization, write the
        // message to stderr to preserve useful debug information.
        Entry new_entry = Impl::CreateEntry(log_class, log_level, filename, line_num, function,
                                            fmt::vformat(format, args), {});
        PrintMessage(new_entry);
    }
}
} // namespace Common::Log
