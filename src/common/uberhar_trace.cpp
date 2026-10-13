// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#include "common/uberhar_trace.h"

#if defined(UBERHAR_ATRACE)
#include <android/trace.h>

namespace Common::Uberhar {
// Claude: minSdk 29 provides all four NDK calls; no dlsym fallback is needed.
bool TraceEnabled() {
    return ATrace_isEnabled();
}
void TraceBegin(const char* name) {
    ATrace_beginSection(name);
}
void TraceEnd() {
    ATrace_endSection();
}
void TraceCounter(const char* name, std::int64_t value) {
    if (ATrace_isEnabled())
        ATrace_setCounter(name, value);
}
} // namespace Common::Uberhar
#endif
