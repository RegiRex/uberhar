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
    # CodexAstraLocal: Local changed-behavior check; CI retains the full gate.
    parser.add_argument('--reset-only', action='store_true')
    # CodexAstraLocal: Focus new grouping/publication/ready-prefix behavior in
    # local checks; the default CI invocation still runs every older control.
    parser.add_argument('--groups-only', action='store_true')
    # CodexAstraLocal: One local .50 run covers changed seed/token/group guards;
    # CI's default still retains the full existing output/failure campaign.
    parser.add_argument('--features-only', action='store_true')
    # CodexAstraLocal: Resume an interrupted local gate without repeating passed
    # controls; default CI still checks the full positive population and defects.
    parser.add_argument('--start-variant')
    parser.add_argument('--positive-scope', choices=('all', 'work'), default='all')
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
               'video_core/pica/uberhar_parallel_vertex.h',
               'video_core/pica/primitive_assembly.h',
               'common/uberhar_parallel_work.h', 'common/uberhar_cpu_work_budget.h']
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
    # CodexAstraLocal: Exercise the actual owner reset sites and reconciliation,
    # without booting an unrelated guest system. Only the pre-drain worker gate
    # is injected, so a broken blocking branch produces an immediate diagnostic.
    pica_path = source_root / 'video_core/pica/pica_core.cpp'
    inputs.append(pica_path)
    pica = pica_path.read_text()
    def reset_branch(register, next_register):
        start = pica.index('    case PICA_REG_INDEX(pipeline.' + register + '):')
        end = pica.index('    case PICA_REG_INDEX(pipeline.' + next_register + '):', start)
        branch = pica[start:end].split('\n', 1)[1]
        return change(branch, '        break;', '')
    topology = reset_branch('triangle_topology', 'restart_primitive')
    restart = reset_branch('restart_primitive', 'vs_default_attributes_setup.index')
    reconcile = body(pica, 'void PicaCore::ReconcileDeferredVertices(bool wait)').replace('PicaCore::', 'ResetOwner::')
    reconcile = change(reconcile, '        state.executor.Drain();', '        Gate(true);\n        state.executor.Drain();')
    owner = '''// CodexAstraLocal: Source-extracted owner operations, real queue and assembler.
struct ResetOwner {
    struct DeferredState {
        CpuDrawExecutor executor{4, 64, 8 * 1024 * 1024, Start};
        std::vector<std::shared_ptr<CpuDrawPacket>> pending;
        std::size_t reconciled{};
        u64 synchronous_boundaries{}, completed{}, inputs{}, invocations{}, hits{};
        u64 deferred_topology_resets{}, deferred_primitive_resets{};
        u64 ready_prefix_packets{};
    };
    std::unique_ptr<DeferredState> deferred_vertices = std::make_unique<DeferredState>();
    PrimitiveAssembler primitive_assembler;
    struct { struct { struct { PipelineRegs::TriangleTopology triangle_topology; } pipeline; } internal; } regs;
    void ReconcileDeferredVertices(bool wait = true);
    void Topology(PipelineRegs::TriangleTopology next) {
        regs.internal.pipeline.triangle_topology = next;
''' + topology + '''
    }
    void Restart() {
''' + restart + '''
    }
};
''' + reconcile + '\n'
    owner_path = source / 'actual_reset_owner.h'
    owner_path.write_text(owner)
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
    instrumented = change(instrumented, '                const auto result = pool->Run(',
        '                PacketProbe::BeforeWave();\n                const auto result = pool->Run(')
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
        # CodexAstraLocal: The conservative standalone floor now uses FIFO misses
        # directly; challenge that decision rather than an obsolete expansion cost.
        ('input_count_as_misses',change(instrumented,
            'const bool standalone_seed = misses >= 96;',
            'const bool standalone_seed = source.count >= 96;'),
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
    # CodexAstraLocal: These defects challenge real admission/publication and
    # completed-prefix behavior, with actual JIT bytes rather than mock results.
    grouped_variants = [
        ('missing_pending_seed', change(instrumented,
            'return policy.broader_groups && asynchronous && !stopping && !terminal &&\n'
            '               pending_seed && !pending.empty() && Partition(pending, work);',
            'return work >= MemberWorkUnit;'), header,
            'groups', 'unseeded small admission'),
        ('missing_member_charge', change(instrumented,
            'work >= GroupWorkUnit + MemberWorkUnit * (members - 1)',
            'work >= GroupWorkUnit'), header,
            'groups', 'member charge omitted'),
        # CodexAstraLocal: A large seed cannot sponsor an oversized cheap tail.
        ('missing_group_bound', change(instrumented,
            'MaxGroupMembers = 4, MaxPackets = 64', 'MaxGroupMembers = 8, MaxPackets = 64'),
            header, 'groups', 'oversized grain admitted'),
        ('missing_submit_recheck', change(instrumented,
            'if (packet->impl->requires_group && !impl->PendingGroupFits(packet->impl->guaranteed_work))',
            'if (false)'), header, 'groups', 'lost seed published small packet'),
        ('unweighted_tiny_claims', change(instrumented,
            'prior[end] = static_cast<int>(end - members); break;',
            'prior[end] = static_cast<int>(end - 1); break;'), header,
            'groups', 'weighted grain accounting'),
        ('group_copy_limit', change(instrumented,
            'if (raw_size > u64(misses) * 70)', 'if (raw_size > u64(misses) * 700)'), header,
            'groups', 'group copy bound escaped'),
        ('missing_ready_error', change(instrumented,
            'if (impl->error)\n        throw VideoCore::ShaderRecoveryError{"Uberhar deferred CPU draw failed"};\n    result =',
            'if (false)\n        throw VideoCore::ShaderRecoveryError{"Uberhar deferred CPU draw failed"};\n    result ='),
            header, 'groups', 'ready error skipped or replayed'),
        ('missing_progress_reservation', change(instrumented,
            'if (!*progress_lane)', 'if (false)'), header,
            'groups', 'no-lane publication failed synchronous progress'),
    ]
    if args.reset_only:
        variants = [('positive', instrumented, header, 'resets', None)]
    elif args.groups_only:
        variants = [('positive', instrumented, header, 'groups', None)] + grouped_variants
    else:
        variants += grouped_variants
    # CodexAstraLocal: Actual policy and token defects must fail their distinct
    # assertions; positive scope compares real JIT/FIFO/88-byte output too.
    feature_variants = [
        ('ignored_seed_control', change(instrumented,
            'const bool standalone_seed = misses >= 96;',
            'const bool standalone_seed = guaranteed_work >= GroupWorkUnit;'),
            header, 'work', 'conservative seed admitted93'),
        ('stale_accepted_token', change(instrumented,
            'Impl::Matches(candidate, source.shader_lease, booleans)', 'true'),
            header, 'groups', 'stale Boolean token reused'),
    ]
    variants += feature_variants
    if args.features_only:
        names = {'positive', 'missing_arithmetic_floor', 'input_count_as_misses',
                 'missing_copy_floor', 'missing_pending_seed', 'missing_member_charge',
                 'missing_group_bound', 'missing_submit_recheck', 'missing_progress_reservation',
                 'ignored_seed_control', 'stale_accepted_token'}
        variants = [v for v in variants if v[0] in names]
        variants[0] = ('positive', instrumented, header, 'features', None)
    if args.positive_scope != 'all':
        variants[0] = ('positive', instrumented, header, args.positive_scope, None)
    if args.start_variant:
        names = [variant[0] for variant in variants]
        if args.start_variant not in names[1:]:
            raise ValueError('Unknown negative-control resume point: ' + args.start_variant)
        variants = variants[:1] + variants[names.index(args.start_variant):]
    proof['selection'] = {'positive_scope': variants[0][3], 'start_variant': args.start_variant}
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
                # CodexAstraLocal: The focused .50 population has11actual byte
                # comparisons. Full/normal coverage retains its larger minimum;
                # a new selection must not masquerade as that complete corpus.
                minimum = (11 if selected == 'features' else 0 if selected == 'work'
                           else 4 if args.reset_only else 9 if args.groups_only
                           else (1024 if args.a64 else 512))
                if result.get('passed') is not True or result.get('comparisons',0) < minimum or result.get('checks',0) == 0:
                    raise RuntimeError('Incomplete positive population')
                proof['result'] = result
            elif result.get('passed') is not False or result.get('error') != expected:
                raise RuntimeError('Wrong negative-control failure: '+str(result))
            proof['variants'].append({'name':name,'source_sha256':sha(cpp),'header_sha256':sha(header_path),
                                      'binary_sha256':sha(binary),'result':result})
            print(name+': '+str(result),flush=True)
        # CodexAstraLocal: Two targeted controls prove the new regression detects
        # a restored redundant drain and failure to retain the serialized tail.
        header_path.write_text(header)
        queue_cpp = out / 'reset-queue.cpp'; queue_cpp.write_text(instrumented)
        queue_obj = compile_one(queue_cpp, 'reset-queue')
        controls = [
            ('redundant_reset_drain', change(owner, '    void Restart() {',
                '    void Restart() {\n        ReconcileDeferredVertices();'),
                'redundant reset drained packets'),
            ('missing_reset_pair', change(owner,
                '        primitive_assembler.AdoptCompletedTriangleTail(result.last_pair);', ''),
                'reset saved pair differs'),
        ]
        if not args.reset_only:
            controls.append(('skip_nonready_prefix', change(owner,
                'else if (!packet->TryCompletedResult(result))\n            break;',
                'else if (!packet->TryCompletedResult(result))\n            continue;'),
                'nonready predecessor skipped'))
        for name, mutated_owner, expected in controls:
            owner_path.write_text(mutated_owner)
            (out / (name + '-owner.h')).write_text(mutated_owner)
            fixture_obj = compile_one(source / 'fixture.cpp', name + '-fixture')
            binary = out / name
            run(name + '-link', flags + [fixture_obj, queue_obj, *common, *link_flags, '-o', binary])
            selected = 'groups' if name == 'skip_nonready_prefix' else 'resets'
            result = json.loads(run(name + '-run', [*prefix, binary, selected], 1).stdout)
            if result.get('passed') is not False or result.get('error') != expected:
                raise RuntimeError('Wrong reset negative-control failure: ' + str(result))
            proof['variants'].append({'name': name, 'owner_sha256': sha(owner_path),
                                     'binary_sha256': sha(binary), 'result': result})
            print(name + ': ' + str(result), flush=True)
        proof['passed'] = True
    finally:
        header_path.write_text(header)
        owner_path.write_text(owner)
        if str(owner_path.resolve()) in proof['dependencies']:
            proof['dependencies'][str(owner_path.resolve())] = sha(owner_path)
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
    print(json.dumps({'out':str(out),'passed':True,'result':proof['result'],'defects':len(proof['variants'])-1}))


if __name__ == '__main__':
    main()
