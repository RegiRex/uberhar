#!/usr/bin/env python3
"""CodexAstraLocal: Bound actual sleeping-transport/clock/lifetime controls.

Native Linux by default; --compiler <NDK clang++> --qemu <qemu-aarch64> executes
the same controls as Android/API33 A64. No device, scheduling or policy changes.
The production header is never edited. Synthetic syscall outcomes and one
delayed-notification seam exist only in retained child include shadows.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import tempfile
import time


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text, old, new):
    require(text.count(old) == 1, 'transport source seam changed: ' + old[:64])
    return text.replace(old, new, 1)


def no_core():
    # CodexAstraLocal: Deliberate terminal-error children have finite outcomes
    # and must not leave core files; unrelated timeouts never count as a pass.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/parallel-transport'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    compiler = args.compiler or shutil.which('c++')
    require(compiler is not None, 'C++ compiler unavailable')
    compiler = Path(compiler).absolute()
    require(compiler.is_file() and os.access(compiler, os.X_OK), 'compiler is not executable')
    launcher = []
    flags = ['-frounding-math']
    if args.qemu:
        qemu = args.qemu.resolve()
        require(qemu.is_file() and os.access(qemu, os.X_OK), 'QEMU is not executable')
        launcher = [str(qemu)]
        flags = ['--target=aarch64-linux-android33', '-static', '-static-libstdc++']
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    header = repo/'src/common/uberhar_parallel_work.h'
    regression = repo/'tools/uberhar/test_parallel_work.cpp'
    fixture = Path(__file__).with_suffix('.cpp')
    inputs = [header, regression, fixture, Path(__file__).resolve()]
    proof = {'author':'CodexAstraLocal', 'scope':__doc__,
             'source_sha256':{str(p.relative_to(repo)):sha(p) for p in inputs},
             'compiler_sha256':sha(compiler), 'commands':[], 'variants':{}}
    if launcher:
        proof['qemu_sha256'] = sha(Path(launcher[0]))
    sources = out/'source'
    for path in inputs:
        target = sources/path.relative_to(repo)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())

    def save():
        (out/'provenance.json').write_text(json.dumps(proof, indent=2)+'\n')

    def command(name, argv, expected=0, stdout=None, stderr=b'', timeout=120):
        started = time.monotonic()
        process = subprocess.run([str(x) for x in argv], cwd=repo, capture_output=True,
                                 timeout=timeout, preexec_fn=no_core)
        (out/(name+'.stdout')).write_bytes(process.stdout)
        (out/(name+'.stderr')).write_bytes(process.stderr)
        proof['commands'].append({'name':name, 'argv':[str(x) for x in argv],
                                  'exit':process.returncode, 'seconds':time.monotonic()-started})
        save()
        # CodexAstraLocal: CI must expose the first compiler diagnostic when a
        # child fails; keep the console excerpt bounded and retain every byte.
        if process.returncode != expected:
            excerpt = process.stderr[:16384]
            print(f'{name}: exit {process.returncode}, expected {expected}; '
                  f'stderr excerpt {len(excerpt)}/{len(process.stderr)} bytes '
                  f'(full: {out/(name+".stderr")})', file=sys.stderr)
            if excerpt:
                print(excerpt.decode('utf-8', errors='replace'), file=sys.stderr)
        require(process.returncode == expected, name+': unexpected exit; see retained stderr')
        if stdout is not None:
            require(process.stdout == stdout, name+': unexpected result')
        if stderr is not None:
            require(process.stderr == stderr, name+': unexpected diagnostic')
        return process

    def build(name, text, source):
        directory = out/name
        (directory/'common').mkdir(parents=True)
        shadow = directory/'common/uberhar_parallel_work.h'
        shadow.write_text(text)
        argv = [str(compiler), '-std=c++20', '-O2', '-pthread', *flags,
                '-I'+str(directory), '-I'+str(repo/'src')]
        binary = directory/'fixture'
        argv += [str(source), '-o', str(binary)]
        command(name+'-compile', argv, stderr=None)
        proof['variants'][name] = {'header_sha256':sha(shadow), 'binary_sha256':sha(binary)}
        save()
        return launcher+[str(binary)]

    raw = header.read_text()
    # CodexAstraLocal: Preserve all actual pool tests while selecting the CV
    # branch, wrapping the generation, or hiding the syscall macro only after
    # the pool's includes. A forced prefix also hid it from libstdc++ itself.
    variants = {
        'actual':raw,
        'cv':replace_once(raw, 'const bool use_futex{ParallelFutexWord::Available()};',
                         'const bool use_futex{false};'),
        'wrapped':replace_once(replace_once(raw, 'ParallelFutexWord generation;',
                                           'ParallelFutexWord generation{0xfffffffeU};'),
                               'std::uint32_t observed = 0;', 'std::uint32_t observed = 0xfffffffeU;'),
        'no_sys_futex':replace_once(raw, '\nnamespace Common::Uberhar {\n',
            '\n// CodexAstraLocal: Simulate the pool compile fallback after standard\n'
            '// headers have parsed their own platform-specific syscall paths.\n'
            '#undef SYS_futex\n\nnamespace Common::Uberhar {\n'),
    }
    expected_pool = (b'parallel work PASS: 738 jobs; exact-once output, 1/2/6/8/12/17 '
                     b'participants, FP state, failure drain\n')
    for name, text in variants.items():
        child = build(name, text, regression)
        command(name+'-run', child, stdout=expected_pool, timeout=60)

    # CodexAstraLocal: The copied source preserves every owner/worker branch.
    # Only the syscall boundary and final-notify scheduling seam are adapted.
    instrumented = raw.replace('::syscall(', 'FutexTest::Syscall(')
    require(instrumented != raw and instrumented.count('FutexTest::Syscall(') == 4,
            'unexpected syscall adapter population')
    instrumented = replace_once(instrumented,
        'if (unfinished.Complete() == 1)\n                    unfinished.Wake();',
        'if (unfinished.Complete() == 1) {\n                    FutexTest::BeforeFinalWake();\n'
        '                    unfinished.Wake();\n                }')
    child = build('transport', instrumented, fixture)
    command('transport-run', child, stdout=b'transport controls PASS 28\n', timeout=30)
    command('denied-probe', child+['denied-probe'], stdout=b'denied probe PASS 3\n', timeout=10)
    command('permanent-wait', child+['permanent-wait'], expected=77, stdout=b'', timeout=10)
    command('permanent-wake', child+['permanent-wake'], expected=77, stdout=b'', timeout=10)
    timing_result = b'timing controls PASS: 12 measured/unmeasured cases\n'
    command('timing-run', child+['timing'], stdout=timing_result, timeout=30)

    # CodexAstraLocal: Deterministic clock endpoints test the performance
    # contract directly: zero calls unmeasured, two serial, three joined.
    counted = instrumented.replace('std::chrono::steady_clock::now()', 'FutexTest::ReadClock()')
    require(counted != instrumented, 'optional timing endpoints missing')
    child = build('counted', counted, fixture)
    command('counted-run', child+['counted-timing'], stdout=timing_result, timeout=30)

    # CodexAstraLocal: Each defect must fail a named behavior, not compile, crash
    # or time out. These never modify the source or the host's syscall policy.
    clock_defect = replace_once(counted,
        'const auto owner_start = measure ? FutexTest::ReadClock()\n'
        '                                         : std::chrono::steady_clock::time_point{};',
        'const auto owner_start = FutexTest::ReadClock();')
    child = build('unconditional_clock', clock_defect, fixture)
    command('unconditional-clock-run', child+['counted-timing'], expected=1, stdout=b'',
            stderr=b'clock branch contract failed\n', timeout=10)
    probe_defect = replace_once(instrumented,
        'const bool use_futex{ParallelFutexWord::Available()};', 'const bool use_futex{true};')
    child = build('ignored_probe', probe_defect, fixture)
    command('ignored-probe-run', child+['denied-probe'], expected=1, stdout=b'',
            stderr=b'denied probe transport identity\n', timeout=10)
    error_defect = replace_once(instrumented,
        '        if (failure != 0 && failure != EINTR && failure != EAGAIN)\n'
        '            std::terminate();', '')
    error_defect = replace_once(error_defect,
        '        if (result < 0)\n            std::terminate();', '')
    child = build('ignored_terminal_error', error_defect, fixture)
    command('ignored-wait-error-run', child+['permanent-wait'], expected=78, stdout=b'', timeout=10)
    command('ignored-wake-error-run', child+['permanent-wake'], expected=78, stdout=b'', timeout=10)
    for path in inputs:
        require(sha(path) == proof['source_sha256'][str(path.relative_to(repo))],
                'source changed during transport gate')
    proof['passed'] = True
    save()
    print('PASS: four 738-job transports, 28 race/syscall assertions, denied/terminal controls, '
          '12 real and 12 counted timing cases, four behavior-sensitive defect outcomes; '+str(out))


if __name__ == '__main__':
    main()
