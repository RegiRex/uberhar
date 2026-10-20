#!/usr/bin/env python3
"""CodexAstraLocal: Finite actual scratch-owner regression and original-input fixtures.

The owner methods/command closures are extracted verbatim except the documented
command endpoint. Vulkan preparation, clocks, filesystem failures and scheduling
are boundary adapters. Optional pixels execute actual shaders on host GL only.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path.cwd().resolve()
HERE=Path(__file__).resolve().parent
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
def run(command,log,timeout=120):
    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
    log.write_text(result.stdout+result.stderr)
    return result

def check_runtime_compiler(out, text, block, command, objects):
    # CodexAstraLocal: Prepare's Vulkan objects remain modeled, but its shader
    # generation and exact CompileGLSL statement now run through the pinned real
    # parser. The .33 macro defect escaped CLI validation, which removes defines.
    prepare=block(text,'void ComputeBenchmark::Impl::Prepare()')
    first=prepare.index('    if (!ComputeBenchmarkData::Prepare(0, workload))')
    last=prepare.index('    for (u32 i = 0; i < modules.size(); ++i)')
    (out/'benchmark-shader-preparation.inc').write_text(prepare[first:last])
    loop=block(prepare,'for (u32 i = 0; i < modules.size(); ++i)')
    calls=[line.strip() for line in loop.splitlines() if 'const auto words = CompileGLSL(' in line]
    assert len(calls)==1 and calls[0].endswith(', "", true);'),calls
    (out/'benchmark-compile-call.inc').write_text(calls[0]+'\n')
    bad=out/'original-preamble';bad.mkdir()
    (bad/'benchmark-compile-call.inc').write_text(
        calls[0].replace(', "", true)',', "#define VULKAN 1\\n", true)')+'\n')
    stubs=out/'compiler-stubs/common';stubs.mkdir(parents=True)
    (stubs/'settings.h').write_text('''// CodexAstraLocal: The explicit-policy overload must never consult the UI setting.
#pragma once
#include <stdexcept>
namespace Settings {
struct ForbiddenOptimizerSetting {
    bool GetValue() const { throw std::runtime_error("unexpected global optimizer read"); }
};
struct Values { ForbiddenOptimizerSetting disable_spirv_optimizer; };
inline Values values;
}
''')
    compiler_root=ROOT/'externals/glslang'
    revision=subprocess.check_output(['git','-C',str(compiler_root),'rev-parse','HEAD'],text=True).strip()
    expected=subprocess.check_output(['git','ls-files','--stage','externals/glslang'],cwd=ROOT,text=True).split()[1]
    assert revision==expected,'glslang checkout must match its recorded pin'
    subprocess.run(['git','-C',str(compiler_root),'diff','--quiet','HEAD'],check=True)
    build=ROOT/'build/uberhar-probe/compute-benchmark-glslang'
    # CodexAstraLocal: This mandatory call disables optimization. The real
    # GlslangToSpv path therefore never enters SPIRV-Tools; build the same pinned
    # parser/emitter without that unused optimizer and validate modules separately.
    config=['cmake','-S',str(compiler_root),'-B',str(build),'-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DBUILD_EXTERNAL=OFF',
            '-DENABLE_OPT=OFF','-DENABLE_HLSL=OFF','-DENABLE_GLSLANG_BINARIES=OFF',
            '-DGLSLANG_TESTS=OFF','-DGLSLANG_ENABLE_INSTALL=OFF','-DENABLE_SPIRV=ON']
    build_command=['cmake','--build',str(build),'--target','glslang','-j2']
    run(config,out/'runtime-compiler-configure.log').check_returncode()
    run(build_command,out/'runtime-compiler-build.log',timeout=480).check_returncode()
    library=build/'glslang/libglslang.a'
    assert library.is_file()
    base=command[:1]+['-I'+str(stubs.parent),'-I'+str(compiler_root)]+command[1:]
    unit=ROOT/'src/video_core/renderer_vulkan/vk_shader_util.cpp'
    driver=HERE/'test_compute_benchmark_compiler.cpp'
    common=out/'runtime-compiler.o'
    argv=base+['-c',str(unit),'-o',str(common)]
    run(argv,out/'runtime-compiler-compile.log').check_returncode()
    commands=[config,build_command,argv];results=[]
    for name,include in [('current',out),('original-preamble',bad)]:
        binary=out/('runtime-compiler-'+name)
        argv=base[:1]+['-I'+str(include)]+base[1:]+[str(driver),str(common),*objects,
                                                 str(library),'-pthread','-o',str(binary)]
        run(argv,out/(name+'-compiler-link.log')).check_returncode();commands.append(argv)
        argv=[str(binary),str(out/(name+'-modules'))]
        result=run(argv,out/(name+'-compiler-run.log'),timeout=60);commands.append(argv)
        if name=='current':
            result.check_returncode()
        else:
            assert result.returncode==1 and 'Macro redefined' in result.stderr and 'VULKAN' in result.stderr
            assert 'FAILED: production compiler returned no SPIR-V 1.3 module' in result.stderr
        results.append({'case':name,'returncode':result.returncode,'output':result.stdout+result.stderr})
    # CodexAstraLocal: Validate the exact runtime-produced modules, not sources
    # recompiled by a CLI with a different preamble or environment setup.
    validator=shutil.which('spirv-val') or str(ROOT/'build/uberhar-validators/spirv-tools/tools/spirv-val')
    assert Path(validator).is_file(),'spirv-val required for actual runtime compiler modules'
    modules=sorted((out/'current-modules').glob('*.spv'))
    assert len(modules)==12
    for module in modules:
        argv=[validator,'--target-env','vulkan1.1',str(module)]
        run(argv,module.with_suffix('.validation.log')).check_returncode();commands.append(argv)
    return {'commands':commands,'cases':results,'glslang_revision':revision,
            'library_sha256':sha(library),'validator_sha256':sha(Path(validator)),
            'validated_modules':len(modules),
            'scope':'Actual CompileGLSL and benchmark source/call; mandatory optimizer disabled; no Vulkan device'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/uberhar-probe/compute-benchmark')
    parser.add_argument('--mutants',action='store_true')
    parser.add_argument('--render',action='store_true')
    parser.add_argument('--require-vulkan',action='store_true')
    parser.add_argument('--reuse-build',action='store_true')
    args=parser.parse_args()
    if args.require_vulkan and not args.render:parser.error('--require-vulkan requires --render')
    # CodexAstraLocal: The separate shader CI step reuses this exact emitted
    # population only after the comparator verifies every source/payload hash.
    if args.reuse_build:
        if not args.render or args.mutants:parser.error('--reuse-build requires --render without --mutants')
        directory=Path(json.loads((args.output/'latest.json').read_text())['directory'])
        argv=[sys.executable,str(HERE/'compare_compute_benchmark.py'),str(directory)]
        if args.require_vulkan:argv+=['--require-vulkan']
        subprocess.run(argv,cwd=ROOT,check=True,timeout=180)
        return
    args.output.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    extraction=load('benchmark_extract',HERE/'test_compute_census.py')
    block=extraction.block
    abi=load('benchmark_abi',HERE/'test_cpu_fragment_abi.py')
    hashes=abi.prepare(out)
    paths=[ROOT/'src/video_core/renderer_vulkan'/n for n in (
        'vk_compute_benchmark.cpp','vk_compute_benchmark.h','uberhar_compute_benchmark.h',
        'vk_compute_rect.cpp','vk_compute_rect.h','uberhar_compute_rect.h',
        'uberhar_compute_rect_shader.h','uberhar_push_constants.h',
        'vk_rasterizer.cpp','vk_pipeline_cache.h','vk_pipeline_cache.cpp','vk_scheduler.h')]
    paths += [HERE/'test_compute_benchmark.cpp',HERE/'compare_compute_benchmark.py',
              HERE/'test_compute_benchmark_compiler.cpp',
              ROOT/'src/video_core/renderer_vulkan/vk_shader_util.cpp',
              ROOT/'src/video_core/renderer_vulkan/vk_shader_util.h',
              HERE/'compare_fragment_state.py',Path(__file__),HERE/'test_compute_census.py',
              HERE/'test_cpu_fragment_abi.py',ROOT/'src/common/uberhar_test_profile.h',
              ROOT/'src/common/settings.h',ROOT/'src/video_core/shader/generator/profile.h']
    renderer=(ROOT/'src/video_core/renderer_vulkan/vk_rasterizer.cpp').read_text()
    cache=(ROOT/'src/video_core/renderer_vulkan/vk_pipeline_cache.h').read_text()
    text=paths[0].read_text()
    # CodexAstraLocal: Structural contracts complement extracted execution at
    # the real owner/callsite and actual resource-preparation boundary.
    assert 'pipeline_cache.ShaderProfile(), title' in renderer
    assert block(cache,'const Pica::Shader::Profile& ShaderProfile() const')=='const Pica::Shader::Profile& ShaderProfile() const {\n        return profile;\n    }'
    tick=block(renderer,'void RasterizerVulkan::TickFrame(')
    assert tick.index('scheduler.WaitWorker()')<tick.index('compute_benchmark->Poll(')
    assert 'StateFlags::Pipeline | StateFlags::DescriptorSets | StateFlags::FragmentConstants' in text
    assert 'Surface::Download' not in text and 'scheduler.Finish(' not in text and 'scheduler.Flush(' not in text
    assert 'instance.IsImageFormatListSupported()' in text and 'ImageViewUsageCreateInfo storage_usage' in text
    assert 'GLSL::GenerateTrivialVertexShader(profile.has_clip_planes, true)' in text
    assert '.logicOpEnable = !instance.NeedsLogicOpEmulation(), .logicOp = vk::LogicOp::eCopy' in text
    assert '.profile' not in block(text,'void ComputeBenchmark::Impl::RecordPair()')
    owner=block(text,'struct ComputeBenchmark::Impl {')
    fields=owner[owner.index('    const Instance&'):owner.index('    BufferOwner upload;')]
    declarations=owner[owner.index('    Impl('):owner.rindex('\n}')]
    (out/'owner-fields.inc').write_text(fields)
    (out/'owner-declarations.inc').write_text(declarations)
    settings=(ROOT/'src/common/settings.h').read_text()
    (out/'mode.inc').write_text(block(settings,'enum class UberharTestMode')+';\n')
    policy=(ROOT/'src/common/uberhar_test_profile.h').read_text()
    (out/'strict-policy.inc').write_text(block(policy,'constexpr bool RequiresComputeOnly(')+'\n')
    scheduler=(ROOT/'src/video_core/renderer_vulkan/vk_scheduler.h').read_text()
    (out/'state-flags.inc').write_text(block(scheduler,'enum class StateFlags')+';\n')
    # CodexAstraLocal: The real next graphics bind consumes the dirty flags;
    # equal cached state must still restore the scratch-overwritten values.
    pipeline=(ROOT/'src/video_core/renderer_vulkan/vk_pipeline_cache.cpp').read_text()
    restoration='\n'.join(block(pipeline,signature) for signature in (
        'if (dynamic.viewport != current_dynamic.viewport || is_dirty)',
        'if (dynamic.scissor != current_dynamic.scissor || is_dirty)',
        'if (is_dirty || bound_pipeline != selected)', 'if (constants_dirty)'))
    tail=pipeline[pipeline.index('if (is_dirty || bound_pipeline != selected)'):]
    tail=tail[:tail.index('\n    current_info = info;')]
    # CodexAstraLocal: Both generic and partial TEV consume the dynamic push
    # transport; execute the exact current restoration condition for each route.
    restoration+='\n'+block(tail,'if (selected_fallback || static_tev_cpu)')
    (out/'graphics-restore.inc').write_text(restoration+'\n')
    rect=(ROOT/'src/video_core/renderer_vulkan/vk_compute_rect.cpp').read_text()
    signatures=('void RequireVma(', 'void ImageBarrier(', 'bool WriteExclusive(',
                'void ComputeBenchmark::Impl::RecordPair()', 'bool ComputeBenchmark::Impl::Collect()',
                'void ComputeBenchmark::Impl::Report()', 'void ComputeBenchmark::Poll(',
                'void ComputeBenchmark::FinishAfterDrain()')
    bodies='\n\n'.join([block(rect,'void RecordComputeRectCommands(')]+[block(text,s) for s in signatures])
    assert bodies.count('vk::CommandBuffer')==3
    bodies=bodies.replace('vk::CommandBuffer','RecordingCommands')
    load_body=block(text,'std::unique_ptr<ComputeBenchmark> ComputeBenchmark::Load(')
    # CodexAstraLocal: Compile the exact Android branch and both unsupported
    # frontend branches in one TU; no preprocessor-only path escapes this gate.
    loads='#define ANDROID\n'+load_body+'\n#undef ANDROID\n'+load_body.replace('ComputeBenchmark::Load(','ComputeBenchmark::LoadUnsupported(')+'\n#define ANDROID\n#define HAVE_LIBRETRO\n'+load_body.replace('ComputeBenchmark::Load(','ComputeBenchmark::LoadLibretro(')+'\n#undef HAVE_LIBRETRO\n#undef ANDROID\n'
    (out/'owner-loads.inc').write_text(loads)
    (out/'owner-methods.inc').write_text(bodies)
    hashes.update({str(p.relative_to(ROOT)):sha(p) for p in paths})
    command=[os.environ.get('CXX','c++'),'-std=c++20','-O1','-DFMT_HEADER_ONLY','-DXXH_INLINE_ALL',
             '-ffunction-sections','-fdata-sections','-I'+str(out),'-Isrc','-Iexternals/fmt/include',
             '-Iexternals/boost','-Iexternals/xxHash','-Iexternals/nihstro/include',
             '-Iexternals/json','-Iexternals/vulkan-headers/include','-Wl,--gc-sections']
    units=['src/video_core/shader/generator/'+n for n in ('glsl_shader_gen.cpp','glsl_fs_shader_gen.cpp','pica_fs_config.cpp')]
    objects=[];commands=[]
    for i,unit in enumerate(units):
        target=out/f'generator-{i}.o';argv=command+['-c',unit,'-o',str(target)]
        r=run(argv,out/f'generator-{i}.log');commands.append(argv);r.check_returncode();objects.append(str(target))
    cases=[('baseline',bodies,None)]
    if args.mutants:
        # CodexAstraLocal: Each defect changes an actual admission/lifetime or
        # command condition, and must fail its independently specified assertion.
        changes=[
            ('completion-only','if (!submitted || !scheduler.IsFree(tick)) return false;','if (!scheduler.IsFree(tick)) return false;','GPU alone cannot prove accepted submission'),
            ('accepted-only','if (!submitted || !scheduler.IsFree(tick)) return false;','if (!submitted) return false;','accepted pair still needs GPU completion'),
            ('lost-device','if (result == vk::Result::eErrorDeviceLost)','if (false)','execution terminal error distinction'),
            ('pixel-ignored','if (graphics[offset] != expected || compute[offset] != expected)','if (false)','result exists'),
            ('partial-ratio','complete ? Json(durations[pair][1] / durations[pair][0]) : Json(nullptr)','Json(durations[pair][1] / durations[pair][0])','partial valid pair has no ratios'),
            ('missing-state','StateFlags::Pipeline | StateFlags::DescriptorSets | StateFlags::FragmentConstants','StateFlags::Pipeline | StateFlags::DescriptorSets','graphics state fully dirty'),
            ('short-compute','operation < OperationsPerRoute; ++operation)\n                    RecordComputeRectCommands','operation + 1 < OperationsPerRoute; ++operation)\n                    RecordComputeRectCommands','both complete route operation counts'),
            ('wrong-stage','command.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe','command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe','TOP BOTTOM timing stages'),
            ('borrowed-packets','readback_handles, packets,','readback_handles, &packets,','captured original packets'),
        ]
        for name,before,after,expected in changes:
            assert bodies.count(before)==1,(name,bodies.count(before))
            # Borrowing a local stack array would be UB; borrow the persistent
            # live workload instead so the test exposes lifetime drift safely.
            mutated=bodies.replace(before,after)
            if name=='borrowed-packets':
                mutated=bodies.replace('const auto packets = workload.packets;','const auto& packets = workload.packets;').replace(before,after)
            cases.append((name,mutated,expected))
    results=[]
    for name,body,expected in cases:
        directory=out/name;directory.mkdir()
        (directory/'owner-methods.inc').write_text(body)
        argv=command[:1]+['-I'+str(directory)]+command[1:]+[str(HERE/'test_compute_benchmark.cpp'),*objects,'-o',str(directory/'test')]
        result=run(argv,directory/'compile.log');commands.append(argv);result.check_returncode()
        result=run([str(directory/'test'),str(directory/'data')],directory/'execute.log',timeout=30)
        passed=result.returncode==0 if expected is None else result.returncode==1 and 'FAILED: '+expected in result.stderr
        if not passed:raise AssertionError((name,result.returncode,result.stdout,result.stderr))
        print(name+': '+(result.stdout or result.stderr).strip(),flush=True)
        results.append({'case':name,'returncode':result.returncode,'expected_failure':expected,'output':result.stdout+result.stderr})
    shader=ROOT/'src/video_core/renderer_vulkan/uberhar_compute_rect_shader.h'
    (out/'compute.comp').write_text(shader.read_text().split('R"glsl(',1)[1].split(')glsl"',1)[0])
    compiler_proof=check_runtime_compiler(out,text,block,command,objects)
    if any(sha(ROOT/p)!=h for p,h in hashes.items()):raise AssertionError('Source changed during gate')
    manifest={'author':'CodexAstraLocal','source_sha256':hashes,'commands':commands,'cases':results,
              'runtime_compiler':compiler_proof,
              'scope':'Actual owner/command bodies with named boundaries; actual CPU ABI/shader fixtures; no device execution',
              'artifacts':{str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()}}
    (out/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (args.output/'latest.json').write_text(json.dumps({'directory':str(out)})+'\n')
    if args.render:
        argv=[sys.executable,str(HERE/'compare_compute_benchmark.py'),str(out)]
        if args.require_vulkan:argv+=['--require-vulkan']
        result=run(argv,out/'render.log',timeout=180);result.check_returncode()
        print(result.stdout,end='',flush=True)
    print('PASS actual compute benchmark gate: '+str(out),flush=True)

if __name__=='__main__':main()
