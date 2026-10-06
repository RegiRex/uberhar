// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <mutex>
#ifdef CITRA_LINUX_GCC_BACKTRACE
#include <csignal>
#endif

namespace Common::Log {

#ifdef CITRA_LINUX_GCC_BACKTRACE
// CodexAstraUlt: The existing Linux fatal handler cannot wait on a helper when
// the faulting thread may own a sink lock. Signal-safe thread-local state covers
// both mutex acquisition and ownership without adding Android hot-path tracking.
inline thread_local volatile std::sig_atomic_t backend_access_active = 0;

class BackendSignalScope {
public:
    BackendSignalScope() : previous{backend_access_active} {
        backend_access_active = 1;
    }
    ~BackendSignalScope() {
        backend_access_active = previous;
    }

private:
    std::sig_atomic_t previous;
};
#endif

// CodexAstraUlt: Member construction marks the scope before waiting for the lock;
// reverse destruction releases the lock before clearing the mark, even on exceptions.
class BackendAccessGuard {
public:
    explicit BackendAccessGuard(std::mutex& mutex) : lock{mutex} {}

    // CodexAstraUlt: The signal mark belongs to this stack scope, never a moved guard.
    BackendAccessGuard(const BackendAccessGuard&) = delete;
    BackendAccessGuard& operator=(const BackendAccessGuard&) = delete;

private:
#ifdef CITRA_LINUX_GCC_BACKTRACE
    BackendSignalScope signal_scope;
#endif
    std::unique_lock<std::mutex> lock;
};

} // namespace Common::Log
