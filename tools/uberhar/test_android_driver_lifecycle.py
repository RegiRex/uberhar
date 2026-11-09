#!/usr/bin/env python3
"""CodexAstraLocal: Exercise the production driver replacement gate and fallback with owned library users."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
# CodexAstraLocal: CI uses its checked-out dependency; local qualification may
# point at an already present pinned header tree without changing source inputs.
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fmt-include', type=Path, default=root / 'externals/fmt/include')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
source = (root / 'src/android/app/src/main/jni/native.cpp').read_text()
start = source.index('int InitializeGpuDriver(')
end = source.index('\nextern "C"', start)
production = source[start:end]
fixture = r'''
#include <cassert>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <fmt/format.h>
#include "common/uberhar_tuning_sample.h"
#define ENABLE_VULKAN
#define CITRA_ARCH(x) 1
#define LOG_WARNING(...)
#define LOG_ERROR(...)
#define LOG_INFO(...)
#define RTLD_NOW 2
#define ADRENOTOOLS_DRIVER_FILE_REDIRECT 1
#define ADRENOTOOLS_DRIVER_CUSTOM 2
namespace Settings { struct { bool renderer_debug{}; } values; }
namespace Common {
struct DynamicLibrary {
    explicit DynamicLibrary(void* h) : handle(h) {}
    void* handle;
};
}
std::mutex running_mutex;
std::shared_ptr<Common::DynamicLibrary> vulkan_library;
std::vector<void*> replies;
std::vector<int> flags;
void* adrenotools_open_libvulkan(int, int flag, void*, const char*, const char*, const char*, const char*, void*) {
    flags.push_back(flag);
    assert(!replies.empty());
    auto value = replies.front(); replies.erase(replies.begin()); return value;
}
'''
main = r'''
// CodexAstraLocal: Observe the real tuning endpoint through its public snapshot,
// retaining actual library fallback identity rather than a permissive no-op stub.
std::string LibraryContext() {
    auto& sample = Common::UberharTuningSample::Instance();
    sample.SetDriver("fixture-driver");
    sample.BeginRun(1, "fixture-title", "fixture", 0, 2, true);
    return sample.Snapshot().context;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    Common::UberharTuningSample::Instance().SetLibrary("prior");
    auto* old_handle = reinterpret_cast<void*>(1);
    auto* custom_handle = reinterpret_cast<void*>(2);
    auto* system_handle = reinterpret_cast<void*>(3);
    vulkan_library = std::make_shared<Common::DynamicLibrary>(old_handle);
    auto window_owner = vulkan_library;
    std::promise<void> acquired, finish;
    auto ready = acquired.get_future(); auto release = finish.get_future();
    std::thread teardown([&] {
        std::lock_guard guard(running_mutex);
        acquired.set_value(); release.wait();
    });
    ready.wait();
    assert(!InitializeGpuDriver("hook", "custom/", "libcustom.so", "redirect"));
    assert(flags.empty() && vulkan_library == window_owner);
    assert(LibraryContext().ends_with(";library=prior"));
    finish.set_value(); teardown.join();
    replies = {custom_handle};
    assert(InitializeGpuDriver("hook", "custom/", "libcustom.so", "redirect") == 2);
    assert(vulkan_library->handle == custom_handle && window_owner->handle == old_handle);
    assert(flags.size() == 1 && (flags[0] & ADRENOTOOLS_DRIVER_CUSTOM));
    assert(LibraryContext().ends_with(";library=custom//libcustom.so"));
    flags.clear(); replies = {nullptr, system_handle};
    assert(InitializeGpuDriver("hook", "missing/", "libmissing.so", "redirect") == 1);
    assert(vulkan_library->handle == system_handle && flags.size() == 2);
    assert(!(flags[1] & ADRENOTOOLS_DRIVER_CUSTOM));
    assert(LibraryContext().ends_with(";library=System"));
    flags.clear(); replies = {nullptr}; auto retained = vulkan_library;
    assert(!InitializeGpuDriver("hook", "", "", "redirect"));
    assert(vulkan_library == retained);
    assert(LibraryContext().ends_with(";library=System"));
    flags.clear(); replies = {system_handle};
    assert(InitializeGpuDriver("hook", "", "", "redirect"));
    assert(flags.size() == 1 && !(flags[0] & ADRENOTOOLS_DRIVER_CUSTOM));
    assert(LibraryContext().ends_with(";library=System"));
    // CodexAstraLocal: Also execute the successful real stat/format path. The
    // fixture file is host-owned synthetic bytes, never a driver/device library.
    flags.clear(); replies = {custom_handle};
    assert(InitializeGpuDriver("hook", argv[1], "driver.so", "redirect") == 2);
    const auto context = LibraryContext();
    assert(context.find(std::string(";library=") + argv[1] + "/driver.so;size=11;mtime=") != std::string::npos);
    assert(vulkan_library->handle == custom_handle && window_owner->handle == old_handle);
}
'''
def check(path):
    path.mkdir(parents=True, exist_ok=True)
    (path / 'fixture.cpp').write_text(fixture + production + main)
    (path / 'driver.so').write_bytes(b'fixtureonly')
    inputs = [Path(__file__), root / 'src/android/app/src/main/jni/native.cpp',
              root / 'src/common/uberhar_tuning_sample.h', args.fmt_include / 'fmt/format.h']
    proof = {'author': 'CodexAstraLocal', 'source_sha256': {
        str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        'production_sha256': hashlib.sha256(production.encode()).hexdigest(), 'passed': False}
    command = ['c++', '-std=c++20', '-pthread', '-DFMT_HEADER_ONLY',
               '-I' + str(root / 'src'), '-I' + str(args.fmt_include),
               str(path/'fixture.cpp'), '-o', str(path/'fixture')]
    try:
        compiled = subprocess.run(command, capture_output=True, timeout=90)
        (path / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
        proof.update(compile_command=command, compile_exit=compiled.returncode)
        # CodexAstraLocal: A failed CI gate must expose the real compiler error
        # in its console even if artifact upload is unavailable.
        if compiled.returncode:
            print((compiled.stdout + compiled.stderr).decode(errors='replace'), flush=True)
        compiled.check_returncode()
        run_command = [str(path/'fixture'), str(path)]
        ran = subprocess.run(run_command, capture_output=True, timeout=10)
        (path / 'run.log').write_bytes(ran.stdout + ran.stderr)
        proof.update(run_command=run_command, run_exit=ran.returncode)
        if ran.returncode:
            print((ran.stdout + ran.stderr).decode(errors='replace'), flush=True)
        ran.check_returncode()
        proof['passed'] = True
    finally:
        (path / 'RESULT.json').write_text(json.dumps(proof, indent=2) + '\n')

if args.output:
    check(args.output.resolve())
else:
    with tempfile.TemporaryDirectory(prefix='uberhar-driver-lifecycle-') as tmp:
        check(Path(tmp))
print('PASS: production driver lifecycle refusal, shared-owner retention, custom/system fallback and load failure; actual tuning-library identity and file metadata')
