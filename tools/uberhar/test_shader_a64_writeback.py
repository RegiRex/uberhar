#!/usr/bin/env python3
"""CodexAstraLocal: Execute the production A64 partial-writeback regression.

Compile the actual current emitter twice with identical observation-only adapters.
Execute under local Android/Bionic QEMU; retain exact emitted words and compare
all state bytes/FP status. No timing or target-throughput conclusion is derived.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shlex
import shutil
import subprocess
import time
import tempfile
import struct


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text, old, new):
    require(text.count(old) == 1, 'changed observation/candidate source seam: '+old[:80])
    return text.replace(old, new, 1)


def no_core():
    # CodexAstraLocal: A mutant crash must remain a failed bounded local child.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def inspect_artifacts(out):
    # CodexAstraLocal: Bind LLVM-decoded FP words to actual generated words;
    # a writeback-only change must preserve every emitted FP instruction.
    meta = [json.loads(line) for line in (out/'emitted.jsonl').read_text().splitlines()]
    indexed = {(row['variant'], row['id']): row for row in meta}
    require(len(meta) == len(indexed) == 960, 'changed emitted population')
    words = {}
    name = None
    for line in (out/'emitted.S').read_text().splitlines():
        match = re.fullmatch(r'(baseline|candidate)_(\d+):', line)
        if match:
            name = (match[1], int(match[2])); words[name] = []
        elif line.startswith('.inst '):
            require(name is not None, 'unbound emitted word')
            words[name].append(int(line.split()[1]))
    fp = {name: [] for name in words}
    current, base = None, None
    for line in (out/'emitted.asm').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]+) <(baseline|candidate)_(\d+)>:', line)
        if match:
            base = int(match[1], 16); current = (match[2], int(match[3])); continue
        match = re.match(r'\s*([0-9a-f]+):\s+(f\w+)\b', line)
        if match and current:
            offset = int(match[1], 16)-base
            require(offset >= 0 and offset % 4 == 0 and offset//4 < len(words[current]),
                    'instruction offset mismatch')
            fp[current].append((match[2], words[current][offset//4]))
    for ident in range(480):
        require(fp['baseline', ident] == fp['candidate', ident], 'FP sequence changed')
        for variant in ('baseline', 'candidate'):
            require(len(words[variant, ident]) == indexed[variant, ident]['words'],
                    'emitted word count mismatch')
    require(any(fp.values()), 'no decoded FP instructions')
    # CodexAstraLocal: Require each exact state/FP pair, including every raw MOV
    # rotation through both banks/entry APIs. Reject duplicates and truncation.
    modes = {0, 1 << 22, 2 << 22, 3 << 22, 1 << 24, (1 << 24) | (1 << 25)}
    expected = {(ident, seed, bank, mode, 0) for ident in range(480)
                for seed in range(3) for bank in range(2) for mode in modes}
    for ident in range(480):
        if indexed['baseline', ident]['opcode'] == 19:
            expected.update((ident, seed, bank, mode, rotation)
                            for seed in (1, 2) for bank in range(2)
                            for mode in (0, (1 << 24) | (1 << 25))
                            for rotation in range(1, 17))
    seen = set()
    with (out/'state-comparisons.bin').open('rb') as data:
        while True:
            header = data.read(64)
            if not header: break
            require(len(header) == 64, 'partial pair header')
            ident, seed, bank, mode, left_flags, right_flags, count, rotation = struct.unpack('<8Q', header)
            key = (ident, seed, bank, mode, rotation)
            require(key in expected and key not in seen and count == 456, 'invalid pair population')
            seen.add(key)
            left, right = data.read(count*4), data.read(count*4)
            require(len(left) == count*4 and left == right, 'raw state changed or truncated')
            require(left_flags == right_flags, 'FP status differs')
    require(seen == expected, 'missing exact-state pairs')
    return dict(paired_states=len(seen), fp_instruction_sequence_pairs=480,
                raw_mov_pairs=sum(key[-1] != 0 for key in seen),
                scope='Finite synthetic A64/QEMU proof; no timing or device rendering claim')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/shader-a64-writeback'))
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    # CodexAstraLocal: Reuse an installed NDK/QEMU, with no network download or
    # silent skip. Preserve clang++ spelling so its C++ linker driver is selected.
    ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
    ndk_root = Path(ndk) if ndk else repo/'build/native-deps/android-ndk-r27d'
    compiler = (args.compiler or ndk_root/'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++').absolute()
    qemu_name = args.qemu or shutil.which('qemu-aarch64-static') or shutil.which('qemu-aarch64')
    require(qemu_name is not None, 'QEMU ARM64 unavailable')
    qemu = Path(qemu_name).resolve()
    require(compiler.is_file() and qemu.is_file(), 'installed compiler/QEMU required')
    objdump = compiler.parent/'llvm-objdump'
    compiler_cpp = repo/'src/video_core/shader/shader_jit_a64_compiler.cpp'
    compiler_h = compiler_cpp.with_suffix('.h')
    candidate, header = compiler_cpp.read_text(), compiler_h.read_text()
    # CodexAstraLocal: Reconstruct only the inherited writeback by removing the
    # explicitly bounded optimization. All surrounding current arithmetic and
    # control flow stay identical; source drift fails for deliberate review.
    start_marker = '    // CodexAstraLocal: Arithmetic has already executed.'
    end_marker = '    // If all components are enabled, write the result to the destination register'
    require(candidate.count(start_marker) == candidate.count(end_marker) == 1,
            'partial-store reconstruction seam changed')
    begin, end = candidate.index(start_marker), candidate.index(end_marker)
    require(begin < end, 'writeback markers reversed')
    original = candidate[:begin] + candidate[end:]
    (out/'candidate.patch').write_text(''.join(difflib.unified_diff(
        original.splitlines(True), candidate.splitlines(True),
        fromfile='baseline.cpp', tofile='candidate.cpp')))
    (out/'candidate.cpp').write_text(candidate)
    # CodexAstraLocal: These accessors/range records observe generated words;
    # no machine instruction or production state layout is changed by adapters.
    observation = '''
    // CodexAstraLocal: Private code/range observation only, never a production API.
    const auto& ProbeCode() const { return probe_code; }
    const auto& ProbeRanges() const { return probe_ranges; }
    std::ptrdiff_t ProbeLabel(u32 pc) const { return instruction_labels[pc].offset(); }
    void ProbeReplace(std::ptrdiff_t start, const std::vector<u32>& words) {
        code_mem->unprotect();
        std::copy(words.begin(), words.end(), code_mem->ptr() + start/4);
        code_mem->protect(); code_mem->invalidate_all();
    }
'''
    adapted_header = replace_once(header, 'private:\n    std::vector<u32> code_vec;',
        observation+'\nprivate:\n    std::vector<u32> probe_code;\n'
        '    std::vector<std::array<std::ptrdiff_t, 3>> probe_ranges;\n    std::vector<u32> code_vec;')
    range_guard = '''
    // CodexAstraLocal: Record only emission offsets; identical in both variants.
    struct ProbeRange {
        std::vector<std::array<std::ptrdiff_t, 3>>& ranges;
        oaknut::VectorCodeGenerator& gen;
        std::ptrdiff_t begin, pc;
        ~ProbeRange() { ranges.push_back({pc, begin, gen.offset()}); }
    } probe{probe_ranges, *this, offset(), program_counter - 1};
'''
    variants = []
    for name, text in (('Baseline', original), ('Candidate', candidate)):
        body = replace_once(text,
            'void JitShader::Compile_DestEnable(Instruction instr, QReg src) {\n',
            'void JitShader::Compile_DestEnable(Instruction instr, QReg src) {\n'+range_guard)
        body = replace_once(body, '    const size_t code_size = code_vec.size() * sizeof(u32);',
                            '    probe_code = code_vec;\n    const size_t code_size = code_vec.size() * sizeof(u32);')
        body = replace_once(body, '#include "video_core/shader/shader_jit_a64_compiler.h"',
                            '#include "'+name+'.h"')
        body = body.replace('namespace Pica::Shader {', 'namespace Pica::Shader::'+name+' {')
        h = adapted_header.replace('namespace Pica::Shader {', 'namespace Pica::Shader::'+name+' {')
        (out/(name+'.cpp')).write_text(body)
        (out/(name+'.h')).write_text(h)
        variants.append(out/(name+'.cpp'))
    # CodexAstraLocal: Reuse the exact existing sequential ABI bridge and local
    # log/assert endpoints. The test never calls that bridge concurrently.
    for source, name in ((Path(__file__), 'run.py'), (Path(__file__).with_suffix('.cpp'), 'fixture.cpp')):
        shutil.copyfile(source, out/name)
    assembly = repo/'tools/uberhar/test_shader_a64_return.S'
    shutil.copyfile(assembly, out/'bridge.S')
    stubs = out/'stubs'
    for relative, contents in {
        'common/assert.h': '#pragma once\n#include <cstdlib>\n#include "common/common_types.h"\n#define ASSERT(x) ((x)?(void)0:std::abort())\n#define ASSERT_MSG(x,...) ASSERT(x)\n#define DEBUG_ASSERT(x) ASSERT(x)\n#define DEBUG_ASSERT_MSG(x,...) ASSERT(x)\n#define UNREACHABLE() std::abort()\n#define UNREACHABLE_MSG(...) std::abort()\n',
        'common/logging/log.h': '#pragma once\n#include <cstdlib>\n#define LOG_CRITICAL(...) std::abort()\n#define LOG_ERROR(...) std::abort()\n#define LOG_WARNING(...) std::abort()\n#define LOG_INFO(...) ((void)0)\n#define LOG_DEBUG(...) ((void)0)\n#define LOG_TRACE(...) ((void)0)\n',
    }.items():
        path = stubs/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('// CodexAstraLocal: Unexpected fixture failures abort; no services linked.\n'+contents)
    # CodexAstraLocal: A zero-based vector emitter cannot relocate ADRP to a
    # low-address helper after copying. Keep this private static ELF above that
    # range, so the unchanged MOVP2R emits absolute pointers as on high mappings.
    # The preserved low-base run faults in baseline EMIT; no production fix or
    # claim about the actual Thor library placement is made here.
    common = [str(compiler), '--target=aarch64-linux-android33', '-std=c++20', '-O2', '-static',
              '-static-libstdc++', '-pthread', '-ffunction-sections', '-fdata-sections',
              '-Wl,--gc-sections', '-Wl,--image-base=0x1000000000', '-DMICROPROFILE_ENABLED=0', '-DFMT_HEADER_ONLY',
              '-I'+str(out), '-I'+str(stubs)]
    for directory in ('src', 'externals/oaknut/include', 'externals/nihstro/include',
                      'externals/boost', 'externals/microprofile', 'externals/fmt/include', 'externals/xxHash'):
        common.append('-I'+str(repo/directory))
    sources = [out/'fixture.cpp', out/'bridge.S', *variants,
               repo/'src/video_core/pica/shader_unit.cpp', repo/'src/video_core/pica/shader_setup.cpp']
    command = common + [str(p) for p in sources] + ['-o', str(out/'fixture')]
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'compile_argv': command,
             'tool_sha256': {str(p): sha(p) for p in (compiler, qemu, objdump)},
             'source_sha256': {str(p): sha(p) for p in (compiler_cpp, compiler_h, assembly,
                 Path(__file__), Path(__file__).with_suffix('.cpp'))}, 'passed': False}
    def save():
        (out/'provenance.json').write_text(json.dumps(proof, indent=2)+'\n')
    save()
    dep = subprocess.run(command[:-2]+['-MM', '-MT', 'inputs'], capture_output=True, timeout=30)
    (out/'dependencies.mk').write_bytes(dep.stdout)
    (out/'dependencies.stderr').write_bytes(dep.stderr)
    require(dep.returncode == 0, 'dependency extraction failed')
    dependencies = set()
    for line in dep.stdout.decode().replace('\\\n', '').splitlines():
        require(line.startswith('inputs:'), 'unexpected dependency record')
        dependencies.update(Path(p) for p in shlex.split(line[len('inputs:'):]))
    proof['dependency_sha256'] = {str(p): sha(p) for p in sorted(dependencies)}
    save()
    started = time.monotonic()
    with (out/'compile.log').open('wb') as log:
        compiled = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    proof.update(compile_exit=compiled.returncode, compile_wall_seconds=time.monotonic()-started)
    save()
    require(compiled.returncode == 0, 'A64 writeback fixture failed to compile')
    # CodexAstraLocal: Verify the same public tail-call seam as the durable gate;
    # instrumentation must not hide ABI corruption behind a wrapper's own frame.
    dis = subprocess.run([str(objdump), '-d', '--no-show-raw-insn',
        '--disassemble-symbols=BaselineRun,CandidateRun,BaselineBound,CandidateBound', str(out/'fixture')],
        capture_output=True, timeout=10, check=True)
    (out/'public-entries.asm').write_bytes(dis.stdout)
    for name in ('BaselineRun', 'CandidateRun', 'BaselineBound', 'CandidateBound'):
        # CodexAstraLocal: High virtual addresses consume objdump's indentation;
        # delimit symbols explicitly rather than mistaking instructions for one.
        match = re.search(r'<'+name+r'>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)', dis.stdout.decode(), re.S)
        require(match is not None, 'missing entry wrapper')
        ins = re.findall(r'^\s*[0-9a-f]+:\s+([^\n]+)', match[1], re.M)
        end = next((i for i, v in enumerate(ins) if re.match(r'br\s+x\d+\b', v)), None)
        require(end is not None, 'wrapper lacks tail branch')
        require(not any(re.search(r'\b(?:sp|x19|w19|x30|w30|lr)\b', v) or re.match(r'(bl|blr|ret)\b', v)
                        for v in ins[:end+1]), 'wrapper altered seeded ABI')
    run = [str(qemu), str(out/'fixture'), str(out)]
    executed = subprocess.run(run, capture_output=True, timeout=45, preexec_fn=no_core)
    (out/'stdout.jsonl').write_bytes(executed.stdout)
    (out/'stderr.log').write_bytes(executed.stderr)
    proof.update(run_argv=run, run_exit=executed.returncode, binary_sha256=sha(out/'fixture'))
    save()
    require(executed.returncode == 0 and not executed.stderr, 'A64 state/ABI/control proof failed')
    # CodexAstraLocal: Assemble observed emitted words as a disassembly-only
    # object. This uses neither a hand-decoded opcode model nor regenerated JIT.
    code_command = [str(compiler), '--target=aarch64-linux-android33', '-c',
                    str(out/'emitted.S'), '-o', str(out/'emitted.o')]
    subprocess.run(code_command, capture_output=True, timeout=30, check=True)
    emitted = subprocess.run([str(objdump), '-d', '--no-show-raw-insn', str(out/'emitted.o')],
                             capture_output=True, timeout=20, check=True)
    (out/'emitted.asm').write_bytes(emitted.stdout)
    proof['emitted_compile_argv'] = code_command
    proof['result'] = json.loads(executed.stdout.splitlines()[-1])
    require(proof['result']['passed'] is True, 'missing final proof verdict')
    # CodexAstraLocal: Missing defect controls or a shortened fixture must fail
    # even if its remaining state comparisons happen to agree.
    for field, expected in dict(programs=480, paired_states=29568, executions=59144,
                                defects=4, masks=16, fpcr_modes=6, raw_rotations=16).items():
        require(proof['result'].get(field) == expected, 'incomplete fixture: '+field)
    proof['independent_artifact_check'] = inspect_artifacts(out)
    for name, digest in {**proof['source_sha256'], **proof['dependency_sha256']}.items():
        require(sha(Path(name)) == digest, 'input changed during proof: '+name)
    proof['artifact_sha256'] = {str(p.relative_to(out)): sha(p) for p in sorted(out.rglob('*'))
                               if p.is_file() and p.name != 'provenance.json'}
    proof['passed'] = True
    save()
    print(json.dumps({'passed': True, 'proof': str(out/'provenance.json'), 'result': proof['result']}))


if __name__ == '__main__':
    main()
