#!/usr/bin/env python3
"""CodexAstraUlt: Exercise production APT logging and IPC bodies with a minimal request adapter."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/core/hle/service/apt/apt.cpp').read_text()
header = (root / 'src/core/hle/service/apt/apt.h').read_text()


# CodexAstraUlt: Compile actual production bodies/counter declarations, not a second logging policy.
def body(signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]


counters = '\n'.join(re.findall(r'    std::array<u64, 2> applet_utility_\w+\{\};', header))
assert len(counters.splitlines()) == 2
serialization = body('void Module::serialize(')
assert 'applet_utility_requests' not in serialization
assert 'applet_utility_logs' not in serialization

prefix = r'''
// CodexAstraUlt: The adapter records warning arguments and guest IPC outputs; production code supplies decisions.
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u32 ResultSuccess = 0;
struct Entry { bool warning; std::string format; std::vector<u64> values; };
std::vector<Entry> entries;
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template <class... Args> void Record(bool warning, const char* format, Args... args) {
    entries.push_back({warning, format, {static_cast<u64>(args)...}});
}
#define LOG_WARNING(category, ...) Record(true, __VA_ARGS__)
#define LOG_INFO(category, ...) Record(false, __VA_ARGS__)
namespace Kernel {
struct HLERequestContext {
    std::array<u32, 3> words;
    std::vector<u8> input, output;
    std::vector<u32> results;
};
}
namespace IPC {
struct RequestBuilder {
    Kernel::HLERequestContext& ctx;
    void Push(u32 value) { ctx.results.push_back(value); }
    void PushStaticBuffer(const std::vector<u8>& value, u32 slot) {
        Check(slot == 0, "Output buffer slot changed");
        ctx.output = value;
    }
};
struct RequestParser {
    Kernel::HLERequestContext& ctx;
    std::size_t index = 0;
    template <class T> T Pop() { return static_cast<T>(ctx.words.at(index++)); }
    std::vector<u8> PopStaticBuffer() { return ctx.input; }
    RequestBuilder MakeBuilder(u32 normal, u32 translate) {
        Check(normal == 2 && translate == 2, "IPC response header changed");
        return {ctx};
    }
};
}
namespace Service::APT {
class Module {
public:
    ~Module();
    void LogAppletUtilityCall(u32 command, u32 input_size, u32 output_size, std::size_t actual_input_size);
    struct APTInterface {
        Module* apt;
        void AppletUtility(Kernel::HLERequestContext& ctx);
    };
'''

suffix = r'''
}
using Service::APT::Module;
// CodexAstraUlt: Check unchanged guest replies on every request, including suppressed warnings and malformed metadata.
void Call(Module& module, u32 command, u32 input, u32 output, std::size_t actual) {
    Kernel::HLERequestContext ctx{{command, input, output}, std::vector<u8>(actual, 0xA5), {}, {}};
    Module::APTInterface{&module}.AppletUtility(ctx);
    Check(ctx.results == std::vector<u32>({0, 0}), "Guest result words changed");
    Check(ctx.output.size() == output, "Guest output size changed");
    for (std::size_t i = 0; i < ctx.output.size(); ++i)
        Check(ctx.output[i] == (command == 6 && i == 0 ? 1 : 0), "Guest output bytes changed");
    Check(ctx.input == std::vector<u8>(actual, 0xA5), "Guest input bytes changed");
}
int main() {
    // CodexAstraUlt: Replay the measured MSR counts; retain the first four and every later power-of-two census point.
    const std::vector<u64> expected{1, 2, 3, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    {
        Module module;
        for (u32 i = 0; i < 8991; ++i) Call(module, 4, 1, 1, 1);
        for (u32 i = 0; i < 8990; ++i) Call(module, 7, 4, 1, 4);
        Check(entries.size() == 30, "Known requests emitted unexpected warning count");
        for (std::size_t slot = 0; slot < 2; ++slot) {
            for (std::size_t i = 0; i < expected.size(); ++i) {
                const auto& entry = entries[slot * expected.size() + i];
                Check(entry.warning && entry.values.size() == 5, "Known warning fields changed");
                Check(entry.values[0] == (slot == 0 ? 4 : 7), "Counters crossed signatures");
                Check(entry.values[3] == expected[i], "First-four/power-of-two cadence changed");
                Check(entry.values[4] == expected[i] - i - 1, "Suppressed census count changed");
            }
        }
    }
    Check(entries.size() == 32, "Final per-signature summaries missing or duplicated");
    Check(!entries[30].warning && entries[30].values == std::vector<u64>({4, 1, 8991, 15, 8976, 0}),
          "Command4 summary differs");
    Check(!entries[31].warning && entries[31].values == std::vector<u64>({7, 4, 8990, 15, 8975, 0}),
          "Command7 summary differs");

    // CodexAstraUlt: A new Module resets its host diagnostics; unused Modules emit nothing.
    entries.clear();
    { Module unused; }
    Check(entries.empty(), "Unused Module emitted a census");
    {
        Module next;
        Call(next, 4, 1, 1, 1);
        Call(next, 7, 4, 1, 4);
        Check(entries.size() == 2 && entries[0].values[3] == 1 && entries[1].values[3] == 1,
              "Module diagnostics did not reset");
    }
    Check(entries.size() == 4, "New Module shutdown summary count changed");

    // CodexAstraUlt: Unknown constants, changed signatures and actual-buffer mismatches always retain their warning.
    entries.clear();
    {
        Module module;
        const std::array<std::array<u32, 4>, 12> variants{{
            {{4, 0, 1, 0}}, {{4, 2, 1, 2}}, {{4, 1, 0, 1}}, {{4, 1, 2, 1}},
            {{7, 1, 1, 1}}, {{7, 4, 2, 4}}, {{0xDEAD, 4, 1, 4}}, {{4, 1, 1, 0}},
            {{7, 4, 1, 3}}, {{4, 1, 1, 2}}, {{6, 1, 5, 1}}, {{6, 1, 0, 1}}
        }};
        for (const auto& signature : variants) {
            for (unsigned repeat = 0; repeat < 17; ++repeat) {
                const auto before = entries.size();
                Call(module, signature[0], signature[1], signature[2], signature[3]);
                Check(entries.size() == before + 1 && entries.back().warning &&
                      entries.back().values.size() == 3, "Other signature warning was suppressed");
            }
        }
        Check(module.applet_utility_requests == std::array<u64, 2>{0, 0},
              "Other signature polluted bounded counters");
    }
    Check(entries.size() == 204, "Other signatures unexpectedly emitted bounded summaries");

    // CodexAstraUlt: Overflow cannot reset counters or restart a warning flood.
    entries.clear();
    {
        Module module;
        module.applet_utility_requests[0] = std::numeric_limits<u64>::max() - 1;
        module.applet_utility_logs[0] = 65;
        Call(module, 4, 1, 1, 1);
        Call(module, 4, 1, 1, 1);
        Check(module.applet_utility_requests[0] == std::numeric_limits<u64>::max(), "Counter wrapped");
        Check(entries.empty(), "Saturated counter emitted another warning");
    }
    Check(entries.size() == 1 && entries[0].values.back() == 1, "Saturation summary missing");
    std::printf("PASS: %u checks; production APT IPC replies, exact MSR warning census, Module reset, "
                "other signatures, malformed buffers, command6 response and counter saturation\n", checks);
}
'''

# CodexAstraUlt: Temporary build outputs stay outside tracked source and require no Android SDK or generated stubs.
production = '\n\n'.join([body('void Module::LogAppletUtilityCall('),
                           body('void Module::APTInterface::AppletUtility('),
                           body('Module::~Module()')])
with tempfile.TemporaryDirectory(prefix='uberhar-apt-log-') as temporary:
    cpp = Path(temporary) / 'test.cpp'
    binary = Path(temporary) / 'test'
    cpp.write_text(prefix + counters + '\n};\n' + production + suffix)
    subprocess.run(['c++', '-std=c++20', '-O2', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
