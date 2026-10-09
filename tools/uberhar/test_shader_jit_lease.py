#!/usr/bin/env python3
"""CodexAstraLocal: Run bounded actual-engine lease lifetime/invalidation controls, without a device."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import resource
import shlex
import shutil
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def change(body, old, new):
    if body.count(old) != 1:
        raise RuntimeError('Defect seam changed: ' + old[:100])
    return body.replace(old, new, 1)


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--a64', action='store_true', help='Execute the actual Android A64 JIT under QEMU')
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/shader-jit-lease'))
    args = parser.parse_args()
    if not args.a64 and platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('Host gate requires x64; use --a64 with an Android NDK and QEMU')
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    sources = out / 'source'
    # CodexAstraLocal: Copy only the actual JIT ownership interface/implementation.
    # The weak observer affects the private header; repository inputs stay intact.
    jit_inputs = [ROOT / 'src/video_core/shader' / name
                  for name in ('shader.h', 'shader_jit.h', 'shader_jit.cpp')]
    for path in jit_inputs:
        target = sources / path.relative_to(ROOT / 'src')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    fixture_path = Path(__file__).with_suffix('.cpp')
    shutil.copyfile(fixture_path, sources / 'fixture.cpp')
    # CodexAstraLocal: The only test-header adaptation permits a weak ownership
    # observer. Executable selection and lease implementation remain real bodies.
    header = sources / 'video_core/shader/shader_jit.h'
    header.write_text(change(header.read_text(), 'private:\n',
        'private:\n    friend struct LeaseTestAccess; // CodexAstraLocal: Test-only weak owner observation.\n'))
    stubs = out / 'stubs/common'; stubs.mkdir(parents=True)
    (stubs / 'settings.h').write_text('''// CodexAstraLocal: Standalone diagnostic policy only.
#pragma once
namespace Settings {
enum class UberharTestMode { Custom };
struct Setting { UberharTestMode GetValue() const { return UberharTestMode::Custom; } };
inline struct { Setting uberhar_test_mode; } values;
}
''')
    actual = [ROOT / p for p in (
        'src/video_core/pica/shader_unit.cpp', 'src/video_core/pica/shader_setup.cpp',
        'src/video_core/shader/shader_interpreter.cpp',
        'src/video_core/shader/shader_jit_a64_compiler.cpp' if args.a64 else
        'src/video_core/shader/shader_jit_x64_compiler.cpp')]
    inputs = [Path(__file__).resolve(), fixture_path, *actual, *jit_inputs,
              ROOT / 'tools/uberhar/test_shader_jit_identity.cpp']
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'passed': False,
             'target': 'NDK API29 A64/QEMU' if args.a64 else 'host x64',
             'sources_before': {str(p): sha(p) for p in inputs},
             'commands': [], 'dependencies': {}, 'variants': []}
    def save():
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
    def run(name, argv, timeout=120, expected=0):
        argv = list(map(str, argv)); start = time.monotonic()
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, timeout=timeout, preexec_fn=no_core)
        (out / (name + '.stdout')).write_bytes(result.stdout)
        (out / (name + '.stderr')).write_bytes(result.stderr)
        proof['commands'].append({'name': name, 'argv': argv, 'exit': result.returncode,
                                  'seconds': time.monotonic() - start})
        save()
        if result.returncode != expected:
            print(result.stdout.decode(errors='replace')[-2000:])
            print(result.stderr.decode(errors='replace')[-6000:])
            raise RuntimeError(name + ' unexpected exit ' + str(result.returncode))
        return result
    # CodexAstraLocal: Reuse an installed toolchain and fail if unavailable.
    # Keep clang++'s invocation spelling instead of resolving its clang symlink.
    if args.a64:
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        ndk_root = Path(ndk) if ndk else ROOT / 'build/native-deps/android-ndk-r27d'
        compiler = args.compiler or ndk_root / 'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++'
        compiler_command = [str(compiler.absolute())]
    else:
        compiler_command = [str(args.compiler)] if args.compiler else shlex.split(os.environ.get('CXX', 'c++'))
    if not compiler_command:
        raise RuntimeError('Empty compiler command')
    compiler_path = shutil.which(compiler_command[0])
    if not compiler_path:
        raise RuntimeError('Compiler unavailable: ' + compiler_command[0])
    compiler_command[0] = str(Path(compiler_path).absolute())
    flags = compiler_command + ['-std=c++20', '-O2', '-DNDEBUG', '-DMICROPROFILE_ENABLED=0',
                                '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL', '-pthread']
    flags += ['--target=aarch64-linux-android29', '-static', '-static-libstdc++'] if args.a64 else ['-msse4.1', '-msse4.2']
    for directory in (sources, out / 'stubs', ROOT / 'src', ROOT / 'externals/fmt/include',
                      ROOT / 'externals/boost', ROOT / 'externals/xxHash',
                      ROOT / 'externals/nihstro/include', ROOT / 'externals/microprofile',
                      ROOT / ('externals/oaknut/include' if args.a64 else 'externals/xbyak')):
        flags.append('-I' + str(directory))
    proof['compiler_sha256'] = sha(compiler_path)
    prefix = []
    if args.a64:
        qemu = str(args.qemu) if args.qemu else shutil.which('qemu-aarch64-static') or shutil.which('qemu-aarch64')
        if not qemu or not Path(qemu).is_file() or not os.access(qemu, os.X_OK):
            raise RuntimeError('QEMU A64 unavailable')
        prefix = [str(Path(qemu).absolute())]
        proof['qemu_sha256'] = sha(prefix[0])
    def compile_one(cpp, name):
        obj = out / (name + '.o')
        run(name + '-compile', flags + ['-MMD', '-MF', obj.with_suffix('.d'), '-c', cpp, '-o', obj], 180)
        raw = obj.with_suffix('.d').read_text().replace('\\\n', ' ').split(':', 1)[1]
        for value in shlex.split(raw):
            path = Path(value); path = path if path.is_absolute() else ROOT / path
            proof['dependencies'][str(path.resolve())] = sha(path)
        return obj
    try:
        common = [compile_one(p, p.stem) for p in actual]
        fixture = compile_one(sources / 'fixture.cpp', 'fixture')
        jit_source = sources / 'video_core/shader/shader_jit.cpp'
        body = jit_source.read_text()
        # CodexAstraLocal: Every defect changes an actual admission/ownership
        # seam. An explicit fixture failure is required; crashes/timeouts do not pass.
        variants = [
            ('positive', body, 'all', None),
            ('missing_owner', change(body,
                'return {owner, context, owner->code, owner->swizzles, setup.entry_point};',
                'return {std::shared_ptr<const void>(owner.get(), [](const void*) {}), context, owner->code, owner->swizzles, setup.entry_point};'),
                'lifetime', 'lease did not retain compiled owner'),
            ('stale_revision', change(body, 'binding.revision == setup.GetCodeRevision()', 'true'),
                'admission', 'stale source revision admitted'),
            ('borrowed_source', change(body,
                'return {owner, context, owner->code, owner->swizzles, setup.entry_point};',
                'return {owner, context, setup.GetProgramCode(), setup.GetSwizzleData(), setup.entry_point};'),
                'admission', 'lease source changed after upload'),
            ('wrong_entry', change(body,
                'owner->shader->BindForDraw(setup, setup.entry_point)', 'owner->shader->BindForDraw(setup, 0)'),
                'bindings', 'leased generated output differs'),
            ('unprepared_pointer', change(body,
                'binding.entry && setup.cached_shader == binding.entry->shader.get()) {', 'binding.entry) {'),
                'bindings', 'unprepared reused address admitted'),
            ('hash_only', change(body,
                'entry.code == setup.GetProgramCode() && entry.swizzles == setup.GetSwizzleData()', 'true'),
                'admission', 'colliding compiled owner aliased'),
        ]
        baseline_obj = None
        for name, modified, selected, failure in variants:
            path = out / (name + '.cpp'); path.write_text(modified)
            obj = compile_one(path, name)
            binary = out / name
            run(name + '-link', flags + [fixture, obj, *common, '-o', binary])
            result = run(name + '-run', [*prefix, binary, selected], 45, 0 if failure is None else 1)
            text = result.stdout.decode()
            if failure is None:
                match = re.fullmatch(r'PASS lease_checks=(\d+) executions=(\d+)\n', text)
                if not match or int(match[1]) != 1127 or int(match[2]) != 276:
                    raise RuntimeError('Lease positive population changed: ' + text)
                proof['result'] = {'checks': int(match[1]), 'executions': int(match[2]), 'threads': 4}
                baseline_obj = obj
            elif text != 'FAIL: ' + failure + '\n':
                raise RuntimeError('Wrong defect failure: ' + text)
            if result.stderr: raise RuntimeError('Unexpected child diagnostic')
            proof['variants'].append({'name': name, 'passed': True, 'source_sha256': sha(path),
                                      'binary_sha256': sha(binary), 'output': text})
            print(name + ': ' + text.strip(), flush=True)
        # CodexAstraLocal: The original actual-engine identity fixture still
        # executes unchanged; this checks synchronous cache/copy compatibility.
        legacy_obj = compile_one(ROOT / 'tools/uberhar/test_shader_jit_identity.cpp', 'identity')
        legacy = out / 'identity'
        run('identity-link', flags + [legacy_obj, baseline_obj, *common, '-o', legacy])
        result = run('identity-run', [*prefix, legacy], 45)
        if result.stdout != b'PASS shader_identity_checks=756\n' or result.stderr:
            raise RuntimeError('Original identity fixture changed')
        proof['identity_checks'] = 756
        proof['passed'] = True
    finally:
        proof['sources_after'] = {p: sha(p) for p in proof['sources_before']}
        proof['sources_stable'] = proof['sources_before'] == proof['sources_after']
        proof['dependencies_stable'] = all(sha(p) == digest for p, digest in proof['dependencies'].items())
        proof['artifacts'] = {str(p.relative_to(out)): sha(p) for p in out.rglob('*')
                              if p.is_file() and p.name != 'provenance.json'}
        save()
    if not proof['sources_stable'] or not proof['dependencies_stable']:
        raise RuntimeError('Lease proof inputs changed')
    print(json.dumps({'out': str(out), 'passed': True, 'result': proof['result']}), flush=True)


if __name__ == '__main__':
    main()
