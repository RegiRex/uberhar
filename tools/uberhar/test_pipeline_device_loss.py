#!/usr/bin/env python3
"""CodexAstraUlt-2: Execute the production pipeline completion/category boundary."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from test_ready_cpu_fragments import function


def main():
    # CodexAstraUlt-2: Driver errors are controlled endpoints; the catch ordering,
    # atomic publication and failure completion come from the actual C++ method.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output or Path(tempfile.mkdtemp(prefix='uberhar-pipeline-device-loss-'))
    out.mkdir(parents=True, exist_ok=True)
    body = function((root / 'src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp').read_text(),
                    'bool GraphicsPipeline::Build(bool fail_on_compile_required)')
    # CodexAstraUlt-2: Execute the real scheduler latch and initial destructor
    # drain as well; a reported startup failure must never resubmit or wait idle.
    scheduler_header = (root / 'src/video_core/renderer_vulkan/vk_scheduler.h').read_text()
    scheduler_methods = '\n'.join(function(scheduler_header, signature) for signature in (
        'void ReportDeviceLost()', 'bool DeviceLost() const', 'void BeginShutdown() noexcept'))
    finish = function((root / 'src/video_core/renderer_vulkan/vk_scheduler.cpp').read_text(),
                      'void Scheduler::Finish(vk::Semaphore signal, vk::Semaphore wait)')
    teardown = function((root / 'src/video_core/renderer_vulkan/renderer_vulkan.cpp').read_text(),
                        'RendererVulkan::~RendererVulkan()')
    teardown = teardown[teardown.index('{') + 1:teardown.index('    device.destroyShaderModule')]
    fixture = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <cstdio>
#include "video_core/shader_build_failure.h"
using u32 = std::uint32_t;
using u64 = std::uint64_t;
bool throwing_log{};
void Log() { if (throwing_log) throw std::bad_alloc{}; }
#define LOG_ERROR(...) Log()
namespace vk {
using Semaphore = int;
inline unsigned idle_waits{};
struct Device { void waitIdle() { ++idle_waits; } };
enum class Result { eErrorDeviceLost = -4 };
struct SystemError : std::system_error {
    explicit SystemError(int code) : std::system_error(code, std::generic_category()) {}
};
}
class Scheduler {
public:
    VideoCore::ShaderFailureState shader_failure;
    std::atomic_bool device_lost{};
    unsigned submissions{}, drains{}, waits{};
    // UBERHAR_SCHEDULER_METHODS
    u64 CurrentTick() const { return 1; }
    void SubmitExecution(int, int) { ++submissions; }
    void Wait(u64) { ++waits; }
    void WaitWorker() { ++drains; shader_failure.Check(); }
    void Finish(vk::Semaphore signal = 0, vk::Semaphore wait = 0);
};
// UBERHAR_FINISH
struct Teardown {
    struct Instance { vk::Device GetDevice() { return {}; } } instance;
    struct Present { unsigned drains{}; void WaitPresent() { ++drains; } } main_present_window;
    Scheduler scheduler;
    void Run() {
        // UBERHAR_TEARDOWN
    }
};
class GraphicsPipeline {
public:
    std::atomic_bool failed{}, device_lost{}, done{};
    int fault{};
    void MarkFailed() { failed.store(true); done.store(true); }
    std::uint64_t Key() const { return 7; }
    bool BuildImpl(bool probe) {
        if (fault == 1) throw vk::SystemError(-4);
        if (fault == 2) throw vk::SystemError(-1);
        if (fault == 3) throw std::runtime_error("source failure");
        if (fault == 4) throw 17;
        if (probe) return false;
        done.store(true);
        return true;
    }
    bool Build(bool probe = false);
};
// UBERHAR_PRODUCTION_BODY
int main() {
    for (bool log_fault : {false, true}) {
        throwing_log = log_fault;
        for (int fault = 0; fault <= 4; ++fault) {
            GraphicsPipeline owner;
            owner.fault = fault;
            assert(owner.Build() == (fault == 0));
            assert(owner.done.load());
            assert(owner.failed.load() == (fault != 0));
            assert(owner.device_lost.load() == (fault == 1));
        }
    }
    GraphicsPipeline probe;
    assert(!probe.Build(true));
    assert(!probe.done.load() && !probe.failed.load() && !probe.device_lost.load());
    assert(probe.Build() && probe.done.load());
    Teardown lost;
    lost.scheduler.ReportDeviceLost();
    assert(lost.scheduler.DeviceLost() && lost.scheduler.shader_failure.Failed());
    bool terminal{};
    try { lost.scheduler.shader_failure.Check(); }
    catch (const VideoCore::ShaderRecoveryError&) { terminal = true; }
    assert(terminal);
    lost.Run();
    assert(lost.scheduler.submissions == 0 && lost.scheduler.waits == 0);
    assert(lost.scheduler.drains == 1 && lost.main_present_window.drains == 1);
    assert(vk::idle_waits == 0);
    Teardown healthy;
    healthy.Run();
    assert(healthy.scheduler.submissions == 1 && healthy.scheduler.waits == 1);
    assert(vk::idle_waits == 1);
    std::puts("PASS: production pipeline failure categories, completion, logging failure and cache miss");
}
'''
    fixture = fixture.replace('// UBERHAR_SCHEDULER_METHODS', scheduler_methods)
    fixture = fixture.replace('// UBERHAR_FINISH', finish)
    fixture = fixture.replace('// UBERHAR_TEARDOWN', teardown)
    cases = [('normal', body)]
    # CodexAstraUlt-2: The negative control must compile and lose only the actual
    # device-loss classification; a compiler failure never counts as protection.
    anchor = 'error.code().value() == static_cast<int>(vk::Result::eErrorDeviceLost)'
    if body.count(anchor) != 1:
        raise ValueError('Production device-loss publication anchor changed')
    cases.append(('lost_category', body.replace(anchor, 'false')))
    for name, production in cases:
        source = out / f'{name}.cpp'
        executable = out / name
        source.write_text(fixture.replace('// UBERHAR_PRODUCTION_BODY', production))
        compile_result = subprocess.run(['c++', '-std=c++20', '-O2', '-pthread', f'-I{root / "src"}', str(source), '-o',
                                         str(executable)], capture_output=True, text=True, timeout=60)
        (out / f'{name}.compile.log').write_text(compile_result.stdout + compile_result.stderr)
        if compile_result.returncode:
            raise RuntimeError(f'{name}: compile failed; see {out}')
        run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
        (out / f'{name}.run.log').write_text(run.stdout + run.stderr)
        if name == 'normal':
            if run.returncode:
                raise RuntimeError(f'Production boundary failed: {run.stderr}')
            print(run.stdout.strip())
        elif run.returncode == 0 or 'owner.device_lost.load() == (fault == 1)' not in run.stderr:
            raise RuntimeError('Device-loss defect did not fail its intended assertion')
    print('PASS: compiled device-loss defect rejected for the intended reason')


if __name__ == '__main__':
    main()
