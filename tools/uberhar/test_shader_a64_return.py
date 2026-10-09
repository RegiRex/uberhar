#!/usr/bin/env python3
"""CodexAstraLocal: Execute production ARM64 shader return/ABI regression locally.

Requires an Android NDK clang++ and qemu-aarch64[-static]. Synthetic shaders use
the actual JIT, executable CodeBlock, public Run/BindForDraw and interpreter.
No renderer, device, title performance or complete shader-control-flow claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import shutil
import subprocess
import tempfile
import time


CASES = (
    'main_unused_call', 'plain_main', 'main_interior_boundary', 'direct_boundary_entry',
    'arbitrary_entry', 'last_slot_entry', 'ordinary_call', 'nested_calls',
    'conditional_call', 'uniform_call', 'nested_conditional_call', 'shared_suffix_calls',
    'conditional_body', 'main_helpers', 'callee_helpers', 'nested_callee_helpers',
    'main_loop_call', 'callee_loop', 'callee_loop_helpers', 'nested_loop_boundary',
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def no_core():
    # CodexAstraLocal: Defect controls run only in the bounded QEMU child; a
    # failed case must not leave core files or affect any emulator/device process.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def replace_once(text, old, new):
    require(text.count(old) == 1, 'return-contract source seam changed')
    return text.replace(old, new, 1)


def mutations(source):
    # CodexAstraLocal: Reconstruct the original marker algorithm in an isolated
    # copy, then separately break sentinel initialization and nested restoration.
    # No production file is rewritten; a crash/timeout is never a passing mutant.
    old = replace_once(source, '    MVN(RETURN_OFFSET, XZR);',
                       '    MVN(XSCRATCH0, XZR);\n    STR(XSCRATCH0, SP, 8);')
    old = replace_once(old, '    CMP(RETURN_OFFSET.toW(), program_counter);',
                       '    LDR(XSCRATCH0, SP, 16);\n    CMP(XSCRATCH0.toW(), program_counter);')
    old = replace_once(old,
        '    STP(RETURN_OFFSET, X30, SP, POST_INDEXED, -16);\n'
        '    MOV(RETURN_OFFSET, instr.flow_control.dest_offset + instr.flow_control.num_instructions);',
        '    MOV(XSCRATCH0, instr.flow_control.dest_offset + instr.flow_control.num_instructions);\n'
        '    STP(XSCRATCH0, X30, SP, POST_INDEXED, -16);')
    old = replace_once(old, '    LDP(RETURN_OFFSET, X30, SP, PRE_INDEXED, 16);',
                       '    LDP(XZR, X30, SP, PRE_INDEXED, 16);')
    return (
        ('original_stack_marker', old, 'main_unused_call', 'stack pointer not restored'),
        ('missing_main_sentinel', replace_once(source, '    MVN(RETURN_OFFSET, XZR);', ''),
         'main_unused_call', 'stack pointer not restored'),
        ('missing_nested_restore', replace_once(source,
            '    LDP(RETURN_OFFSET, X30, SP, PRE_INDEXED, 16);',
            '    LDP(XZR, X30, SP, PRE_INDEXED, 16);'),
         'nested_calls', 'interpreter/JIT architectural state differs'),
    )


def validate_tail_calls(text):
    # CodexAstraLocal: The fast path must tail-call the real generated function
    # before touching SP/X19/LR. A cold invalid-label exception tail is irrelevant
    # for these initialized entries, but compiler drift fails closed for review.
    for name in ('PublicRun', 'PublicBoundRun'):
        match = re.search(r'<'+name+r'>:\n(.*?)(?=\n\S|\Z)', text, re.S)
        require(match is not None, 'missing public entry wrapper disassembly')
        instructions = re.findall(r'^\s*[0-9a-f]+:\s+([^\n]+)', match[1], re.M)
        branch = next((i for i, line in enumerate(instructions)
                       if re.match(r'br\s+x\d+\b', line)), None)
        require(branch is not None, 'public wrapper no longer tail-calls')
        fast = instructions[:branch+1]
        require(not any(re.search(r'\b(?:sp|x19|w19|x30|w30|lr)\b', line) for line in fast),
                'public wrapper changes seeded entry ABI')
        require(not any(re.match(r'(?:bl|blr|ret)\b', line) for line in fast),
                'unexpected public wrapper call before generated entry')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/shader-a64-return'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    compiler = args.compiler
    if compiler is None:
        # CodexAstraLocal: Reuse an already installed NDK; this gate performs no
        # network install and fails rather than silently skipping A64 execution.
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        ndk_root = Path(ndk) if ndk else repo / 'build/native-deps/android-ndk-r27d'
        compiler = ndk_root / 'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++'
    # CodexAstraLocal: Preserve the clang++ invocation name; resolving its
    # clang-18 symlink would select the C linker driver and drop the C++ runtime.
    compiler = compiler.absolute()
    qemu = args.qemu or shutil.which('qemu-aarch64-static') or shutil.which('qemu-aarch64')
    require(compiler.is_file() and os.access(compiler, os.X_OK), 'Android NDK compiler unavailable')
    require(qemu is not None, 'QEMU ARM64 unavailable')
    qemu = Path(qemu).resolve()
    require(qemu.is_file() and os.access(qemu, os.X_OK), 'QEMU ARM64 not executable')
    objdump = compiler.parent / 'llvm-objdump'
    require(objdump.is_file(), 'NDK objdump unavailable')
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    stubs = out / 'stubs'
    # CodexAstraLocal: Only logging/assert endpoints are standalone adapters;
    # shader state, interpretation, machine generation and execution remain real.
    for relative, contents in {
        'common/assert.h': '''// CodexAstraLocal: Unexpected fixture assertions abort the bounded child.
#pragma once
#include <cstdlib>
#include "common/common_types.h"
#define ASSERT(x) ((x) ? (void)0 : std::abort())
#define ASSERT_MSG(x,...) ASSERT(x)
#define DEBUG_ASSERT(x) ASSERT(x)
#define DEBUG_ASSERT_MSG(x,...) ASSERT(x)
#define UNREACHABLE() std::abort()
#define UNREACHABLE_MSG(...) std::abort()
''',
        'common/logging/log.h': '''// CodexAstraLocal: Unexpected shader errors fail; no logger service is linked.
#pragma once
#include <cstdlib>
#define LOG_CRITICAL(...) std::abort()
#define LOG_ERROR(...) std::abort()
#define LOG_WARNING(...) std::abort()
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_TRACE(...) ((void)0)
''',
    }.items():
        path = stubs / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
    compiler_source = repo / 'src/video_core/shader/shader_jit_a64_compiler.cpp'
    sources = [Path(__file__).with_suffix('.cpp'), Path(__file__).with_suffix('.S')] + [repo / p for p in (
        'src/video_core/shader/shader_interpreter.cpp', 'src/video_core/pica/shader_unit.cpp',
        'src/video_core/pica/shader_setup.cpp')]
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'variants': [],
             'tool_sha256': {'compiler': sha(compiler), 'qemu': sha(qemu)},
             'source_sha256': {str(p.relative_to(repo)): sha(p) for p in sources + [compiler_source, Path(__file__)]}}
    proof_path = out / 'provenance.json'
    snapshot = out / 'source'
    snapshot.mkdir()
    for source_file in sources + [compiler_source, Path(__file__)]:
        copy = snapshot / source_file.relative_to(repo)
        copy.parent.mkdir(parents=True, exist_ok=True)
        copy.write_bytes(source_file.read_bytes())

    def save():
        proof_path.write_text(json.dumps(proof, indent=2)+'\n')

    original = compiler_source.read_text()
    variants = [('production', original, None, None), *mutations(original)]
    for name, body, selected_case, error in variants:
        directory = out / name
        directory.mkdir()
        source = directory / 'shader_jit_a64_compiler.cpp'
        source.write_text(body)
        binary = directory / 'fixture'
        command = [str(compiler), '--target=aarch64-linux-android33', '-std=c++20', '-O2',
                   '-static', '-static-libstdc++', '-pthread', '-ffunction-sections', '-fdata-sections',
                   '-Wl,--gc-sections', '-DMICROPROFILE_ENABLED=0', '-DFMT_HEADER_ONLY', '-I'+str(stubs)]
        for include in ('src', 'externals/oaknut/include', 'externals/nihstro/include',
                        'externals/boost', 'externals/microprofile', 'externals/fmt/include', 'externals/xxHash'):
            command.append('-I'+str(repo/include))
        command += [str(p) for p in sources] + [str(source), '-o', str(binary)]
        record = {'name': name, 'source_sha256': sha(source), 'compile_argv': command}
        proof['variants'].append(record)
        save()
        if name == 'production':
            # CodexAstraLocal: Ask the same compiler for real non-system input
            # dependencies and retain their hashes; do not infer them from names.
            dep_argv = command[:-2] + ['-MM', '-MT', 'inputs']
            deps = subprocess.run(dep_argv, capture_output=True, timeout=30, check=True)
            (out/'dependencies.mk').write_bytes(deps.stdout)
            dependency_paths = set()
            for line in deps.stdout.decode().replace('\\\n', '').splitlines():
                require(line.startswith('inputs:'), 'unexpected compiler dependency record')
                dependency_paths.update(Path(p) for p in shlex.split(line[len('inputs:'):]))
            proof['dependency_sha256'] = {str(p): sha(p) for p in sorted(dependency_paths)}
            save()
        started = time.monotonic()
        with (directory/'compile.log').open('wb') as log:
            compiled = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        record.update(compile_exit=compiled.returncode, compile_seconds=time.monotonic()-started)
        save()
        compiled.check_returncode()
        # CodexAstraLocal: Retain actual public wrapper code and dependency files
        # from the production binary; neither is substituted with a modeled JIT.
        dis = subprocess.run([str(objdump), '-d', '--no-show-raw-insn',
                              '--disassemble-symbols=PublicRun,PublicBoundRun', str(binary)],
                             capture_output=True, timeout=10, check=True)
        (directory/'public-entries.asm').write_bytes(dis.stdout)
        validate_tail_calls(dis.stdout.decode())
        run_argv = [str(qemu), str(binary)] + ([selected_case] if selected_case else [])
        started = time.monotonic()
        executed = subprocess.run(run_argv, capture_output=True, timeout=20, preexec_fn=no_core)
        (directory/'stdout.jsonl').write_bytes(executed.stdout)
        (directory/'stderr.log').write_bytes(executed.stderr)
        record.update(run_argv=run_argv, run_exit=executed.returncode,
                      run_seconds=time.monotonic()-started, binary_sha256=sha(binary))
        save()
        require(not executed.stderr, 'unexpected QEMU diagnostic')
        require(len(executed.stdout) < 32768, 'unbounded fixture output')
        lines = [json.loads(line) for line in executed.stdout.splitlines()]
        if name == 'production':
            require(executed.returncode == 0, 'production A64 regression failed')
            require(lines[:-1] == [{'case': c, 'passed': True} for c in CASES], 'case coverage changed')
            require(lines[-1] == {'passed': True, 'cases': 20, 'executions': 4480, 'assertions': 83537,
                                 'parallel_jobs': 80},
                    'A64 execution/assertion coverage changed')
        else:
            require(executed.returncode == 1 and lines == [
                {'passed': False, 'case': selected_case, 'error': error}],
                'defect control did not fail its intended assertion')
        record['validated'] = True
        save()
    # CodexAstraLocal: Execute the existing pool's independent exact-once,
    # participant, FP-status/rounding, exception-drain and reuse tests on A64 too.
    # The x86-only status controls remain outside this architecture's result.
    pool_source = repo/'tools/uberhar/test_parallel_work.cpp'
    pool_header = repo/'src/common/uberhar_parallel_work.h'
    pool_body = pool_source.read_text()
    pool_header_body = pool_header.read_text()
    # CodexAstraLocal: Keep the actual test body intact, but give its ordinary
    # function a void return and catch assertion exceptions in a tiny child main.
    # This makes the deliberate defect fail explicitly rather than via an abort.
    pool_wrapper = replace_once(pool_body, 'int main() {', 'void RunPoolRegression() {')
    pool_wrapper += '''
// CodexAstraLocal: A failed actual pool assertion is a bounded, explicit result.
int main() {
    try { RunPoolRegression(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\\n'; return 1; }
}
'''
    wrapper = out/'parallel-work-test.cpp'
    wrapper.write_text(pool_wrapper)
    proof['pool'] = {'source_sha256': sha(pool_source), 'header_sha256': sha(pool_header),
                     'wrapper_sha256': sha(wrapper), 'variants': []}
    pool_variants = (
        ('production', pool_header_body, 0, b''),
        ('missing_arm_status', replace_once(pool_header_body,
            '        const Status combined = Flags() | extra;',
            '        const Status combined = Flags() | (extra & 0x1fU);'),
         1, b'ARM worker-only sticky status lost or invented\n'),
    )
    for name, header, expected_exit, expected_error in pool_variants:
        directory = out/('pool-'+name)
        (directory/'common').mkdir(parents=True)
        (directory/'common/uberhar_parallel_work.h').write_text(header)
        pool_binary = directory/'fixture'
        pool_command = [str(compiler), '--target=aarch64-linux-android33', '-std=c++20', '-O2',
                        '-static', '-static-libstdc++', '-pthread', '-I'+str(directory),
                        '-I'+str(repo/'src'), str(wrapper), '-o', str(pool_binary)]
        record = {'name': name, 'compile_argv': pool_command,
                  'header_sha256': sha(directory/'common/uberhar_parallel_work.h')}
        proof['pool']['variants'].append(record)
        save()
        with (directory/'compile.log').open('wb') as log:
            compiled = subprocess.run(pool_command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        record['compile_exit'] = compiled.returncode
        save()
        compiled.check_returncode()
        pool_run = subprocess.run([str(qemu), str(pool_binary)], capture_output=True, timeout=60,
                                  preexec_fn=no_core)
        (directory/'stdout.log').write_bytes(pool_run.stdout)
        (directory/'stderr.log').write_bytes(pool_run.stderr)
        record.update(run_exit=pool_run.returncode, binary_sha256=sha(pool_binary))
        save()
        expected_output = (b'parallel work PASS: 738 jobs; exact-once output, 1/2/6/8/12/17 '
                           b'participants, FP state, failure drain\n') if expected_exit == 0 else b''
        require(pool_run.returncode == expected_exit and pool_run.stderr == expected_error and
                pool_run.stdout == expected_output, 'A64 independent pool/control regression failed')
        record['validated'] = True
        save()
    require(pool_source.read_text() == pool_body and pool_header.read_text() == pool_header_body,
            'pool source changed during the gate')
    require(original == compiler_source.read_text(), 'production shader changed during the gate')
    for name, digest in {**proof['source_sha256'], **proof['dependency_sha256']}.items():
        require(sha(repo/name) == digest, 'gate input changed during execution: '+name)
    proof['passed'] = True
    save()
    print('PASS: 20 A64 programs, 4480 executions, 83537 assertions, 80 shared-JIT jobs, '
          '3 return defects, 738 independent pool jobs, ARM IDC/QC and a status-merge defect')
    print(proof_path)


if __name__ == '__main__':
    main()
