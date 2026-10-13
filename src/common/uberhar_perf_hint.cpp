// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#include "common/uberhar_perf_hint.h"

#if defined(UBERHAR_ATRACE)
#include <algorithm>
#include <mutex>
#include <type_traits>
#include <vector>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#include "common/logging/log.h"
#include "common/uberhar_trace.h"

namespace Common::Uberhar::PerfHint {
namespace {
struct Manager;
struct Session;
// Claude: Resolved at runtime because the app's minimum SDK (29) predates the
// API (33); setThreads arrived in 34 and is optional.
struct Api {
    Manager* (*get_manager)();
    Session* (*create)(Manager*, const std::int32_t*, std::size_t, std::int64_t);
    int (*update_target)(Session*, std::int64_t);
    int (*report)(Session*, std::int64_t);
    void (*close)(Session*);
    int (*set_threads)(Session*, const pid_t*, std::size_t);
    bool ok{};
};

Api Load() {
    Api api{};
    void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return api;
    const auto find = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
    };
    find(api.get_manager, "APerformanceHint_getManager");
    find(api.create, "APerformanceHint_createSession");
    find(api.update_target, "APerformanceHint_updateTargetWorkDuration");
    find(api.report, "APerformanceHint_reportActualWorkDuration");
    find(api.close, "APerformanceHint_closeSession");
    find(api.set_threads, "APerformanceHint_setThreads");
    api.ok = api.get_manager && api.create && api.update_target && api.report && api.close;
    return api;
}

struct State {
    std::mutex mutex;
    std::vector<std::int32_t> threads;
    bool dirty{};
    Session* session{};
    std::int64_t target{};
    unsigned failures{};
};

State& Global() {
    // Claude: Never destroyed; registered threads may exit after static teardown.
    static State* const state = new State;
    return *state;
}

const Api& Functions() {
    static const Api api = Load();
    return api;
}

struct Registration {
    std::int32_t tid{};
    ~Registration() {
        if (!tid)
            return;
        auto& state = Global();
        std::lock_guard lock{state.mutex};
        std::erase(state.threads, tid);
        state.dirty = true;
    }
};
} // namespace

void RegisterCurrentThread() {
    if (!Functions().ok)
        return;
    thread_local Registration registration;
    if (registration.tid)
        return;
    registration.tid = static_cast<std::int32_t>(gettid());
    auto& state = Global();
    std::lock_guard lock{state.mutex};
    state.threads.push_back(registration.tid);
    state.dirty = true;
}

void ReportFrame(std::int64_t work_ns, std::int64_t target_ns) {
    const auto& api = Functions();
    if (!api.ok || work_ns <= 0 || target_ns <= 0)
        return;
    RegisterCurrentThread();
    auto& state = Global();
    std::lock_guard lock{state.mutex};
    if (state.dirty && !state.threads.empty() && state.failures < 8) {
        state.dirty = false;
        bool updated = false;
        if (state.session && api.set_threads) {
            std::vector<pid_t> tids(state.threads.begin(), state.threads.end());
            updated = api.set_threads(state.session, tids.data(), tids.size()) == 0;
        }
        if (!updated) {
            if (state.session)
                api.close(state.session);
            state.session = nullptr;
            if (auto* manager = api.get_manager()) {
                state.session = api.create(manager, state.threads.data(), state.threads.size(),
                                           target_ns);
                state.target = target_ns;
            }
            if (!state.session) {
                // Claude Log Line: Bounded; the platform refused a hint session.
                if (++state.failures <= 2)
                    LOG_WARNING(Core, "Claude: performance hint session unavailable threads={}",
                                state.threads.size());
                return;
            }
            // Claude Log Line: Once per thread-set change.
            LOG_INFO(Core, "Claude: performance hint session threads={} target_ms={:.2f}",
                     state.threads.size(), target_ns / 1e6);
        }
    }
    if (!state.session)
        return;
    if (target_ns != state.target && api.update_target(state.session, target_ns) == 0)
        state.target = target_ns;
    api.report(state.session, work_ns);
    TraceCounter("Uberhar: frame work us", work_ns / 1000);
}
} // namespace Common::Uberhar::PerfHint
#endif
