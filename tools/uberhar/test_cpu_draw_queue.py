#!/usr/bin/env python3
"""CodexAstraLocal: Real generated shader/owned CPU packet regression on host or Android API29/QEMU.

The fixture uses synthetic programs only. Actual input/FIFO/output/assembler,
accelerated HardwareVertex/writer bodies and JitEngine execute unchanged.
Diagnostic services and deterministic exception boundaries are explicitly adapted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import subprocess
import tempfile
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def change(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Source seam changed: ' + old[:100])
    return text.replace(old, new, 1)


def body(text, start):
    offset = text.index(start)
    begin = text.index('{', offset)
    depth = 1
    end = begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[offset:end]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--source-root', type=Path, help='Optional reviewed source overlay; otherwise root/src.')
    parser.add_argument('--output-root', type=Path)
    parser.add_argument('--a64', action='store_true')
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    source_root = (args.source_root or root / 'src').resolve()
    output_root = args.output_root or root / 'build/uberhar-probe/cpu-draw-queue'
    output_root.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=output_root)).resolve()
    source = out / 'source'
    source.mkdir()
    fixture_source = Path(__file__).with_suffix('.cpp').resolve()
    logical = ['video_core/pica/uberhar_cpu_draw_queue.h', 'video_core/pica/uberhar_cpu_draw_queue.cpp',
               'video_core/pica/uberhar_parallel_vertex.h']
    inputs = [Path(__file__).resolve(), fixture_source,
              root / 'tools/uberhar/test_parallel_vertex_observable_cases.h',
              root / 'src/video_core/rasterizer_accelerated.cpp']
    for relative in logical:
        original = source_root / relative
        target = source / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(original, target); inputs.append(original)
    shutil.copy(fixture_source, source / 'fixture.cpp')
    shutil.copy(root / 'tools/uberhar/test_parallel_vertex_observable_cases.h', source)
    # CodexAstraLocal: Extract exact real writer/converter bodies. Only fixture
    # type visibility and the receiver name differ; no pixel/vertex oracle model.
    renderer = (root / 'src/video_core/rasterizer_accelerated.cpp').read_text()
    constructor = body(renderer, 'RasterizerAccelerated::HardwareVertex::HardwareVertex(')
    opposite = body(renderer, 'static bool AreQuaternionsOpposite(')
    append = body(renderer, 'void RasterizerAccelerated::AddTriangle(').replace(
        'void RasterizerAccelerated::AddTriangle(', 'void TransportSink::AddTriangle(', 1)
    writer = body(renderer, 'void RasterizerAccelerated::WriteDeferredTriangles(').replace(
        'void RasterizerAccelerated::WriteDeferredTriangles(', 'void ActualWriter(', 1)
    (source / 'actual_writer.h').write_text('''// CodexAstraLocal: Exact current production bodies, synthetic data only.
#pragma once
#include "video_core/rasterizer_accelerated.h"
namespace VideoCore {
using Pica::f24;
''' + constructor + '\n' + opposite + '''
struct ExposeHardware : RasterizerAccelerated { using RasterizerAccelerated::HardwareVertex; };
using HardwareVertex = ExposeHardware::HardwareVertex;
struct TransportSink {
    std::vector<HardwareVertex> vertex_batch;
    void AddTriangle(const Pica::OutputVertex&, const Pica::OutputVertex&, const Pica::OutputVertex&);
};
''' + append + '\n' + writer + '\n}\n')
    stubs = out / 'stubs/common'; stubs.mkdir(parents=True)
    (stubs / 'settings.h').write_text('''// CodexAstraLocal: Standalone shader diagnostic policy only.
#pragma once
namespace Settings {
enum class UberharTestMode { Custom };
struct Setting { UberharTestMode GetValue() const { return UberharTestMode::Custom; } };
inline struct { Setting uberhar_test_mode; } values;
}
''')
    compiler = args.compiler
    if compiler is None and args.a64:
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        ndk_root = Path(ndk) if ndk else root / 'build/native-deps/android-ndk-r27d'
        compiler = ndk_root / 'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++'
    compiler = compiler or Path(shutil.which('c++'))
    if not compiler.is_file():
        raise RuntimeError('Compiler unavailable')
    prefix = []
    if args.a64:
        qemu = args.qemu or shutil.which('qemu-aarch64-static') or shutil.which('qemu-aarch64')
        if not qemu:
            raise RuntimeError('QEMU A64 unavailable')
        prefix = [str(qemu)]
    actual = [root / 'src' / relative for relative in (
        'video_core/pica/shader_unit.cpp', 'video_core/pica/shader_setup.cpp',
        'video_core/pica/primitive_assembly.cpp', 'video_core/shader/shader_jit.cpp',
        'video_core/shader/shader_jit_a64_compiler.cpp' if args.a64 else
        'video_core/shader/shader_jit_x64_compiler.cpp')]
    inputs += actual
    proof = {'author': 'CodexAstraLocal', 'passed': False,
        'target': 'Android API29 A64/QEMU' if args.a64 else 'host x64',
        'scope': __doc__, 'source_sha256': {str(p): sha(p) for p in inputs},
        'tool_sha256': {'compiler': sha(compiler)}, 'commands': [], 'dependencies': {}, 'variants': []}
    if prefix: proof['tool_sha256']['qemu'] = sha(prefix[0])
    def save():
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
    def run(name, argv, expected=0, timeout=180):
        argv = list(map(str, argv)); begin = time.monotonic()
        result = subprocess.run(argv, cwd=root, capture_output=True, timeout=timeout, preexec_fn=no_core)
        (out / (name + '.stdout')).write_bytes(result.stdout)
        (out / (name + '.stderr')).write_bytes(result.stderr)
        proof['commands'].append({'name': name, 'argv': argv, 'exit': result.returncode,
                                  'seconds': time.monotonic()-begin}); save()
        if result.returncode != expected:
            print(result.stdout.decode(errors='replace')[-3000:])
            print(result.stderr.decode(errors='replace')[-6000:])
            raise RuntimeError(name + ' unexpected exit ' + str(result.returncode))
        return result
    flags = [str(compiler), '-std=c++20', '-O2', '-DNDEBUG', '-DMICROPROFILE_ENABLED=0',
             '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL', '-pthread']
    flags += ['--target=aarch64-linux-android29', '-static', '-static-libstdc++'] if args.a64 else ['-msse4.1', '-msse4.2']
    for directory in (source, out / 'stubs', root / 'src', root / 'externals/fmt/include',
                      root / 'externals/boost', root / 'externals/xxHash', root / 'externals/nihstro/include',
                      root / 'externals/microprofile', root / 'externals/json',
                      root / ('externals/oaknut/include' if args.a64 else 'externals/xbyak')):
        flags.append('-I' + str(directory))
    def compile_one(cpp, name):
        obj = out / (name + '.o')
        run(name + '-compile', flags + ['-MMD', '-MF', obj.with_suffix('.d'), '-c', cpp, '-o', obj])
        raw = obj.with_suffix('.d').read_text().replace('\\\n', ' ').split(':',1)[1]
        for token in shlex.split(raw):
            p = Path(token); p = p if p.is_absolute() else root / p
            proof['dependencies'][str(p.resolve())] = sha(p)
        return obj
    original = (source / logical[1]).read_text()
    header_path = source / logical[2]
    header = header_path.read_text()
    instrumented = '// CodexAstraLocal: Test-only deterministic exceptional boundaries.\n' + \
        'namespace PacketProbe { void BeforeWave(); void BeforeInvocation(); }\n' + original
    instrumented = change(instrumented, '                const auto result = pool.Run(',
        '                PacketProbe::BeforeWave();\n                const auto result = pool.Run(')
    instrumented = change(instrumented, '                    context.Run(unit);',
        '                    PacketProbe::BeforeInvocation();\n                    context.Run(unit);')
    variants = [
        ('positive', instrumented, header, 'all', None),
        ('missing_active_completion', change(instrumented,
            '            for (const auto& packet : wave) packet->impl->Fail(terminal);',''), header,
            'failures','active wave lost completion'),
        ('missing_pending_completion', change(instrumented,
            '            for (const auto& packet : canceled) packet->impl->Fail(terminal);',''), header,
            'failures','pending wave lost completion'),
        ('forgot_packet_error',change(instrumented,'            error = std::current_exception();','            error = {};'),
            header,'failures','packet failure not terminal typed'),
        ('reset_carry',change(instrumented,'                    input.Load(unit, defaults, vertex - first_vertex);',
            '                    unit = ShaderUnit{};\n                    input.Load(unit, defaults, vertex - first_vertex);'),
            header,'normal','hardware differs'),
        ('lost_uniforms',change(instrumented,'packet->uniforms = source.uniforms;','packet->uniforms = {};'),
            header,'normal','hardware differs'),
        ('wrong_last_pair',change(instrumented,'last_pair = {converted[vertex_count - 3], converted[vertex_count - 2]};',
            'last_pair = {converted[vertex_count - 2], converted[vertex_count - 1]};'),header,'normal','last pair differs'),
        ('forgot_boolean_key',change(instrumented,'effect.booleans == booleans','true'),header,
            'bounds','late external effect escaped proof'),
        ('missing_arithmetic_floor',change(instrumented,'if (effect.minimum_arithmetic < 35)',
            'if (effect.minimum_arithmetic < 0)'),header,'work','arithmetic floor boundary'),
        ('input_count_as_misses',change(instrumented,'if (misses < 96)','if (source.count < 96)'),
            header,'work','FIFO miss floor boundary'),
        ('missing_copy_floor',change(instrumented,'if (raw_size > u64(misses) * 70)',
            'if (raw_size > u64(misses) * 700)'),header,'work','raw copy floor accepted71'),
        ('graph_nodes_as_work',instrumented,change(header,
            'if (terminal_minimum != Unknown) *work = {terminal_minimum, true};',
            'if (terminal_minimum != Unknown) *work = {static_cast<u32>(nodes.size()), true};'),
            'work','arithmetic floor boundary'),
        ('stale_work_bound',instrumented,change(header,'    if (work) *work = {};',
            '    if (work) {}'),'work','refusal reused stale work'),
    ]
    try:
        common = [compile_one(p,p.stem) for p in actual]
        fixture = compile_one(source / 'fixture.cpp','fixture')
        for name, cpp_text, header_text, selected, expected in variants:
            header_path.write_text(header_text)
            # CodexAstraLocal: Retain each exact header defect separately so CI
            # evidence can be reconstructed after the baseline copy is restored.
            (out / (name + '-header.h')).write_text(header_text)
            selected_fixture = fixture
            if header_text != header:
                selected_fixture = compile_one(source / 'fixture.cpp',name+'-fixture')
            cpp = out / (name+'.cpp'); cpp.write_text(cpp_text)
            queue = compile_one(cpp,name)
            binary = out / name
            link_flags = ['-Wl,--image-base=0x1000000000'] if args.a64 else []
            run(name+'-link',flags+[selected_fixture,queue,*common,*link_flags,'-o',binary])
            result = json.loads(run(name+'-run',[*prefix,binary,selected],0 if expected is None else 1).stdout)
            if expected is None:
                if result.get('passed') is not True or result.get('comparisons',0) < (1024 if args.a64 else 512):
                    raise RuntimeError('Incomplete positive population')
                proof['result'] = result
            elif result.get('passed') is not False or result.get('error') != expected:
                raise RuntimeError('Wrong negative-control failure: '+str(result))
            proof['variants'].append({'name':name,'source_sha256':sha(cpp),'header_sha256':sha(header_path),
                                      'binary_sha256':sha(binary),'result':result})
            print(name+': '+str(result),flush=True)
        proof['passed'] = True
    finally:
        header_path.write_text(header)
        # CodexAstraLocal: Header mutants are retained separately in the variant
        # hashes/commands; the final dependency record binds the restored baseline.
        if str(header_path.resolve()) in proof['dependencies']:
            proof['dependencies'][str(header_path.resolve())] = sha(header_path)
        proof['sources_stable'] = all(sha(p)==digest for p,digest in proof['source_sha256'].items())
        proof['dependencies_stable'] = all(sha(p)==digest for p,digest in proof['dependencies'].items())
        proof['artifacts'] = {str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file() and p.name!='provenance.json'}
        save()
    if not proof['sources_stable'] or not proof['dependencies_stable']:
        raise RuntimeError('Source drift during queue gate')
    print(json.dumps({'out':str(out),'passed':True,'result':proof['result'],'defects':len(variants)-1}))


if __name__ == '__main__':
    main()
