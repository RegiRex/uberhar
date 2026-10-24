#!/usr/bin/env python3
"""CodexAstraLocal: Exercise the production driver replacement gate and fallback with owned library users."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
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
int main() {
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
    finish.set_value(); teardown.join();
    replies = {custom_handle};
    assert(InitializeGpuDriver("hook", "custom/", "libcustom.so", "redirect") == 2);
    assert(vulkan_library->handle == custom_handle && window_owner->handle == old_handle);
    assert(flags.size() == 1 && (flags[0] & ADRENOTOOLS_DRIVER_CUSTOM));
    flags.clear(); replies = {nullptr, system_handle};
    assert(InitializeGpuDriver("hook", "missing/", "libmissing.so", "redirect") == 1);
    assert(vulkan_library->handle == system_handle && flags.size() == 2);
    assert(!(flags[1] & ADRENOTOOLS_DRIVER_CUSTOM));
    flags.clear(); replies = {nullptr}; auto retained = vulkan_library;
    assert(!InitializeGpuDriver("hook", "", "", "redirect"));
    assert(vulkan_library == retained);
    flags.clear(); replies = {system_handle};
    assert(InitializeGpuDriver("hook", "", "", "redirect"));
    assert(flags.size() == 1 && !(flags[0] & ADRENOTOOLS_DRIVER_CUSTOM));
}
'''
with tempfile.TemporaryDirectory(prefix='uberhar-driver-lifecycle-') as tmp:
    path = Path(tmp)
    (path / 'fixture.cpp').write_text(fixture + production + main)
    subprocess.run(['c++', '-std=c++20', '-pthread', str(path/'fixture.cpp'), '-o', str(path/'fixture')], check=True)
    subprocess.run([str(path/'fixture')], check=True, timeout=10)
print('PASS: production driver lifecycle refusal, shared-owner retention, custom/system fallback and load failure')
