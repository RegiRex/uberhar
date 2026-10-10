// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Share the native capture/sampling regression verbatim. Only
// the whole guest-system triangle case needs the separately linked CTest build.
#define UBERHAR_SOFTWARE_HELPER_PROBE
#include "../../src/tests/video_core/software_renderer.cpp"
#include "common/logging/log.h"

// CodexAstraLocal: This standalone fixture has no logging service. Unexpected
// production error diagnostics fail it; normal messages need no global sink.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) {
        throw std::runtime_error(fmt::vformat(format, args));
    }
}
} // namespace Common::Log
