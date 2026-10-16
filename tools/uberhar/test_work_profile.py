#!/usr/bin/env python3
"""CodexAstraLocal: Execute production GPU-profile methods with deterministic Vulkan endpoints.

This validates query ownership and failure behavior, not GPU timing accuracy,
driver behavior, scheduler integration or renderer performance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


# CodexAstraLocal: Only the Vulkan/settings/logging boundary is modeled. The
# complete production class and implementation are copied without body edits.
FAKE_COMMON = r'''
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
using u32=std::uint32_t; using u64=std::uint64_t;
namespace vk {
enum class Result { eSuccess, eNotReady, eErrorDeviceLost };
enum class QueryType { eTimestamp };
enum class QueryResultFlagBits : unsigned { e64=1, eWithAvailability=2, eWait=4 };
inline unsigned operator|(QueryResultFlagBits a, QueryResultFlagBits b) {
    return unsigned(a)|unsigned(b);
}
enum class PipelineStageFlagBits { eTopOfPipe, eBottomOfPipe };
struct QueryPoolCreateInfo { QueryType queryType{}; u32 queryCount{}; };
struct UniqueQueryPool {
    bool live{};
    explicit operator bool() const { return live; }
    u64 operator*() const { return 1; }
};
}
namespace Model {
struct Answer {
    vk::Result result{vk::Result::eNotReady};
    std::array<u64,4> words{};
    bool throws{};
};
struct Backend {
    std::array<Answer,128> answers{};
    u32 valid_bits{8}; double period{2.5}; bool supported{true};
    bool physical_throws{}, create_throws{}, render_pass{};
    unsigned creates{}, resets{}, timestamps{}, queries{}, violations{};
    std::vector<unsigned> read_slots;
    void Ready(unsigned slot,u64 start=100,u64 end=160) {
        answers.at(slot)={vk::Result::eSuccess,{start,1,end,1},false};
    }
};
inline bool log_throws{};
inline std::vector<int> deliveries;
}
namespace vk {
struct CommandBuffer {
    Model::Backend* backend{};
    void resetQueryPool(u64,u32 first,u32 count) const {
        ++backend->resets;
        if (backend->render_pass || count!=2 || (first&1) || first>=256) ++backend->violations;
        backend->answers.at(first/2)={};
    }
    void writeTimestamp(PipelineStageFlagBits,u64,u32 index) const {
        ++backend->timestamps;
        if(index>=256) ++backend->violations;
    }
};
}
'''

FAKE_SETTINGS = r'''
#pragma once
namespace Settings {
enum class UberharTestMode { Custom, Native, Automatic, ComboGeneric, Compute, Software };
struct Setting { UberharTestMode value{UberharTestMode::Native};
    UberharTestMode GetValue() const { return value; } };
// CodexAstraLocal: Boundary settings mirror production controls; tests explicitly
// request timestamps where query behavior is under examination.
struct BoolSetting { bool value{true}; bool GetValue() const { return value; } };
inline struct Values { Setting uberhar_test_mode; BoolSetting uberhar_feature_diagnostics;
    BoolSetting uberhar_gpu_timestamps; } values;
inline bool UsesReadyGpuVertices(UberharTestMode mode) {
    return mode==UberharTestMode::Automatic || mode==UberharTestMode::ComboGeneric;
}
}
'''

FAKE_LOG = r'''
#pragma once
#include "video_core/renderer_vulkan/vk_common.h"
namespace Common::Log {
enum class Delivery { Reliable, Diagnostic };
template<class... A> void Emit(Delivery delivery,const char*,const A&...) {
    if(Model::log_throws) throw std::runtime_error("modeled logging failure");
    Model::deliveries.push_back(delivery==Delivery::Reliable ? 1 : 2);
}
}
#define LOG_INFO(c,...) Common::Log::Emit(Common::Log::Delivery::Reliable,__VA_ARGS__)
#define LOG_WARNING(c,...) Common::Log::Emit(Common::Log::Delivery::Reliable,__VA_ARGS__)
#define LOG_INFO_DIAGNOSTIC(c,...) Common::Log::Emit(Common::Log::Delivery::Diagnostic,__VA_ARGS__)
#define LOG_INFO_WITH_DELIVERY(c,d,...) Common::Log::Emit(d,__VA_ARGS__)
'''

FAKE_INSTANCE = r'''
#pragma once
#include "video_core/renderer_vulkan/vk_common.h"
namespace Vulkan {
struct Physical {
    Model::Backend* b;
    struct Family { u32 timestampValidBits; };
    struct Properties { struct { bool timestampComputeAndGraphics; double timestampPeriod; } limits; };
    std::vector<Family> getQueueFamilyProperties() const {
        if(b->physical_throws) throw std::runtime_error("modeled physical query failure");
        return {{b->valid_bits}};
    }
    Properties getProperties() const { return {{b->supported,b->period}}; }
};
struct Device {
    Model::Backend* b;
    vk::UniqueQueryPool createQueryPoolUnique(vk::QueryPoolCreateInfo info) const {
        ++b->creates;
        if(info.queryType!=vk::QueryType::eTimestamp || info.queryCount!=256) ++b->violations;
        if(b->create_throws) throw std::runtime_error("modeled pool creation failure");
        return {true};
    }
    vk::Result getQueryPoolResults(u64,u32 first,u32 count,std::size_t size,
                                  void* data,std::size_t stride,unsigned flags) const {
        ++b->queries; b->read_slots.push_back(first/2);
        if(count!=2 || size!=4*sizeof(u64) || stride!=2*sizeof(u64) ||
           flags!=(unsigned(vk::QueryResultFlagBits::e64)|unsigned(vk::QueryResultFlagBits::eWithAvailability)))
            ++b->violations;
        auto answer=b->answers.at(first/2);
        if(answer.throws) throw std::runtime_error("modeled Vulkan query exception");
        auto* output=static_cast<u64*>(data);
        for(unsigned i=0;i<4;++i) output[i]=answer.words[i];
        return answer.result;
    }
};
class Instance {
public:
    explicit Instance(Model::Backend& b):backend{b} {}
    Physical GetPhysicalDevice() const { return {&backend}; }
    unsigned GetGraphicsQueueFamilyIndex() const { return 0; }
    Device GetDevice() const { return {&backend}; }
private:
    Model::Backend& backend;
};
}
'''

# CodexAstraLocal: Visibility changes only for inspecting bounded state. Method
# definitions, storage fields and all normal baseline expressions stay exact.
HARNESS = r'''
#include <algorithm>
#include <bit>
#include <iostream>
#include <thread>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "common/uberhar_test_profile.h"
#include "common/logging/log.h"
#define private public
#include "video_core/renderer_vulkan/vk_work_profile.h"
#undef private
#include "candidate.cpp"
using Vulkan::WorkProfile; using Vulkan::GpuWork; using Vulkan::HostWait;
unsigned checks{}, cases{};
std::string current;
void Check(bool value,const char* reason) {
    ++checks; if(!value) throw std::runtime_error(current+": "+reason);
}
template<class F> void Case(const char* name,F function) {
    current=name; ++cases;
    Settings::values.uberhar_test_mode.value=Settings::UberharTestMode::Native;
    Settings::values.uberhar_feature_diagnostics.value=true;
    Settings::values.uberhar_gpu_timestamps.value=true;
    Model::log_throws=false; Model::deliveries.clear(); function();
}
auto& Stats(WorkProfile& p,GpuWork kind) { return p.totals[static_cast<u32>(kind)]; }
int Next(WorkProfile& p,vk::CommandBuffer command,GpuWork kind) {
    for(unsigned i=0;i<64;++i) { const int slot=p.Begin(command,kind,16); if(slot>=0) return slot; }
    return -1;
}
int main() {
try {
    Case("disabled and unsupported",[] {
        Model::Backend b; Vulkan::Instance instance{b}; vk::CommandBuffer command{&b};
        Settings::values.uberhar_test_mode.value=Settings::UberharTestMode::Custom;
        { WorkProfile p{instance}; Check(!p.enabled && b.creates==0,"custom remains disabled");
          Check(p.Begin(command,GpuWork::Graphics)==-1,"disabled cannot record queries");
          p.Wait(HostWait::Recorder,50); Check(p.wait_calls[0]==0,"disabled waits stay inert"); }
        Settings::values.uberhar_test_mode.value=Settings::UberharTestMode::Native;
        b.valid_bits=0;
        { WorkProfile p{instance}; Check(p.enabled && !p.pool,"unsupported has no pool");
          Check(p.Begin(command,GpuWork::Copy,64)==-1 && Stats(p,GpuWork::Copy).calls==1,
                "unsupported counts work without inventing timing");
          Check(Stats(p,GpuWork::Copy).samples==0 && b.queries==0,"unsupported has no sample"); }
    });
    // CodexAstraLocal: Timestamp-off must retain truthful counts while issuing
    // no query commands; diagnostic-off is inert even if timestamps are requested.
    Case("independent diagnostic controls",[] {
        Model::Backend b; Vulkan::Instance instance{b}; vk::CommandBuffer command{&b};
        Settings::values.uberhar_gpu_timestamps.value=false;
        { WorkProfile p{instance}; Check(p.Enabled() && !p.pool,"counts remain enabled");
          p.BeginPass(command,GpuWork::Graphics); p.Route(0); p.EndPass(command);
          p.Submitted(1,1); p.Poll(1);
          Check(Stats(p,GpuWork::Graphics).calls==1 && p.routes[0]==1,"counts retained");
          Check(b.creates==0 && b.resets==0 && b.timestamps==0 && b.queries==0,"no GPU observer commands"); }
        Settings::values.uberhar_gpu_timestamps.value=true;
        Settings::values.uberhar_feature_diagnostics.value=false;
        { WorkProfile p{instance}; Check(!p.Enabled(),"diagnostic off is effective");
          p.BeginPass(command,GpuWork::Graphics); p.EndPass(command); p.Submitted(1,1);
          Check(Stats(p,GpuWork::Graphics).calls==0 && b.creates==0,"disabled collection inert"); }
    });
    Case("submission and original tick",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        // CodexAstraLocal: Route annotations belong to a real pass, not an
        // arbitrary utility bracket. Exercise the actual BeginPass API.
        p.BeginPass(command,GpuWork::Graphics); const int slot=p.pass_slot;
        Check(slot==0 && p.samples[slot].state==1,"recording owns first slot");
        p.Route(0); p.Route(2); p.Route(99); p.EndPass(command);
        Check(p.samples[slot].state==2,"end stays unsubmitted");
        b.Ready(slot); p.Poll(~u64{});
        Check(b.queries==0,"unsubmitted pair cannot be queried");
        p.Submitted(41,40);
        Check(b.queries==0 && p.samples[slot].tick==41,"precompletion cannot query");
        p.Submitted(99,40);
        Check(p.samples[slot].tick==41,"original successful tick cannot be replaced");
        p.Poll(41);
        auto& s=Stats(p,GpuWork::Graphics);
        Check(s.samples==1 && s.ns==150 && s.maximum_ns==150,"period conversion is exact");
        Check(s.first_tick==41 && s.last_tick==41 && s.first_serial==1,"sample identity retained");
        Check(s.mixed==1 && p.routes[0]==1 && p.routes[2]==1,"mixed routes remain explicit");
        Check(s.calls==1 && s.bytes==0 && p.samples[slot].state==0,"retired pass sample frees slot");
        Check(b.violations==0,"queries never request WAIT or malformed layout");
    });
    Case("unavailable then ready",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        const int slot=p.Begin(command,GpuWork::Copy); p.End(command,slot); p.Submitted(7,7);
        Check(p.unavailable==1 && p.samples[slot].state==3,"NotReady retains ownership");
        b.answers[slot]={vk::Result::eSuccess,{0,1,0,0},false}; p.Poll(7);
        Check(p.unavailable==2 && Stats(p,GpuWork::Copy).samples==0,"both availability words required");
        b.Ready(slot,0,0); p.Poll(7);
        Check(Stats(p,GpuWork::Copy).samples==1 && Stats(p,GpuWork::Copy).ns==0,
              "available zero duration is valid measured zero");
    });
    Case("query errors and exception containment",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        int slot=p.Begin(command,GpuWork::Copy); p.End(command,slot);
        b.answers[slot]={vk::Result::eErrorDeviceLost,{},false}; p.Submitted(8,8);
        Check(p.query_errors==1 && p.unavailable==0 && p.samples[slot].state==0,
              "VkResult error precedes availability");
        slot=Next(p,command,GpuWork::Copy); Check(slot>=0,"error slot can be reused after retirement");
        p.End(command,slot); b.answers[slot].throws=true; Model::log_throws=true;
        p.Submitted(9,9); Model::log_throws=false;
        Check(p.query_errors>=2 && p.samples[slot].state==0,"query and logging exceptions stay contained");
    });
    Case("ring exhaustion and completed reuse",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        for(unsigned n=0;n<128;++n) {
            const int slot=Next(p,command,GpuWork::Upload);
            Check(slot==static_cast<int>(n),"live slot cannot be reused"); p.End(command,slot);
        }
        Check(b.resets==128 && b.queries==0,"full unsubmitted ring remains owned");
        Check(Next(p,command,GpuWork::Upload)==-1 && Stats(p,GpuWork::Upload).dropped==1,
              "exhaustion drops a diagnostic sample");
        Check(b.resets==128 && b.queries==0,"exhaustion never resets or polls to make space");
        p.Submitted(50,49); Check(b.queries==0,"full ring still obeys completion");
        for(unsigned n=0;n<128;++n) b.Ready(n);
        p.Poll(50);
        Check(Stats(p,GpuWork::Upload).samples==128,"all completed samples retire");
        Check(Next(p,command,GpuWork::Upload)==0 && b.resets==129,"only retired slots are recycled");
        Check(b.violations==0,"bounded ring uses valid Vulkan query layout");
    });
    Case("valid bit wrap",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        const int slot=p.Begin(command,GpuWork::Blit); p.End(command,slot);
        b.Ready(slot,250,5); p.Submitted(5,5);
        Check(Stats(p,GpuWork::Blit).ns==27,"8-bit wrap masks before period conversion");
    });
    Case("sampled and unsampled pass guard",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        p.BeginPass(command,GpuWork::Graphics); b.render_pass=true;
        Check(p.pass_active && p.pass_slot==0,"sampled pass becomes active");
        const auto reset=b.resets;
        Check(p.Begin(command,GpuWork::Copy)==-1 && b.resets==reset,"sampled pass forbids inner query reset");
        b.render_pass=false; p.EndPass(command);
        Check(!p.pass_active && p.pass_slot==-1,"end clears pass ownership");
        p.BeginPass(command,GpuWork::Graphics); b.render_pass=true;
        Check(p.pass_active && p.pass_slot==-1,"unsampled pass is still active");
        Check(p.Begin(command,GpuWork::Upload)==-1 && b.resets==reset,
              "unsampled pass forbids inner query reset");
        b.render_pass=false; p.EndPass(command);
        Check(Next(p,command,GpuWork::Upload)>=0 && b.resets==reset+1,"outside-pass sampling resumes");
        Check(b.violations==0,"no query reset inside actual render pass");
    });
    Case("final retirement without later submission",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        const int slot=p.Begin(command,GpuWork::Presentation); p.End(command,slot); p.Submitted(200,199);
        b.Ready(slot); Check(Stats(p,GpuWork::Presentation).samples==0,"final pair initially pending");
        p.Poll(~u64{});
        Check(Stats(p,GpuWork::Presentation).samples==1 && p.samples[slot].state==0,
              "safe final poll retires last pair without submitting");
        p.Report(true); const auto count=Model::deliveries.size(); p.Report(true);
        Check(p.final_reported && Model::deliveries.size()==count,"final report is emitted once");
    });
    Case("distinct successful submissions",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance}; vk::CommandBuffer command{&b};
        int a=p.Begin(command,GpuWork::Copy); p.End(command,a); p.Submitted(10,9);
        int c=p.Begin(command,GpuWork::Blit); p.End(command,c); p.Submitted(11,9);
        b.Ready(a); b.Ready(c); p.Poll(10);
        Check(Stats(p,GpuWork::Copy).samples==1 && Stats(p,GpuWork::Blit).samples==0,
              "completion cannot retire later submission");
        Check(p.samples[c].tick==11 && p.samples[c].state==3,"later owner remains original");
        p.Poll(11); Check(Stats(p,GpuWork::Blit).first_tick==11,"later tick retires independently");
    });
    Case("host counters and bounded delivery",[] {
        Model::Backend b; Vulkan::Instance instance{b}; WorkProfile p{instance};
        auto add=[&]{ for(unsigned i=0;i<100;++i) p.Wait(HostWait::Recorder,7); };
        std::thread a{add},c{add}; a.join(); c.join();
        Check(p.wait_calls[0]==200 && p.waits[0]==1400,"cross-thread host totals are atomic");
        Model::deliveries.clear(); p.Report(); Check(Model::deliveries.empty(),"cadence suppresses immediate report");
        p.report_time-=std::chrono::seconds(11); p.report_prescale=63; p.Report();
        Check(Model::deliveries.size()==static_cast<unsigned>(GpuWork::Count)+static_cast<unsigned>(HostWait::Count)+1 && std::ranges::all_of(Model::deliveries,[](int x){return x==2;}),
              "periodic delivery is nonblocking diagnostic");
        Model::deliveries.clear(); p.Report(true);
        Check(Model::deliveries.size()==static_cast<unsigned>(GpuWork::Count)+static_cast<unsigned>(HostWait::Count)+1 && std::ranges::all_of(Model::deliveries,[](int x){return x==1;}),
              "final delivery is retained reliable");
    });
    Case("initialization and final log failures",[] {
        Model::Backend b; b.physical_throws=true; Vulkan::Instance instance{b};
        { WorkProfile p{instance}; Check(p.query_errors==1 && !p.pool,"physical query failure cannot fail startup"); }
        b.physical_throws=false; b.create_throws=true;
        { WorkProfile p{instance}; Check(p.query_errors==1 && !p.pool,"query pool failure cannot fail startup"); }
        b.create_throws=false;
        { WorkProfile p{instance}; Model::log_throws=true; p.Report(true);
          Check(p.query_errors>=1,"final log failure is contained"); Model::log_throws=false; }
    });
    std::cout<<"{\"passed\":true,\"cases\":"<<cases<<",\"checks\":"<<checks<<"}\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<"\n"; return 1;
}
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# CodexAstraLocal: Retain each attempt and exact source preimages. Failed
# compilation never counts as an expected behavioral negative control.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/work-profile'))
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output)).resolve()
    vk = out / 'include/video_core/renderer_vulkan'
    common = out / 'include/common'
    vk.mkdir(parents=True)
    (common / 'logging').mkdir(parents=True)
    runtime = root / 'src/video_core/renderer_vulkan'
    header, implementation = runtime / 'vk_work_profile.h', runtime / 'vk_work_profile.cpp'
    originals = {str(p.relative_to(root)): sha(p) for p in (header, implementation, Path(__file__))}
    (vk / 'vk_common.h').write_text(FAKE_COMMON)
    (vk / 'vk_instance.h').write_text(FAKE_INSTANCE)
    (common / 'uberhar_test_profile.h').write_text(FAKE_SETTINGS)
    (common / 'logging/log.h').write_text(FAKE_LOG)
    (vk / 'vk_work_profile.h').write_bytes(header.read_bytes())
    original = implementation.read_text()
    (out / 'production.cpp').write_text(original)
    (out / 'harness.cpp').write_text(HARNESS)
    report = {'scope': 'production WorkProfile methods, modeled Vulkan/settings/log endpoints',
              'output': str(out), 'inputs': originals, 'runs': [], 'passed': False}
    result_path = out / 'result.json'

    def persist():
        result_path.write_text(json.dumps(report, indent=2) + '\n')

    def execute(label, source, expected=None):
        (out / 'candidate.cpp').write_text(source)
        run = {'label': label, 'candidate_sha256': sha(out / 'candidate.cpp')}
        report['runs'].append(run)
        binary = out / label
        command = [args.cxx, '-std=c++20', '-O2', '-pthread', '-I'+str(out/'include'),
                   str(out/'harness.cpp'), '-o', str(binary)]
        start = time.monotonic()
        compile_result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (out / (label+'.compile.stdout')).write_text(compile_result.stdout)
        (out / (label+'.compile.stderr')).write_text(compile_result.stderr)
        run.update(compile_command=command, compile_exit=compile_result.returncode)
        persist()
        if compile_result.returncode:
            raise RuntimeError(f'{label} compilation failed; see {out}')
        completed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        (out / (label+'.stdout')).write_text(completed.stdout)
        (out / (label+'.stderr')).write_text(completed.stderr)
        run.update(exit_code=completed.returncode, elapsed_seconds=time.monotonic()-start,
                   executable_sha256=sha(binary))
        if expected is None:
            run['pass'] = completed.returncode == 0
            if run['pass']:
                run['behavior'] = json.loads(completed.stdout)
        else:
            run['expected_failure'] = expected
            run['pass'] = completed.returncode != 0 and expected in completed.stderr
        persist()
        if not run['pass']:
            raise RuntimeError(f'{label} behavioral expectation failed; see {out}')

    try:
        execute('baseline', original)
        # CodexAstraLocal: Three small executed counterexamples check that the
        # fixture detects premature reads, tick reassignment and active-pass loss.
        mutations = [
            ('ignore_completion', 'sample.state != 3 || sample.tick > completed_tick || !pool',
             'sample.state != 3 || !pool', 'precompletion cannot query'),
            ('replace_tick', 'if (sample.state == 2) { sample.tick = tick; sample.state = 3; }',
             'if (sample.state == 2 || sample.state == 3) { sample.tick = tick; sample.state = 3; }',
             'original successful tick cannot be replaced'),
            ('forget_unsampled_pass', 'pass_active = true;', 'pass_active = pass_slot >= 0;',
             'unsampled pass is still active'),
        ]
        for name, before, after, expected in mutations:
            if original.count(before) != 1:
                raise RuntimeError(f'nonunique production mutation anchor: {name}')
            execute(name, original.replace(before, after, 1), expected)
        report['unchanged_sources'] = all(sha(root/p) == digest for p, digest in originals.items())
        report['passed'] = report['unchanged_sources'] and all(r['pass'] for r in report['runs'])
        report['artifacts'] = {str(p.relative_to(out)): sha(p) for p in out.rglob('*')
                               if p.is_file() and p != result_path}
        persist()
        print(json.dumps({'passed': report['passed'], 'result': str(result_path)}), flush=True)
        if not report['passed']:
            raise RuntimeError('source changed during execution')
    except Exception as error:
        report['error'] = str(error)
        persist()
        print(json.dumps({'passed': False, 'result': str(result_path), 'error': str(error)}), flush=True)
        raise


if __name__ == '__main__':
    main()
