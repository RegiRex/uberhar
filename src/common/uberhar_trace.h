// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <cstdint>
#include <initializer_list>

// Claude: Named slices and counters for Perfetto/systrace captures on Android.
// Each call first asks the platform whether a trace is recording, so normal play
// pays one cheap check per marker. Other hosts compile every marker away.
namespace Common::Uberhar {
#if defined(UBERHAR_ATRACE)
bool TraceEnabled();
void TraceBegin(const char* name);
void TraceEnd();
void TraceCounter(const char* name, std::int64_t value);
#else
inline bool TraceEnabled() { return false; }
inline void TraceBegin(const char*) {}
inline void TraceEnd() {}
inline void TraceCounter(const char*, std::int64_t) {}
#endif

// Claude: A MicroProfile token reused as a trace slice name. Per-vertex,
// per-triangle, per-SVC and per-instruction scopes would turn a trace into
// millions of markers and distort timing, so they stay silent.
struct TraceToken {
    const char* name;
    bool coarse;
    constexpr explicit TraceToken(const char* name_) : name{name_}, coarse{IsCoarse(name_)} {}

private:
    static constexpr bool Same(const char* a, const char* b) {
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        return *a == *b;
    }
    static constexpr bool IsCoarse(const char* name) {
        for (const char* fine : {"GPU: Shader", "GPU: Rasterization", "Kernel: SVC",
                                 "DynCom: Decode", "DynCom: execute"}) {
            if (Same(name, fine))
                return false;
        }
        return true;
    }
};

class TraceScope {
public:
    explicit TraceScope(const char* name) : active{TraceEnabled()} {
        if (active)
            TraceBegin(name);
    }
    explicit TraceScope(const TraceToken& token) : active{token.coarse && TraceEnabled()} {
        if (active)
            TraceBegin(token.name);
    }
    ~TraceScope() {
        if (active)
            TraceEnd();
    }
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;

private:
    const bool active;
};
} // namespace Common::Uberhar

#define UBERHAR_TRACE_JOIN2(a, b) a##b
#define UBERHAR_TRACE_JOIN(a, b) UBERHAR_TRACE_JOIN2(a, b)
// Claude: One slice covering the rest of the enclosing scope. Names must be literals.
#define UBERHAR_TRACE(name)                                                                      \
    ::Common::Uberhar::TraceScope UBERHAR_TRACE_JOIN(uberhar_trace_, __LINE__) { name }
