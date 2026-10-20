#!/usr/bin/env python3
"""CodexAstraLocal: Execute the qualified output certificate with the real A64 JIT.

Synthetic shaders compare serial execution with actual worker grains, including
raw special values and all rounding/FZ/DN combinations. The actual input, FIFO,
output and assembler composition is also checked. This does not prove guest
FPSR isolation, memory ownership, renderer behavior or device performance; those
are independent prerequisites for the caller-qualified production route.
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


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def no_core():
    # CodexAstraLocal: Defects execute in bounded QEMU children and must report
    # an output mismatch; crashes, timeouts and leftover core files never pass.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def replace_once(source, old, new):
    require(source.count(old) == 1, 'certificate mutation seam changed: ' + old)
    return source.replace(old, new, 1)


def defects():
    # CodexAstraLocal: Every altered analysis rule has an independently authored
    # shader whose selected output actually depends on the removed safeguard.
    return (
        ('zero_initial_taint', 'observable_swizzle_future_carry',
         '    taint[first] = possible;', '    taint[first] = {};'),
        ('intersection_join', 'observable_join_one_defined',
         'else Union(taint[successor], state);', 'else Intersect(taint[successor], state);'),
        ('dp4_discards_W', 'observable_dot_2',
         'if (op == Op::Id::DP3 && lane == 3) continue;', 'if (lane == 3) continue;'),
        ('dph_ignores_source2_W', 'observable_dph_source2_W',
         '            reduced |= source[1][lane];',
         '            if (lane != 3) reduced |= source[1][lane];'),
        ('scalar_wrong_component', 'observable_scalar_14_X',
         'value.fill(source[0][0]);', 'value.fill(source[0][1]);'),
        ('wrong_output_mask', 'observable_changed_output_mask',
         'if ((output_register_mask & (1U << reg)) && carried)', 'if (reg == 0 && carried)'),
        ('condition_guard', 'observable_sensitive_carried_branch',
         'const u8 condition = node.access.reads.conditions & state.conditions;',
         'const u8 condition = 0;'),
        ('address_guard', 'observable_sensitive_carried_address',
         'const u8 address = node.access.reads.addresses & state.addresses;',
         'const u8 address = 0;'),
        ('alias_write_before_read', 'observable_alias_swap_lane_1',
         '    std::array<std::array<bool, 4>, 3> source{};',
         '''    // CodexAstraLocal: Intentional defect: erase the alias before reading it.
    const auto early = mad ? instruction.mad.dest.Value() : instruction.common.dest.Value();
    if (early.GetRegisterType() == nihstro::RegisterType::Temporary)
        for (u32 lane = 0; lane < 4; ++lane) if (swizzle.DestComponentEnabled(lane))
            state.temporary[early.GetIndex()] &= ~(1U << lane);
    std::array<std::array<bool, 4>, 3> source{};'''),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--output', type=Path,
                        default=Path('build/uberhar-probe/parallel-vertex-observable'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    # CodexAstraLocal: Reuse the installed CI/local NDK and QEMU. Missing tools
    # fail the gate; this script never downloads dependencies or operates a device.
    compiler = args.compiler
    if compiler is None:
        ndk = os.environ.get('ANDROID_NDK_HOME') or os.environ.get('ANDROID_NDK_ROOT')
        root = Path(ndk) if ndk else repo / 'build/native-deps/android-ndk-r27d'
        compiler = root / 'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++'
    compiler = compiler.absolute()  # Preserve clang++ rather than its clang symlink target.
    qemu = args.qemu or Path(shutil.which('qemu-aarch64-static') or
                              shutil.which('qemu-aarch64') or '')
    qemu = qemu.absolute()
    require(compiler.is_file() and qemu.is_file(), 'installed NDK/QEMU is required')
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    header = repo / 'src/video_core/pica/uberhar_parallel_vertex.h'
    fixture = Path(__file__).with_suffix('.cpp')
    cases_header = Path(__file__).with_name('test_parallel_vertex_observable_cases.h')
    production = [repo / name for name in (
        'src/video_core/shader/shader_jit_a64_compiler.cpp',
        'src/video_core/pica/shader_setup.cpp', 'src/video_core/pica/shader_unit.cpp',
        'src/video_core/pica/primitive_assembly.cpp')]
    sources = [Path(__file__).resolve(), fixture, cases_header, header, *production]
    original = header.read_text()
    proof = {'author': 'CodexAstraLocal', 'scope': __doc__, 'passed': False,
             'sources_before': {str(p): digest(p) for p in sources},
             'tools': {str(p): digest(p) for p in (compiler, qemu)},
             'commands': [], 'dependencies': {}, 'variants': []}
    for path in (fixture, cases_header, Path(__file__).resolve()):
        shutil.copyfile(path, out / path.name)

    def save():
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')

    def command(argv, label, directory, timeout):
        start = time.monotonic()
        run = subprocess.run(list(map(str, argv)), cwd=repo, capture_output=True,
                             timeout=timeout, preexec_fn=no_core)
        (directory / (label + '.stdout')).write_bytes(run.stdout)
        (directory / (label + '.stderr')).write_bytes(run.stderr)
        proof['commands'].append({'argv': list(map(str, argv)), 'label': label,
                                  'exit': run.returncode, 'seconds': time.monotonic() - start})
        save()
        return run

    # CodexAstraLocal: Only diagnostics/assert endpoints are isolated. Unexpected
    # warnings/errors abort; quiet informational logs require no emulator service.
    stubs = out / 'stubs'
    for relative, content in {
        'common/assert.h': '#pragma once\n#include <cstdlib>\n#include "common/common_types.h"\n'
            '#define ASSERT(x) ((x)?(void)0:std::abort())\n#define ASSERT_MSG(x,...) ASSERT(x)\n'
            '#define DEBUG_ASSERT(x) ASSERT(x)\n#define DEBUG_ASSERT_MSG(x,...) ASSERT(x)\n'
            '#define UNREACHABLE() std::abort()\n#define UNREACHABLE_MSG(...) std::abort()\n',
        'common/logging/log.h': '#pragma once\n#include <cstdlib>\n'
            '#define LOG_CRITICAL(...) std::abort()\n#define LOG_ERROR(...) std::abort()\n'
            '#define LOG_WARNING(...) std::abort()\n#define LOG_INFO(...) ((void)0)\n'
            '#define LOG_DEBUG(...) ((void)0)\n#define LOG_TRACE(...) ((void)0)\n',
    }.items():
        path = stubs / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('// CodexAstraLocal: Bounded fixture endpoints, no app services.\n' + content)
    # CodexAstraLocal: The unchanged vector emitter selects absolute helper
    # addresses for this high static image. This avoids the existing zero-based
    # ADRP relocation limitation of low-address static fixtures, without editing
    # compiler arithmetic, helper calls, executable allocation or public entries.
    flags = [str(compiler), '--target=aarch64-linux-android33', '-std=c++20', '-O2',
             '-static', '-static-libstdc++', '-pthread', '-ffunction-sections', '-fdata-sections',
             '-Wl,--gc-sections', '-Wl,--image-base=0x1000000000',
             '-DMICROPROFILE_ENABLED=0', '-DFMT_HEADER_ONLY']
    includes = ['-I' + str(stubs), '-I' + str(out)]
    for directory in ('src', 'externals/oaknut/include', 'externals/nihstro/include',
                      'externals/boost', 'externals/microprofile', 'externals/fmt/include',
                      'externals/xxHash', 'externals/json'):
        includes.append('-I' + str(repo / directory))

    def compile_one(source, output, prefix=(), defines=()):
        dependency = output.with_suffix('.d')
        run = command([*flags, *prefix, *includes, *defines, '-MMD', '-MF', dependency,
                       '-c', source, '-o', output], output.stem + '-compile', output.parent, 150)
        run.check_returncode()
        for name in shlex.split(dependency.read_text().replace('\\\n', ' ').split(':', 1)[1]):
            path = Path(name)
            if not path.is_absolute():
                path = repo / path
            proof['dependencies'].setdefault(str(path.resolve()), digest(path))
        save()

    try:
        # CodexAstraLocal: Build unmodified production objects once; each defect
        # recompiles only the fixture with one isolated certificate copy.
        objects = []
        for source in production:
            output = out / (source.stem + '.o')
            compile_one(source, output)
            objects.append(output)
        variants = [('baseline', None, None, None), *defects(),
                    ('wrong_composed_order', None, None, None)]
        exported = None
        for name, target, old, new in variants:
            directory = out / name
            local = directory / 'include/video_core/pica'
            local.mkdir(parents=True)
            modified = original if old is None else replace_once(original, old, new)
            (local / header.name).write_text(modified)
            defines = ['-DCOMPOSE_WRONG_ORDER'] if name == 'wrong_composed_order' else []
            output = directory / 'fixture.o'
            compile_one(out / fixture.name, output, ['-I' + str(directory / 'include')], defines)
            binary = directory / 'fixture'
            linked = command([*flags, output, *objects, '-o', binary], 'link', directory, 60)
            linked.check_returncode()
            if name == 'baseline':
                export = command([qemu, binary, '--export'], 'export', directory, 30)
                export.check_returncode()
                require(not export.stderr, 'unexpected synthetic export stderr')
                exported = json.loads(export.stdout)
                require(len(exported) == 107 and len({c['name'] for c in exported}) == 107 and
                        sum(c['expect_accept'] is True for c in exported) == 85,
                        'synthetic case population changed')
                selected = exported
            elif name == 'wrong_composed_order':
                selected = []
            else:
                selected = [c for c in exported if c['name'] == target]
                require(len(selected) == 1 and selected[0]['expect_accept'] is False and
                        selected[0]['safe_to_execute'] is True, 'invalid sensitive defect target')
            data = directory / 'cases.json'
            data.write_text(json.dumps(selected) + '\n')
            argv = [qemu, binary, data] + (['mutant'] if target else [])
            executed = command(argv, 'execute', directory, 240 if name == 'baseline' else 30)
            rows = [json.loads(line) for line in executed.stdout.splitlines()]
            require(not executed.stderr and bool(rows), 'unexpected execution output')
            final = rows[-1]
            if name == 'baseline':
                passed = executed.returncode == 0 and final.get('passed') is True and \
                    final['accepted'] == 85 and final['rejected'] == 22 and \
                    final['batches'] == 43520 and final['shader_calls'] == 2785280 and \
                    final['worker_items'] > 0 and final['status_different_batches'] > 0 and \
                    final['unused_final_state_different_batches'] > 0 and \
                    final['composed_draws'] == 168 and final['composed_invocations'] == 419544 and \
                    final['composed_hits'] == 84288 and final['composed_worker_invocations'] > 0
            else:
                expected = 'composed output bytes' if name == 'wrong_composed_order' else 'selected output mismatch'
                passed = executed.returncode == 1 and len(rows) == 1 and \
                    final.get('passed') is False and final.get('error') == expected
            proof['variants'].append({'name': name, 'passed': passed, 'result': final,
                                      'header_sha256': digest(local / header.name),
                                      'binary_sha256': digest(binary), 'data_sha256': digest(data)})
            save()
            require(passed, 'unexpected proof outcome: ' + name)
            print(name + ': ' + json.dumps(final), flush=True)
        proof['passed'] = True
    finally:
        # CodexAstraLocal: Detect concurrent source changes instead of assigning
        # passing execution to bytes that were not compiled in this exact run.
        proof['sources_after'] = {str(p): digest(p) for p in sources}
        proof['source_unchanged'] = proof['sources_before'] == proof['sources_after']
        proof['dependencies_unchanged'] = all(digest(p) == value for p, value in proof['dependencies'].items())
        proof['passed'] = proof['passed'] and proof['source_unchanged'] and proof['dependencies_unchanged']
        save()
        print('Proof: ' + str(out), flush=True)
    require(proof['passed'], 'source changed during the proof')


if __name__ == '__main__':
    main()
