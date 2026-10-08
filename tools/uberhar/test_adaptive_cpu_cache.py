#!/usr/bin/env python3
"""CodexAstraLocal: Require bounded lifetime defects to fail the intended assertions."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys


def main():
    here=Path(__file__).resolve().parent
    root=Path(__file__).resolve().parents[2]
    sys.path.insert(0,str(root/'tools/uberhar'))
    from test_ready_cpu_fragments import replace_once
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root',type=Path,default=root)
    parser.add_argument('--source-root',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--mutants',action='store_true')
    args=parser.parse_args();root=args.repo_root.resolve();source_root=(args.source_root or root).resolve()
    if args.output:
        out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    else:
        import tempfile
        parent=root/'build/uberhar-probe/adaptive-cpu-cache';parent.mkdir(parents=True,exist_ok=True)
        out=Path(tempfile.mkdtemp(prefix='validation-',dir=parent))
    header_path=source_root/'src/video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h'
    header=header_path.read_text();fixture=Path(__file__).with_suffix('.cpp').read_text()
    source=(source_root/'src/video_core/renderer_vulkan/vk_pipeline_cache.cpp').read_text()
    anchor='if (is_dirty || bound_pipeline != selected) {'
    start=source.index(anchor);end=source.index('\n        }',start)+len('\n        }')
    bind=source[start:end]+'\n'
    mutations=[
     ('normal',None,None,None),
     ('collision-reuses-demand','header',
      ('!entry.key || entry.hash != hash || !(*entry.key == key)', '!entry.key || entry.hash != hash'),
      'distinct-key collision resets probation before failure ledger'),
     ('missing-post-draw-tick','header',
      ('slot.last_use_tick = std::max(slot.last_use_tick, tick);\n    }\n\n    // CodexAstraLocal: Exact-key',
       '(void)tick;\n    }\n\n    // CodexAstraLocal: Exact-key'),
      'post-map-flush draw extends use tick'),
     ('accepted-as-completed','header',('known_gpu_tick < slot.last_use_tick','false'),
      'uncompleted draw forbids destruction'),
     ('isdone-as-release','header',
      ('return slot.compiler_released.load(std::memory_order_acquire) == slot.generation;',
       'return !slot.owner || slot.owner->IsDone();'),
      'never retire still-referenced compiler owner'),
     ('stale-bound-pointer','fixture',
      ('if (bound_pipeline == owner) bound_pipeline = nullptr;', '(void)owner;'),
      'retirement invalidates raw bind cache'),
     ('early-capacity-release','header',
      ('slot.state = State::DestroyQueued;','slot.state = State::DestroyQueued; --owned;'),
      'queued deletion cannot free capacity'),
     ('repeat-deletion-allocation','header',
      ('slot.state = State::Retired;\n                disabled = true;',
       'slot.state = State::Retired;\n                disabled = false;'),
      'destruction queue failure disables adaptation'),
     ('retire-after-work-exhaustion','header',
      ('pressure && attempts < CpuAttempts && combined_attempts < CombinedLimit &&', 'pressure &&'),
      'exhausted combined work prevents new retirement'),
     ('extra-attempt','header',('CpuAttempts = 64;', 'CpuAttempts = 65;'),
      'CPU token exhaustion forbids new retirement'),
    ]
    includes=[root/'src']+[root/'externals'/p for p in ('fmt/include','boost','xxHash','nihstro/include','vulkan-headers/include')]
    flags=['c++','-std=c++20','-O2','-pthread','-DFMT_HEADER_ONLY','-DXXH_INLINE_ALL',*[f'-I{p}'for p in includes]]
    objects=[]
    for name in ('src/common/thread.cpp','src/common/error.cpp','src/video_core/shader/generator/pica_fs_config.cpp'):
        obj=out/(Path(name).stem+'.o')
        r=subprocess.run([*flags,'-c',str(root/name),'-o',str(obj)],capture_output=True,text=True,timeout=90)
        (out/(obj.stem+'.compile.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(f'dependency compile failed {name}')
        objects.append(obj)
    rows=[]
    if not args.mutants:mutations=mutations[:1]
    for name,kind,change,expected in mutations:
        case=out/name;case.mkdir(exist_ok=True)
        h=header;f=fixture
        if kind=='header':h=replace_once(h,*change)
        if kind=='fixture':f=replace_once(f,*change)
        patched=case/'video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h';patched.parent.mkdir(parents=True)
        patched.write_text(h)
        (case/'experiment.cpp').write_text(f)
        (case/'bind_identity.inc').write_text(bind)
        cmd=[flags[0],f'-I{case}',*flags[1:],str(case/'experiment.cpp'),*map(str,objects),'-o',str(case/'experiment')]
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=90)
        (case/'compile.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(f'mutant must compile: {name}')
        r=subprocess.run([str(case/'experiment')],capture_output=True,text=True,timeout=20)
        (case/'execute.log').write_text(r.stdout+r.stderr)
        good=(r.returncode==0) if expected is None else (r.returncode==1 and f'FAILED: {expected}' in r.stderr)
        rows.append({'name':name,'argv':cmd,'expected_assertion':expected,'returncode':r.returncode,'output':r.stdout+r.stderr,'pass':good,
            'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in case.iterdir()if p.is_file()}})
        (out/'report.json').write_text(json.dumps({'author':'CodexAstraLocal','scope':'Private policy/actual worker proof; mutations are controls, not device findings.',
            'baseline':{str(header_path):hashlib.sha256(header.encode()).hexdigest(),str(Path(__file__).with_suffix('.cpp')):hashlib.sha256(fixture.encode()).hexdigest()},'cases':rows},indent=2)+'\n')
        print(name,good,flush=True)
        if not good:raise RuntimeError(f'wrong outcome: {name}: {r.stdout}{r.stderr}')

if __name__=='__main__':
    main()
