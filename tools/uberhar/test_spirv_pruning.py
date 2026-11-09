#!/usr/bin/env python3
"""CodexAstraLocal: Bounded actual-SPIR-V/TEV pruning and immutable policy gate.

Execute the original/pruned instruction emitters for 384 synthetic TEV states,
then validate 12 paired production modules. This does not execute a GPU or claim
pixel equivalence outside the recorded numerical cases.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from test_ready_cpu_fragments import function


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--dependency-root', type=Path)
    ap.add_argument('--sirit-library', type=Path, required=True)
    ap.add_argument('--spirv-val', type=Path)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    donor = (args.dependency_root or root).resolve()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    cpp = root / 'src/video_core/shader/generator/spv_fs_shader_gen.cpp'
    config = root / 'src/video_core/shader/generator/pica_fs_config.cpp'
    disk = root / 'src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp'
    fixture = Path(__file__).with_suffix('.cpp')
    methods = ['static bool IsPassThroughTevStage(', 'void FragmentModule::WriteTevStage(',
        'void FragmentModule::WritePrunedTevStage(', 'Id FragmentModule::GetSource(',
        'Id FragmentModule::AppendColorModifier(', 'Id FragmentModule::AppendAlphaModifier(',
        'Id FragmentModule::AppendColorCombiner(', 'Id FragmentModule::AppendAlphaCombiner(',
        'Id FragmentModule::Byteround(']
    (out / 'actual_tev_methods.inc').write_text('\n\n'.join(function(cpp.read_text(), x) for x in methods))
    (out / 'actual_profile_words.inc').write_text(function(disk.read_text(), 'std::vector<u32> OptionalProfileWords('))
    pins = {}
    for module in ['fmt', 'boost', 'xxHash', 'nihstro', 'sirit/sirit', 'spirv-headers']:
        expected = subprocess.check_output(['git', 'ls-tree', 'HEAD', 'externals/' + module], cwd=root, text=True).split()[2]
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=donor/'externals'/module, text=True).strip()
        if expected != actual: raise RuntimeError('Dependency commit mismatch: '+module)
        pins[module] = actual
    validator = str(args.spirv_val or shutil.which('spirv-val') or '')
    if not validator or not Path(validator).is_file(): raise RuntimeError('Explicit SPIR-V validator required')
    proof = {'author': 'CodexAstraLocal', 'status': 'running', 'dependency_pins': pins, 'commands': [],
        'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [
            cpp, cpp.with_suffix('.h'), config, disk, fixture, Path(__file__).resolve(),
            root/'src/video_core/pica/uberhar_tev_liveness.h', root/'src/video_core/shader/generator/profile.h',
            args.sirit_library, Path(validator)]}}

    def run(name, command, timeout=120):
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=timeout)
        (out/(name+'.log')).write_bytes(result.stdout+result.stderr)
        proof['commands'].append({'name':name,'argv':list(map(str,command)), 'exit':result.returncode,
                                  'seconds':round(time.monotonic()-start,3)})
        result.check_returncode()

    try:
        includes = [root/'src', out, donor/'externals/fmt/include', donor/'externals/boost',
                    donor/'externals/xxHash', donor/'externals/nihstro/include',
                    donor/'externals/sirit/sirit/include', donor/'externals/spirv-headers/include']
        run('compile', ['c++','-std=c++20','-O1','-DFMT_HEADER_ONLY','-DXXH_INLINE_ALL',
            *['-I'+str(p) for p in includes], str(fixture), str(cpp), str(config),
            str(args.sirit_library), '-o', str(out/'fixture')])
        run('emit-and-semantics',[str(out/'fixture'),str(out/'modules')],30)
        modules = sorted((out/'modules').glob('*.spv'))
        if len(modules)!=24: raise RuntimeError('Missing paired module')
        for module in modules:
            run('validate-'+module.stem,[validator,'--target-env','vulkan1.1',str(module)],15)
        run('queued-policy',['python3','tools/uberhar/test_shader_compile_policy.py',
            '--output',str(out/'queued-policy')],120)
        proof['status']='passed'
        proof['modules_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in modules}
    except Exception as error:
        proof['status']='failed';proof['error']=str(error);raise
    finally:
        (out/'RESULT.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS actual SPIR-V pruning, TEV semantics and queued policy:',out)


if __name__=='__main__':main()
