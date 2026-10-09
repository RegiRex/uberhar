#!/usr/bin/env python3
"""CodexAstraLocal: Execute guest/host FP-status isolation on the actual A64 CPU JIT.

This finite synthetic gate supports the separately scoped no-GS observable vertex
contract. It does not establish device speed or the correctness of other backends.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile
import time


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def no_core():
    # CodexAstraLocal: A deliberately broken backend remains a bounded child.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def replace_once(source, old, new):
    require(source.count(old) == 1, 'review changed backend seam: ' + old)
    return source.replace(old, new, 1)


def check_records(path):
    # CodexAstraLocal: Independently enumerate the entire guest population and
    # inspect actual VMRS/software status/rounding, rejecting omitted duplicates.
    status = 0x0800009f
    seeds, poisons = (0, 0x08000000, 0x80, 0x08000080, 1, status), (0, status, 0x08000000, 0x80, 0x1c, 3)
    records = iter(path.read_text().splitlines())
    cases = executions = 0
    for mode in range(16):
        controls = ((mode & 3) << 22) | ((mode & 4) << 22) | ((mode & 8) << 22)
        for route in range(9):
            for mapping in range(3):
                if route == 0 and mapping == 2:
                    continue
                for seed in seeds:
                    for poison in poisons:
                        cases += 1
                        for round_number in range(2):
                            executions += 1
                            line = next(records, None)
                            require(line is not None, 'missing guest execution')
                            row = json.loads(line)
                            expected = dict(case=cases, round=round_number, mode=mode,
                                path=route, mapping=mapping, seed=seed,
                                poison=poison ^ (status if round_number else 0),
                                first=controls | seed | (0x13 if round_number else 2),
                                second=controls | seed | 0x13, fpscr=controls | seed | 0x13,
                                sum=0x3f800001 if (mode & 3) == 1 else 0x3f800000,
                                callbacks=0 if mapping == 2 else 1)
                            require(row == expected, 'guest status/population differs: ' + str(row))
    require(next(records, None) is None, 'extra guest execution')
    require((cases, executions) == (14976, 29952), 'changed fixture population')
    return dict(cases=cases, executions=executions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/guest-a64-fpsr'))
    args = parser.parse_args()
    require(1 <= args.jobs <= 16, 'bounded build job count required')
    repo = Path(__file__).resolve().parents[2]
    ndk_env = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
    ndk = (args.ndk or (Path(ndk_env) if ndk_env else repo/'build/native-deps/android-ndk-r27d')).absolute()
    qemu = args.qemu or shutil.which('qemu-aarch64-static') or shutil.which('qemu-aarch64')
    require(qemu is not None, 'installed ARM64 QEMU required')
    qemu = Path(qemu).resolve()
    toolchain = ndk/'build/cmake/android.toolchain.cmake'
    compiler = ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++'
    require(toolchain.is_file() and compiler.is_file() and qemu.is_file(), 'installed NDK/QEMU required')
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    fixture = Path(__file__).with_suffix('.cpp')
    shutil.copy2(fixture, out/'fixture.cpp')
    fpsr = repo/'externals/dynarmic/src/dynarmic/backend/arm64/fpsr_manager.cpp'
    original = fpsr.read_text()
    # CodexAstraLocal: Link each complete actual manager override ahead of its
    # archive member. The vendored backend stays unchanged; one machine defect is
    # executed per binary against the same guest oracle.
    mutations = {
        'missing_clear': ('code.MSR(oaknut::SystemReg::FPSR, XZR);',
            'code.NOP(); // CodexAstraLocal: Deliberate missing guest collector clear.'),
        'missing_spill': ('code.ORR(Wscratch0, Wscratch0, Wscratch1);',
            'code.NOP(); // CodexAstraLocal: Deliberate loss of prior guest status.'),
        'inactive_spill': ('    if (!fpsr_loaded)\n        return;',
            '    // CodexAstraLocal: Deliberate inactive collector spill leaks callback status.'),
    }
    for name, (old, new) in mutations.items():
        (out/(name+'.cpp')).write_text(replace_once(original, old, new))
    # CodexAstraLocal: Use only pinned local dependencies and the same Android ABI
    # as the app. This builds the real A32 frontend/ARM64 backend, not a status mock.
    def quoted(path):
        value = str(path)
        require(not any(c in value for c in ('"', '\n', '\r', ';', '$')), 'unsupported CMake path')
        return '"' + value.replace('\\', '/') + '"'
    cmake = '''# CodexAstraLocal: Generated finite actual backend contract fixture.
cmake_minimum_required(VERSION 3.22)
project(UberharGuestFpsr LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_FRONTENDS A32 CACHE STRING "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "" FORCE)
'''
    cmake += 'set(Boost_INCLUDE_DIR '+quoted(repo/'externals/boost')+' CACHE PATH "" FORCE)\n'
    cmake += 'set(BOOST_ROOT '+quoted(repo/'externals/boost')+' CACHE PATH "" FORCE)\n'
    cmake += 'add_subdirectory('+quoted(repo/'externals/dynarmic')+' dynarmic)\n'
    for name in ('baseline', *mutations):
        cmake += f'add_executable(guest_{name} fixture.cpp'+('' if name == 'baseline' else ' '+name+'.cpp')+')\n'
        cmake += f'target_include_directories(guest_{name} PRIVATE '+quoted(repo/'externals/dynarmic/tests/A32')+')\n'
        cmake += f'target_link_libraries(guest_{name} PRIVATE dynarmic merry::mcl merry::oaknut)\n'
        cmake += f'target_link_options(guest_{name} PRIVATE -static -Wl,--image-base=0x1000000000)\n'
    (out/'CMakeLists.txt').write_text(cmake)
    proof = dict(author='CodexAstraLocal', scope=__doc__, passed=False, commands=[],
        tool_sha256={str(p): sha(p) for p in (compiler, qemu, toolchain)},
        source_sha256={str(p): sha(p) for p in (Path(__file__), fixture, fpsr)},
        private_assets=False, device_operations=False)
    def save():
        (out/'provenance.json').write_text(json.dumps(proof, indent=2)+'\n')
    def run(label, argv, timeout, suffix='log'):
        start = time.monotonic()
        with (out/(label+'.'+suffix)).open('wb') as stdout, (out/(label+'.stderr.log')).open('wb') as stderr:
            result = subprocess.run(argv, stdout=stdout, stderr=stderr, timeout=timeout, preexec_fn=no_core)
        proof['commands'].append(dict(label=label, argv=argv, exit=result.returncode, seconds=time.monotonic()-start))
        save()
        return result.returncode
    try:
        require(run('configure', ['cmake','-S',str(out),'-B',str(out/'build'),'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),'-DANDROID_ABI=arm64-v8a',
            '-DANDROID_PLATFORM=android-33','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release'], 120) == 0,
            'backend fixture configure failed')
        require(run('compile', ['cmake','--build',str(out/'build'),'--target',
            *('guest_'+name for name in ('baseline', *mutations)), '-j'+str(args.jobs)], 600) == 0,
            'backend fixture compilation failed')
        # CodexAstraLocal: Ninja's actual dependency graph binds vendored headers
        # and sources used by this build. Tool hashes bind the separately installed
        # NDK; no copied binary is accepted without its current source identity.
        dep = subprocess.run(['ninja','-C',str(out/'build'),'-t','deps'], capture_output=True, text=True, check=True, timeout=60)
        (out/'dependencies.log').write_text(dep.stdout)
        bound = set()
        for line in dep.stdout.splitlines():
            if line.startswith('    '):
                p = Path(line.strip())
                if p.is_file() and p.is_relative_to(repo) and not p.is_relative_to(repo/'build'):
                    bound.add(p)
        require(len(bound) > 100, 'missing actual backend dependency graph')
        proof['source_sha256'].update({str(p): sha(p) for p in sorted(bound)})
        proof['generated_sha256'] = {str(p.relative_to(out)): sha(p) for p in out.glob('*.cpp')}
        proof['generated_sha256']['CMakeLists.txt'] = sha(out/'CMakeLists.txt')
        proof['binary_sha256'] = {name: sha(out/'build'/('guest_'+name)) for name in ('baseline', *mutations)}
        save()
        require(run('baseline', [str(qemu),str(out/'build/guest_baseline')], 300, 'jsonl') == 0,
            'actual guest FP-status contract failed')
        proof['population'] = check_records(out/'baseline.jsonl')
        require((out/'baseline.stderr.log').read_text() == 'PASS cases=14976 executions=29952 checks=317952\n',
            'guest fixture checks/population changed')
        for name in mutations:
            require(run(name, [str(qemu),str(out/'build'/('guest_'+name))], 30, 'jsonl') == 2,
                'backend defect did not fail: '+name)
            expected_check = 2 if name == 'missing_spill' else 12
            require((out/(name+'.stderr.log')).read_text() ==
                f'FAIL immediate post-callback guest FPSCR check={expected_check}\n', 'wrong defect assertion: '+name)
        require(all(sha(Path(p)) == digest for p,digest in proof['source_sha256'].items()), 'source changed during gate')
        proof['passed'] = True
        proof['result_sha256'] = {p.name: sha(p) for p in out.glob('*.jsonl')}
    finally:
        save()
    print(json.dumps(dict(passed=True, output=str(out), **proof['population'], detected_backend_defects=len(mutations))))


if __name__ == '__main__':
    main()
