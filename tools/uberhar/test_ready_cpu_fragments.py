#!/usr/bin/env python3
"""CodexAstraLocal: Execute CPU-ready fragment routing and queued binding.

Production selection, key, reset and queued command bodies are extracted without
rewriting their decisions. GPU compilation, task scheduling and command emission
use explicit recording endpoints; this test cannot establish driver/image parity.
Real worker completion and CPU-output/shader execution have complementary gates.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess


# CodexAstraLocal: Balanced extraction rejects missing/ambiguous source rather than
# silently compiling a second implementation of selection or a neighboring job.
def function(source: str, signature: str) -> str:
    if source.count(signature) != 1:
        raise ValueError(f"Expected exactly one function: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth, state, index = 0, "code", opening
    while index < len(source):
        char, pair = source[index], source[index:index + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if pair == "*/":
                state = "code"
                index += 1
        elif state in ('"', "'"):
            if char == "\\":
                index += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            index += 1
        elif pair == "/*":
            state = "block"
            index += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
        index += 1
    raise ValueError(f"Unterminated function: {signature}")


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"Mutation/extraction anchor changed: {old}")
    return source.replace(old, new)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# CodexAstraLocal: These endpoints deliberately expose a delayed queue and one
# frontend mode. No production worker or settings implementation is replaced.
WORKER = r'''#pragma once
#include <deque>
#include <functional>
#include <stdexcept>
#include <string_view>
namespace Common {
class ThreadWorker {
public:
    std::deque<std::function<void()>> tasks;
    bool throw_queue{};
    unsigned scheduled{}, executed{}, drains{};
    ThreadWorker(unsigned = 1, std::string_view = {}) {}
    template <class F> void QueueWork(F&& task) {
        if (throw_queue) { throw_queue = false; throw std::bad_alloc{}; }
        tasks.emplace_back(std::forward<F>(task));
        ++scheduled;
    }
    void WaitForRequests() {
        ++drains;
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
            ++executed;
        }
    }
};
}
'''
SETTINGS = r'''#pragma once
namespace Settings {
// CodexAstraLocal: Match appended production Software ID without changing old IDs.
enum class UberharTestMode {Custom, Native, Compute, Automatic, ComboGeneric, Software};
struct Mode {
    UberharTestMode value{UberharTestMode::Automatic};
    auto GetValue() const { return value; }
};
inline struct Values { Mode uberhar_test_mode; } values;
}
'''



def main() -> None:
    import tempfile
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--source-root',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--mutants',action='store_true')
    parser.add_argument('--static-tev',action='store_true',help='Execute both bounded CPU fragment tiers')
    args=parser.parse_args()
    root=args.repo_root.resolve();source_root=(args.source_root or root).resolve()
    if args.output:
        out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    else:
        parent=root/'build/uberhar-probe/ready-cpu-fragments';parent.mkdir(parents=True,exist_ok=True)
        out=Path(tempfile.mkdtemp(prefix='validation-',dir=parent))
    inputs={}
    def read(name):
        p=source_root/name
        if not p.exists():p=root/name
        inputs[str(p)]=digest(p)
        return p.read_text()
    cpp=read('src/video_core/renderer_vulkan/vk_pipeline_cache.cpp')
    graphics=read('src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp')
    parts=[function(graphics,'u64 GraphicsPipeline::Key()').replace('GraphicsPipeline::Key()','GraphicsPipeline::ActualKey()'),
           function(graphics,'bool GraphicsPipeline::MatchesExecution(')]
    for signature in ('bool PipelineCache::PreferReadySpecializedFragment(',
                      'bool PipelineCache::ReadyVertexShaders()',
                      'GraphicsPipeline* PipelineCache::PrepareReadyCpuFragment(',
                      'void PipelineCache::CompleteReadyCpuDraw(',
                      'void PipelineCache::RetireReadyCpuAfterWorkerDrain()',
                      'GraphicsPipeline* PipelineCache::PrepareReadyGpuVertex(',
                      'void PipelineCache::UseFragmentShader(',
                      'bool PipelineCache::BindPipeline(',
                      'void PipelineCache::ClearTevFallbacks()'):
        parts.append(function(cpp,signature))
    parts.append(function(read('src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp'),
        'std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseReadyFragmentShader('))
    parts.append(function(read('src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp'),
        'std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseStaticTevFragmentShader('))
    parts.append(function(cpp,'bool PipelineCache::ReadyGpuFragmentPreflight('))
    disk_header=read('src/video_core/renderer_vulkan/vk_shader_disk_cache.h')
    parts.append(function(disk_header,'bool OptionalFragmentPending() const').replace(
        'bool OptionalFragmentPending()', 'bool ShaderDiskCache::OptionalFragmentPending()'))
    old_members=disk_header[disk_header.index('    struct ReadyFragmentKey {'):
        disk_header.index('    std::unordered_map<size_t, Shader> fixed_geometry_shaders;')]
    new_members=disk_header[disk_header.index('    struct StaticTevEntry {'):
        disk_header.index('    std::unordered_set<u64> known_geometry_shaders;')]
    (out/'fragment_members.inc').write_text(old_members+new_members)
    body=replace_once('\n\n'.join(parts),'vk::CommandBuffer cmdbuf','RecordingCommands cmdbuf')
    key=read('src/video_core/renderer_vulkan/uberhar_cpu_fragment_cache.h')
    if key.count('namespace Vulkan {')!=1:raise ValueError('observation-key namespace changed')
    (out/'cpu_fragment_key.inc').write_text(key.split('namespace Vulkan {',1)[1].rsplit('} // namespace Vulkan',1)[0])
    read('src/video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h')
    ras=read('src/video_core/renderer_vulkan/vk_rasterizer.cpp')
    layout=function(ras,'void RasterizerVulkan::MakeSoftwareVertexLayout()')
    (out/'layout.inc').write_text(layout[layout.index('{')+1:-1])
    stamp='if (cpu_fragment_use) pipeline_cache.CompleteReadyCpuDraw(cpu_fragment_use);'
    if ras.count(stamp)!=1 or ras.index(stamp)<ras.index('cmdbuf.draw(vertex_count, 1, 0, 0);'):
        raise ValueError('post-draw ownership stamp missing or reordered')
    tick=function(ras,'void RasterizerVulkan::TickFrame()')
    if tick.index('scheduler.WaitWorker();')>tick.index('pipeline_cache.RetireReadyCpuAfterWorkerDrain();'):
        raise ValueError('retirement moved before existing command drain')
    (out/'after_draw.inc').write_text(stamp+'\n')
    stubs=out/'stubs/common';stubs.mkdir(parents=True)
    (stubs/'thread_worker.h').write_text(WORKER);(stubs/'settings.h').write_text(SETTINGS)
    # CodexAstraLocal: Share driver/command endpoints while keeping the legacy
    # full-route population and the new partial-policy population explicit.
    fixture=Path(__file__).with_name('test_static_tev_cache.cpp') if args.static_tev else Path(__file__).with_suffix('.cpp')
    common=Path(__file__).with_name('static_tev_cache_fixture.h')
    inputs[str(common)]=digest(common)
    read('src/video_core/renderer_vulkan/uberhar_static_tev_policy.h')
    inputs[str(fixture)]=digest(fixture);inputs[str(Path(__file__))]=digest(Path(__file__))
    includes=[out/'stubs',out,source_root/'src',root/'src']+[root/'externals'/p for p in (
        'fmt/include','boost','xxHash','nihstro/include','vulkan-headers/include')]
    flags=['c++','-std=c++20','-O2','-pthread','-DFMT_HEADER_ONLY','-DXXH_INLINE_ALL',*[f'-I{p}'for p in includes]]
    objects=[];commands=[]
    for name in ('pica_fs_config','glsl_fs_shader_gen'):
        source=source_root/f'src/video_core/shader/generator/{name}.cpp'
        if not source.exists():source=root/f'src/video_core/shader/generator/{name}.cpp'
        inputs[str(source)]=digest(source)
        obj=out/(name+'.o');cmd=[*flags,'-c',str(source),'-o',str(obj)]
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
        (out/(name+'.compile.log')).write_text(r.stdout+r.stderr);commands.append(cmd)
        if r.returncode:raise RuntimeError(f'generator compile failed: {name}')
        objects.append(obj)
    cases=[('normal',body,None)]
    if args.mutants and not args.static_tev:
        for name,old,new,expected in [
            ('wrong-selected-owner','pipeline = ready;','pipeline = generic;','actual G/S/G pipeline identity'),
            ('hash-only','ready->Key() != candidate.key || !ready->MatchesExecution(candidate.pipeline, owners)',
             'ready->Key() != candidate.key','hash collision cannot replace actual execution equality'),
            ('missing-postdraw-use','ready_cpu_bank->DrawQueued(token, scheduler.CurrentTick());',
             '(void)token;','actual after-Map draw tick retained'),
            ('stale-warming-pointer','if (warming_ready_vertex == owner) warming_ready_vertex = nullptr;',
             '(void)owner;','retirement clears shared warming raw pointer'),
            ('missing-cpu-cap','!ReadyVertexPolicy::CanQueue(ready_vertex_pipelines.size() + ReadyCpuOwned(), pending)',
             '!ReadyVertexPolicy::CanQueue(ready_vertex_pipelines.size(), pending)','combined physical cap includes CPU owner'),
            ('uncharged-gpu-work','++ready_optional_attempts;','(void)ready_optional_attempts;',
             'GPU attempt charged before worker admission'),
            ('stale-generic-constants','tev_push_constants.Invalidate();','(void)constants_dirty;',
             'specialized dirty state invalidates same-A generic constants'),
        ]:cases.append((name,replace_once(body,old,new),expected))
    header_mutations={}
    if args.mutants and args.static_tev:
        for name,old,new,expected in [
            ('no-full-promotion','if (partial.ready) {\n            if (allow_full) advance(full);',
             'if (partial.ready) {\n            if (false) advance(full);','CPU demand still promotes full while partial ready'),
            ('partial-priority','if (full.ready) return select(full);','if (false) return select(full);','exact ready full wins priority'),
            ('pending-full-blocks','return select(partial);',
             'if (!curr_disk_cache->OptionalFragmentPending() && !(warming_ready_vertex && !warming_ready_vertex->IsDone())) return select(partial);\n            return nullptr;',
             'partial remains complete during full advancement'),
            ('missing-partial-push','selected_fallback || static_tev_cpu','selected_fallback','partial retains exact per-draw runtime transport'),
            ('native-worker-gpu','!allow_ready_gpu_vertices || !ready_vertex_worker || !ReadyVertexShaders()',
             '!ready_vertex_worker || !ReadyVertexShaders()','Native CPU compiler does not admit GPU vertices'),
            ('partial-key-alias','!(entry.key.plan == plan) ||','false ||','exact partial key rejects same-hash aliases'),
            ('shared-attempt-cap','if (full.ready) return select(full);',
             'if (ready_cpu_bank->Attempts(0) < 64 && full.ready) return select(full);','full ready hit survives 64 full attempts')]:
            cases.append((name,replace_once(body,old,new),expected))
        for name,old,new,expected in [
            ('total-only-pressure','PartitionPressure();','false;','full partition pressure permits ninth full replacement'),
            ('shared-probation','Partitions::Index(key) * Observations + hash % Observations','hash % Observations',
             'stable cross-tier bucket collision preserves both probation histories'),
            ('cross-tier-victim','!ReplacementDemand(slot, other_owned)','false','full pressure never retires partial owner')]:
            header_mutations[name]=(old,new)
            cases.append((name,body,expected))
    results=[]
    for name,functions,expected in cases:
        case=out/name;case.mkdir();inc=case/'functions.inc';inc.write_text(functions)
        adaptive=read('src/video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h')
        if name in header_mutations:adaptive=replace_once(adaptive,*header_mutations[name])
        h=case/'video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h';h.parent.mkdir(parents=True)
        h.write_text(adaptive)
        cmd=[flags[0],f'-I{case}',*flags[1:],f'-DUBERHAR_CPU_FRAGMENT_FUNCTIONS="{inc}"',str(fixture),*map(str,objects),'-o',str(case/'test')]
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
        (case/'compile.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(f'compile failed: {case}')
        r=subprocess.run([str(case/'test')],capture_output=True,text=True,timeout=30)
        (case/'execute.log').write_text(r.stdout+r.stderr)
        good=(r.returncode==0)if expected is None else(r.returncode==1 and f'FAIL {expected}'in r.stderr)
        results.append({'name':name,'argv':cmd,'expected':expected,'returncode':r.returncode,'output':r.stdout+r.stderr,'pass':good,
            'artifacts':{p.name:digest(p)for p in case.iterdir()if p.is_file()}})
        (out/'provenance.json').write_text(json.dumps({'author':'CodexAstraLocal','scope':__doc__,
            'inputs':inputs,'generator_commands':commands,'cases':results,
            'artifacts':{p.name:digest(p)for p in out.iterdir()if p.is_file()and p.name!='provenance.json'}},indent=2)+'\n')
        print(name,good,r.stdout.strip(),flush=True)
        if not good:raise RuntimeError(f'wrong outcome: {name}: {r.stdout}{r.stderr}')
    print(f'Evidence: {out}')


if __name__=='__main__':
    main()
