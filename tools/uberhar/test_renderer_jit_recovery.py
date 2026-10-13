#!/usr/bin/env python3
"""CodexAstraLocal: Check actual SVC recovery bodies against a built native Dynarmic archive.

This is a finite correctness check, not a performance or Android hardware result.
It reuses the existing build; it never launches a second native build implicitly.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time


def method(source, signature):
    if source.count(signature) != 1:
        raise RuntimeError('production method anchor changed: ' + signature)
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/uberhar-native'))
    parser.add_argument('--output-root', type=Path)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    # CodexAstraLocal: Retry only an unfinished source-control case locally;
    # the default release invocation always checks baseline and every defect.
    parser.add_argument('--case', action='append', choices=(
        'baseline', 'omit_halt', 'mutable_target', 'omit_delivery', 'omit_pending'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    if bool(args.compiler) != bool(args.qemu):
        raise RuntimeError('NDK compiler and ARM64 QEMU must be supplied together')
    parent = args.output_root or root / 'build/uberhar-probe/renderer-jit-recovery'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent)).resolve()
    source_path = root / 'src/core/arm/dynarmic/arm_dynarmic.cpp'
    header_path = source_path.with_suffix('.h')
    source = source_path.read_text()
    callback = method(source, 'void CallSVC(std::uint32_t swi) override')
    run = method(source, 'void ARM_Dynarmic::Run()')
    step = method(source, 'void ARM_Dynarmic::Step()')
    fields = ['Dynarmic::A32::Jit* jit = nullptr;', 'Dynarmic::A32::Jit* executing_jit = nullptr;',
              'std::exception_ptr renderer_failure;']
    for field in fields:
        if header_path.read_text().count(field) != 1:
            raise RuntimeError('actual recovery field changed: ' + field)
    actual = callback + '\n' + run + '\n' + step
    pending_check = 'if (renderer_failure) [[unlikely]] {\n        std::rethrow_exception(renderer_failure);\n    }'
    variants = {'baseline': actual,
        'omit_halt': actual.replace('parent.executing_jit->HaltExecution();', '(void)0;'),
        'mutable_target': actual.replace('parent.executing_jit->HaltExecution();', 'parent.jit->HaltExecution();'),
        'omit_delivery': actual.replace('executing_jit->Run();\n    if (renderer_failure)',
                                       'executing_jit->Run();\n    if (false)'),
        'omit_pending': callback + '\n' + run.replace(pending_check, '', 1) + '\n' +
                        step.replace(pending_check, '', 1)}
    for name, value in variants.items():
        if name != 'baseline' and value == actual:
            raise RuntimeError('intended control seam changed: ' + name)
    generated = ''
    for name, value in variants.items():
        split = value.index('\nvoid ARM_Dynarmic::Run()')
        generated += f'namespace {name} {{\nclass ARM_Dynarmic {{ public:\n'
        generated += '\n'.join(fields) + '\nMemoryStub memory; void* current_page_table = nullptr; bool break_flag = false; void Run(); void Step(); };\n'
        generated += 'class DynarmicUserCallbacks final : public ArmTestEnv { public: ARM_Dynarmic& parent; SvcStub svc_context; explicit DynarmicUserCallbacks(ARM_Dynarmic& p) : parent(p) {}\n'
        generated += value[:split] + '\n};\n' + value[split:] + '\n}\n'
    (out / 'recovery_actual.inc').write_text(generated)
    fixture = Path(__file__).with_suffix('.cpp')
    # CodexAstraLocal: The existing ARM gate has already built these exact
    # archives. Reuse them without a second backend build or host archive guess.
    if args.qemu:
        dynarmic_build = build / 'dynarmic'
        libs = [dynarmic_build / 'src/dynarmic/libdynarmic.a',
                dynarmic_build / 'externals/mcl/src/libmcl.a',
                dynarmic_build / 'externals/fmt/libfmt.a']
        # CodexAstraLocal: clang++ may symlink to clang-N; resolving argv[0]
        # silently selects the C driver and drops C++ exception/runtime linkage.
        compiler = [str(args.compiler.absolute()), '--driver-mode=g++']
        machine_flags = ['--target=aarch64-linux-android33', '-static', '-static-libstdc++',
                         '-Wl,--image-base=0x1000000000']
        runner = [str(args.qemu.resolve())]
        fmt_include = 'externals/dynarmic/externals/fmt/include'
    else:
        libs = [build / 'externals/dynarmic/src/dynarmic/libdynarmic.a',
                build / 'externals/dynarmic/externals/mcl/src/libmcl.a', build / 'externals/fmt/libfmt.a',
                build / 'externals/dynarmic/externals/zydis/libZydis.a',
                build / 'externals/dynarmic/externals/zydis/zycore/libZycore.a']
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        machine_flags, runner = [], []
        fmt_include = 'externals/fmt/include'
    for lib in libs:
        if not lib.is_file():
            raise RuntimeError('existing native archive required: ' + str(lib))
    includes = ['src', 'externals/dynarmic/src', 'externals/dynarmic/tests/A32',
                'externals/dynarmic/externals/mcl/include', fmt_include,
                'externals/boost']
    command = compiler + ['-std=c++20', '-O2', '-pthread', *machine_flags,
                          '-MMD', '-MF', str(out/'recovery.d')]
    command += ['-I' + str(root / p) for p in includes] + ['-I' + str(out)]
    command += [str(fixture), '-o', str(out / 'recovery'), '-Wl,--start-group']
    command += list(map(str, libs)) + ['-Wl,--end-group']
    proof = {'passed': False, 'started_unix': time.time(), 'commands': [],
             'cases': args.case or list(variants),
             'scope': 'source-extracted actual production callback/Run/Step; real A32 JIT',
             'backend': 'A64 Android API33 under QEMU' if args.qemu else 'native host'}
    expected_failure = {'omit_halt': 'no post-error guest sentinel or SVC',
                        'mutable_target': 'no post-error guest sentinel or SVC',
                        'omit_delivery': 'immediate C++ delivery',
                        'omit_pending': 'pending delivered before break/new guest progress'}
    def execute(label, argv):
        with (out / (label + '.stdout')).open('w') as stdout, (out / (label + '.stderr')).open('w') as stderr:
            result = subprocess.run(argv, cwd=root, stdout=stdout, stderr=stderr, timeout=120)
        proof['commands'].append({'label': label, 'argv': argv, 'exit_code': result.returncode})
        return result.returncode
    try:
        if execute('compile', command):
            raise RuntimeError('compile failed: ' + str(out / 'compile.stderr'))
        for name in proof['cases']:
            code = execute(name, runner + [str(out / 'recovery'), name])
            if name == 'baseline':
                if code or 'PASS real JIT recovery' not in (out / (name + '.stdout')).read_text():
                    raise RuntimeError('actual JIT recovery failed')
            elif code != 2 or expected_failure[name] not in (out / (name + '.stderr')).read_text():
                raise RuntimeError('intended failure not detected: ' + name)
        proof['passed'] = True
    finally:
        proof['ended_unix'] = time.time()
        inputs = {Path(__file__), fixture, source_path, header_path, *libs}
        if args.compiler:
            inputs.update((args.compiler.resolve(), args.qemu.resolve()))
        dep = out/'recovery.d'
        if dep.is_file():
            text = dep.read_text().replace('\\\n', '')
            inputs.update((root/p).resolve() for p in shlex.split(text.split(':', 1)[1]))
        proof['inputs'] = [{'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                           for p in sorted(inputs)]
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(json.dumps({'passed': proof['passed'], 'output': str(out)}))


if __name__ == '__main__':
    main()
