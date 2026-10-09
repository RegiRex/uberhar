#!/usr/bin/env python3
"""CodexAstraLocal: Build finite actual-input compute rectangle regression data.

Run from the repository root. HardwareVertex/layout and queued command bodies
are source-derived. Resource/command endpoints record arguments, not Vulkan work.
Synthetic shader rendering is optional and never claims title or device parity.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path.cwd().resolve()
HERE=Path(__file__).resolve().parent
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def checked(command,log,timeout=180):
    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=timeout)
    log.write_text(result.stdout+result.stderr);result.check_returncode()
    if result.stdout:print(result.stdout,end='',flush=True)
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=ROOT)
    parser.add_argument('--output',type=Path,default=ROOT/'build/uberhar-probe/compute-rect-pixels')
    parser.add_argument('--render',action='store_true')
    parser.add_argument('--require-vulkan',action='store_true')
    args=parser.parse_args()
    if args.require_vulkan and not args.render:parser.error('--require-vulkan needs --render')
    source=args.source_dir.resolve();parent=args.output.resolve();parent.mkdir(parents=True,exist_ok=True)
    out=Path(tempfile.mkdtemp(prefix='fixtures-',dir=parent))
    def selected(relative):
        candidate=source/relative
        return candidate if candidate.is_file() else ROOT/relative
    # CodexAstraLocal: Reuse actual CPU-ABI source extraction only, without
    # compiling or rendering its separate broad fragment corpus.
    abi_path=ROOT/'tools/uberhar/test_cpu_fragment_abi.py'
    abi=load('compute_actual_abi',abi_path)
    hashes=abi.prepare(out)
    fixture=HERE/'test_compute_rect_pixels.cpp'
    (out/'compute-probe.cpp').write_bytes(fixture.read_bytes())
    relative='src/video_core/renderer_vulkan/uberhar_compute_rect.h'
    header=selected(relative);code=header.read_text()
    shader=selected('src/video_core/renderer_vulkan/uberhar_compute_rect_shader.h')
    (out/'compute.comp').write_text(shader.read_text().split('R"glsl(',1)[1].split(')glsl"',1)[0])
    blend_guard='''        if (!replace)
            return {raw, 0, true};'''
    geometry_guard='''    if (state.exact_geometry && !ComputeRectSupport::StrictGeometry(
            vertices, viewport, regs.framebuffer.framebuffer.IsFlipped()))
        return {};'''
    if code.count(blend_guard)!=1 or code.count(geometry_guard)!=1:
        raise AssertionError('Expected exact production admission guards')
    # CodexAstraLocal: Compile production guards and deliberate removals; blend
    # removal must change pixels, while geometry removals test conservative admission.
    variants=[('baseline',code,None),
              ('blend-mutant',code.replace(blend_guard,'        (void)replace;'),'IGNORE_BLEND_PROOF'),
              ('geometry-mutant',code.replace(geometry_guard,'    (void)state.exact_geometry;'),'IGNORE_STRICT_GEOMETRY')]
    flags=[os.environ.get('CXX','c++'),'-std=c++20','-O2','-DFMT_HEADER_ONLY','-DXXH_INLINE_ALL',
           '-ffunction-sections','-fdata-sections','-I'+str(source/'src'),'-Isrc',
           '-Iexternals/fmt/include','-Iexternals/boost','-Iexternals/xxHash',
           '-Iexternals/nihstro/include','-Iexternals/json','-Wl,--gc-sections']
    units=[selected(p) for p in ('src/video_core/shader/generator/glsl_shader_gen.cpp',
                                 'src/video_core/shader/generator/glsl_fs_shader_gen.cpp',
                                 'src/video_core/shader/generator/pica_fs_config.cpp')]
    extractor_path=ROOT/'tools/uberhar/test_compute_census.py'
    cpp=selected('src/video_core/renderer_vulkan/vk_compute_rect.cpp')
    command_fixture=HERE/'test_compute_rect_commands.cpp'
    # CodexAstraLocal: Freeze inputs before any compiler work; a source change
    # during the gate must fail instead of acquiring an unrelated final hash.
    paths=[header,shader,cpp,fixture,command_fixture,Path(__file__),
           HERE/'compare_compute_rect_pixels.py',abi_path,extractor_path,*units]
    hashes.update({str(p):digest(p) for p in paths})
    commands=[]
    for name,text,define in variants:
        include=out/(name+'-include/video_core/renderer_vulkan');include.mkdir(parents=True)
        (include/'uberhar_compute_rect.h').write_text(text)
        command=flags[:1]+['-I'+str(include.parents[1])]+flags[1:]
        if define:command+=['-D'+define]
        command+=[str(out/'compute-probe.cpp'),*[str(p) for p in units],'-o',str(out/(name+'-producer'))]
        commands.append(command);checked(command,out/(name+'-compile.log'))
        checked([str(out/(name+'-producer')),str(out/name)],out/(name+'-produce.log'))

    # CodexAstraLocal: Execute the exact Draw closure with one documented command
    # endpoint substitution, proving read access and immutable packet/handle data.
    extractor=load('compute_command_extract',extractor_path)
    # CodexAstraLocal: Keep the shared recorder and guest closure in the same
    # actual-source gate after the explicit scratch benchmark reuses commands.
    body=extractor.block(cpp.read_text(),'void RecordComputeRectCommands(')+'\n'+extractor.block(cpp.read_text(),'void ComputeRectRenderer::Draw(')
    if body.count('vk::CommandBuffer cmdbuf')!=2:raise AssertionError('Command boundary changed')
    body=body.replace('vk::CommandBuffer cmdbuf','RecordingCommands cmdbuf')
    needle='vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite'
    if body.count(needle)!=1 or body.count('packet](RecordingCommands cmdbuf)')!=1:
        raise AssertionError('Expected read access and immutable packet capture')
    command_cases=[('baseline',body,None),
        ('missing-read',body.replace(needle,'vk::AccessFlags{vk::AccessFlagBits::eShaderWrite}'),
         'partial mask read dependency'),
        ('borrowed-packet',body.replace('packet](RecordingCommands cmdbuf)','&packet](RecordingCommands cmdbuf)'),
         'queued packet is immutable')]
    rows=[]
    for name,text,expected in command_cases:
        d=out/('commands-'+name);d.mkdir();(d/'draw.inc').write_text(text+'\n')
        command=flags[:1]+['-I'+str(d)]+flags[1:]+[str(command_fixture),'-o',str(d/'test')]
        commands.append(command);checked(command,d/'compile.log')
        ran=subprocess.run([str(d/'test')],cwd=ROOT,capture_output=True,text=True,timeout=20)
        (d/'execute.log').write_text(ran.stdout+ran.stderr)
        passed=ran.returncode==0 if expected is None else ran.returncode==1 and 'FAILED: '+expected in ran.stderr
        if not passed:raise AssertionError((name,ran.returncode,ran.stdout,ran.stderr))
        rows.append({'name':name,'returncode':ran.returncode,'expected':expected,'output':ran.stdout+ran.stderr})

    if any(digest(ROOT/p)!=h for p,h in hashes.items()):raise AssertionError('Source changed during proof')
    manifest={'author':'CodexAstraLocal','source_sha256':hashes,'compile_commands':commands,
        'command_cases':rows,'artifact_sha256':{str(p.relative_to(out)):digest(p) for p in out.rglob('*') if p.is_file()},
        'scope':'Actual packet helper/CPU ABI/shaders; recording queued Vulkan arguments; synthetic only'}
    (out/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # CodexAstraLocal: Rendering reuses exactly these compiled fixtures if source
    # and payload hashes still match, avoiding a second release-gate compilation.
    latest=out/'latest.json';latest.write_text(json.dumps({'directory':str(out)})+'\n')
    os.replace(latest,parent/'latest.json')
    print('PASS compute rectangle fixtures and queued commands: '+str(out),flush=True)
    if args.render:
        command=[sys.executable,str(HERE/'compare_compute_rect_pixels.py'),str(out)]
        if args.require_vulkan:command+=['--require-vulkan']
        checked(command,out/'render.log',timeout=300)
if __name__=='__main__':main()
