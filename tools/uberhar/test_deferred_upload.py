#!/usr/bin/env python3
"""CodexAstraLocal: Compile actual changed ring/lease methods with controlled device and tick seams."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root', type=Path, default=ROOT/'src')
parser.add_argument('--output', type=Path, default=ROOT/'build/uberhar-probe/deferred-upload')
args=parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
HERE=Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
# CodexAstraLocal: Copy the exact public header so every defect runs against the
# same declaration; all generated methods and compiler outputs stay in this run.
relative=Path('video_core/renderer_vulkan/vk_stream_buffer')
source_path=args.source_root/relative.with_suffix('.cpp')
header_path=args.source_root/relative.with_suffix('.h')
header=HERE/'source'/relative.with_suffix('.h')
header.parent.mkdir(parents=True)
shutil.copyfile(header_path, header)
source=source_path.read_text()
# CodexAstraLocal: Extract complete brace-balanced production definitions. The
# fake device observes lifetime and visibility calls, not a second ring algorithm.
def definition(start):
    i=source.index(start); begin=source.index('{',i); depth=1;j=begin+1
    while depth:
        depth+=(source[j]=='{')-(source[j]=='}');j+=1
    return source[i:j]+(';' if start.startswith('class ') else '')
methods=['class StreamBuffer::Allocation final',
'bool StreamBuffer::DeferredUpload::Publish(',
'vk::Buffer StreamBuffer::DeferredUpload::Handle()',
'void StreamBuffer::DestroyBuffers()',
'std::tuple<u8*, u32, bool> StreamBuffer::Map(',
'void StreamBuffer::Commit(',
'bool StreamBuffer::CanDeferUpload()',
'std::optional<StreamBuffer::DeferredMapping> StreamBuffer::MapDeferredUpload(',
'std::optional<StreamBuffer::DeferredUpload> StreamBuffer::CommitDeferredUpload(',
'void StreamBuffer::ReserveWatches(',
'void StreamBuffer::WaitPendingOperations(']
stub=HERE/'stub/video_core/renderer_vulkan';stub.mkdir(parents=True,exist_ok=True)
(stub/'vk_common.h').write_text(r'''// CodexAstraLocal: Fake Vulkan calls count lifetime/cache operations; no device access.
#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
using u8=uint8_t;using u32=uint32_t;using u64=uint64_t;
#define VK_NULL_HANDLE 0
namespace vk {
using Buffer=uint64_t;using DeviceMemory=uint64_t;using BufferUsageFlags=uint32_t;
struct MappedMemoryRange {DeviceMemory memory;u64 offset;u64 size;};
struct Device {static inline int unmaps{},destroys{},frees{},flushes{},invalidates{};
void unmapMemory(DeviceMemory) const{++unmaps;}
void destroyBuffer(Buffer) const{++destroys;}
void freeMemory(DeviceMemory) const{++frees;}
void flushMappedMemoryRanges(MappedMemoryRange) const{++flushes;}
void invalidateMappedMemoryRanges(MappedMemoryRange) const{++invalidates;}};
}
''')
preamble=r'''// CodexAstraLocal: Controlled tick and allocation seams execute actual StreamBuffer methods.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#define private public
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#undef private
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x,...) assert(x)
namespace Common {template<class T,class U> T AlignUp(T n,U a){return T((n+a-1)/a*a);}}
namespace Vulkan {
constexpr u64 WATCHES_RESERVE_CHUNK=2;
class Instance {public: mutable u64 freed{};vk::Device GetDevice()const{return{};}
u64 NonCoherentAtomSize()const{return 16;}void RecordRawStreamFree(u64 n)const{freed+=n;}};
class Scheduler {public:u64 tick=1;std::vector<u64> waits;std::function<void(u64)> on_wait;
u64 CurrentTick()const{return tick;}void Wait(u64 t){waits.push_back(t);if(on_wait)on_wait(t);}};
'''
# CodexAstraLocal: Backing storage belongs to this fixture. Actual shared
# Allocation destruction must still issue exactly one unmap/destroy/free.
construct=r'''
StreamBuffer::StreamBuffer(const Instance& i,Scheduler& s,vk::BufferUsageFlags u,u64 n,BufferType t)
:instance{i},scheduler{s},device{i.GetDevice()},buffer{1},memory{2},
stream_buffer_size{n},allocation_bytes{n},usage{u},type{t},is_coherent{true}{
mapped=new u8[n]{};allocation=std::make_shared<Allocation>(i,device,buffer,memory,mapped,n);
ReserveWatches(current_watches,8);ReserveWatches(previous_watches,8);}
StreamBuffer::~StreamBuffer(){DestroyBuffers();}
}
int main(){using namespace Vulkan;
// CodexAstraLocal: Exact numbered semantic failures distinguish each defect
// from a crash, timeout, compiler error or earlier unrelated failure.
int checks=0;auto check=[&](bool b){if(!b){std::cerr<<"check "<<checks+1<<" failed\n";std::exit(23);}++checks;};
std::array<u8,16> input;input.fill(0x5a);
Instance i;Scheduler s;auto b=std::make_unique<StreamBuffer>(i,s,0,32,BufferType::Stream);
auto* raw=b->mapped;check(b->CanDeferUpload());
auto stale=b->MapDeferredUpload(16,1);check(bool(stale));
b->Map(16,1); // Same offset, different generation must refuse.
check(!b->CommitDeferredUpload(*stale));
auto reservation=b->MapDeferredUpload(16,1);s.tick=42;
auto upload=b->CommitDeferredUpload(*reservation);check(bool(upload));
check(b->current_watches[0].tick==42);check(b->offset==16);
check(!b->CommitDeferredUpload(*reservation));
check(!upload->Publish(std::span<const u8>(input).first(15)));
check(upload->Publish(input));check(!upload->Publish(input));
auto moved=std::move(*upload);check(!upload->Publish(input));check(upload->Handle()==0);
check(!moved.Publish(input));check(std::equal(input.begin(),input.end(),raw));
check(vk::Device::flushes==0&&vk::Device::invalidates==0);
// CodexAstraLocal: Reservation crossing a final bind submit uses the new tick.
s.tick=43;auto second=b->MapDeferredUpload(16,1);s.tick=44;
auto second_upload=b->CommitDeferredUpload(*second);check(bool(second_upload));
check(b->current_watches[1].tick==44);
// CodexAstraLocal: A real host thread waits a held publication at ring wrap.
std::mutex mutex;std::condition_variable cv;bool entered=false,ready=false;
s.on_wait=[&](u64 t){check(t==42);std::unique_lock lock(mutex);entered=true;cv.notify_all();
cv.wait(lock,[&]{return ready;});};
std::atomic<bool> mapped_again=false;
std::thread owner([&]{b->Map(16,1);mapped_again=true;});
{std::unique_lock lock(mutex);cv.wait(lock,[&]{return entered;});}
check(!mapped_again.load());
{std::lock_guard lock(mutex);ready=true;}cv.notify_all();owner.join();check(mapped_again.load());
check(s.waits==std::vector<u64>{42});
// CodexAstraLocal: Unsupported policies must refuse without cursor changes.
b->is_coherent=false;const auto offset=b->offset;
check(!b->MapDeferredUpload(8,1));check(b->offset==offset);b->is_coherent=true;
b->type=BufferType::Download;check(!b->MapDeferredUpload(8,1));b->type=BufferType::Stream;
check(!b->MapDeferredUpload(0,1));check(!b->MapDeferredUpload(33,1));
b->mapping_generation=std::numeric_limits<u64>::max()-1;
check(!b->MapDeferredUpload(8,1));check(!b->CanDeferUpload());
// CodexAstraLocal: Canceling captures releases leases without publishing. CPU
// tasks own their separate output; there is no asynchronous write to this mapping.
b.reset();check(i.freed==0);second_upload.reset();check(i.freed==0);
{auto last=std::move(moved);check(last.Handle()==1);}check(i.freed==32);
check(vk::Device::unmaps==1&&vk::Device::destroys==1&&vk::Device::frees==1);
delete[] raw;
std::cout<<"checks="<<checks<<" PASS\n";
}
'''
fixture=preamble+'\n'.join(map(definition,methods))+construct
(HERE/'fixture.cpp').write_text(fixture)
# CodexAstraLocal: Require the candidate and deliberately incorrect generations,
# retirement ticks and duplicate publication to produce distinct checked outcomes.
def run_case(label, text, positive, failure_check=None):
    case=HERE/label; case.mkdir()
    unit=case/'fixture.cpp'; unit.write_text(text)
    command=[os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-Wall', '-Wextra',
             '-pthread', '-I'+str(HERE/'stub'), '-I'+str(HERE/'source'),
             str(unit), '-o', str(case/'fixture')]
    compiled=subprocess.run(command, capture_output=True, timeout=90)
    (case/'compile.stdout').write_bytes(compiled.stdout)
    (case/'compile.stderr').write_bytes(compiled.stderr)
    compiled.check_returncode()
    result=subprocess.run([str(case/'fixture')], capture_output=True, timeout=15)
    (case/'run.stdout').write_bytes(result.stdout)
    (case/'run.stderr').write_bytes(result.stderr)
    if positive:
        if result.returncode or result.stdout != b'checks=33 PASS\n':
            raise RuntimeError('Candidate did not complete all 33 checks')
    elif result.returncode != 23 or result.stderr != f'check {failure_check} failed\n'.encode():
        raise RuntimeError('Defect failed for an unexpected reason: '+label)
    return {'label':label, 'argv':command, 'returncode':result.returncode,
            'source_sha256':hashlib.sha256(text.encode()).hexdigest()}

def changed(old, new):
    if fixture.count(old) != 1:
        raise RuntimeError('Production defect seam changed: '+old)
    return fixture.replace(old, new)

results=[run_case('candidate', fixture, True)]
for label, old, new, failure_check in [
    ('stale-generation', 'reservation.generation != mapping_generation || ', '', 3),
    ('wrong-tick', 'watch.tick = scheduler.CurrentTick();', 'watch.tick = 1;', 5),
    ('duplicate-publication', '!owner || published || source.size() != size',
     '!owner || source.size() != size', 10)]:
    results.append(run_case(label, changed(old, new), False, failure_check))
# CodexAstraLocal: Preserve exact source/header identities with the finite control
# results. Fake Vulkan calls establish CPU ownership/order, not GPU execution.
(HERE/'provenance.json').write_text(json.dumps({
    'author':'CodexAstraLocal', 'methods':methods,
    'inputs':{str(path):hashlib.sha256(path.read_bytes()).hexdigest()
              for path in (source_path, header_path, Path(__file__))},
    'results':results,
    'scope':'actual ring/lease methods, controlled device/tick seams; no GPU/title or timing claim'
},indent=2)+'\n')
print('Deferred upload: 33 checks and 3 detected defects PASS; '+str(HERE))
