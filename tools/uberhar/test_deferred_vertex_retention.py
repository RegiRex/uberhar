#!/usr/bin/env python3
"""CodexAstraLocal: Bind a finite no-target differential to actual repository source.

The oracle uses current scalar AddTriangle and the generic null-packet branch. The
candidate executes the deferred writer, admission and retained-batch branch. Targets,
GPU recording and executor publication storage are explicit adapters; no Vulkan
device, title, JIT execution or performance result is claimed by this control.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(b,why):
    if not b:raise RuntimeError(why)

def main():
    global ROOT
    # CodexAstraLocal: A source shadow supports finite defects/private review;
    # normal CI extracts actual repository sources and writes a new proof folder.
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=ROOT)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--output',type=Path,default=Path('build/uberhar-probe/deferred-vertex-retention'))
    args=parser.parse_args()
    ROOT=args.repo.resolve()
    source_root=args.source_root.resolve()
    args.output.mkdir(parents=True,exist_ok=True)
    output=Path(tempfile.mkdtemp(prefix='retention-',dir=args.output.resolve()))
    proof={'author':'CodexAstraLocal','scope':__doc__,'inputs':{},'seams':{},'variants':[]}
    def read(p):
        p=p.resolve();rel=str(p);data=p.read_bytes()
        proof['inputs'][rel]=hashlib.sha256(data).hexdigest()
        copy=output/'sources'/('input-'+str(len(proof['inputs']))+'-'+p.name);copy.parent.mkdir(parents=True,exist_ok=True);copy.write_bytes(data)
        return data.decode()
    helper=ROOT/'tools/uberhar/test_ready_cpu_fragments.py';read(helper)
    spec=importlib.util.spec_from_file_location('extractor',helper)
    extractor=importlib.util.module_from_spec(spec);spec.loader.exec_module(extractor)
    def func(source,signature):
        body=extractor.function(source,signature)
        proof['seams'][signature]=hashlib.sha256(body.encode()).hexdigest()
        return body
    try:
        current=read(source_root/'src/video_core/rasterizer_accelerated.cpp')
        header=read(source_root/'src/video_core/rasterizer_accelerated.h')
        candidate=read(source_root/'src/video_core/renderer_vulkan/vk_rasterizer.cpp')
        packet=read(source_root/'src/video_core/pica/uberhar_cpu_draw_queue.cpp')
        read(source_root/'src/video_core/pica/uberhar_cpu_draw_queue.h')
        read(ROOT/'src/video_core/renderer_vulkan/uberhar_compute_rect.h')
        fixture=read(HERE/'test_deferred_vertex_retention.cpp');read(Path(__file__).resolve())
        # CodexAstraLocal: Original scalar AddTriangle is the transport oracle;
        # a null packet executes the actual current generic no-target branch.
        common=['RasterizerAccelerated::HardwareVertex::HardwareVertex(',
                'static bool AreQuaternionsOpposite(',
                'void RasterizerAccelerated::AddTriangle(']
        bodies=[]
        for sig in common:
            bodies.append(func(current,sig))
        writer=func(current,'void RasterizerAccelerated::WriteDeferredTriangles(')
        bodies.append(writer)
        draw=func(candidate,'bool RasterizerVulkan::Draw(')
        branch=func(draw,'    if (!framebuffer->Handle())')
        original=branch
        retain=extractor.function(branch,'        if (deferred)')
        generated={
            'hardware-type.inc':func(header,'    struct HardwareVertex {')+';',
            'hardware-functions.inc':'\n'.join(bodies),
            'packet-functions.inc':'\n'.join(func(packet,s) for s in
                ['void CpuDrawPacket::Wait() const','std::span<const u8> CpuDrawPacket::HardwareBytes() const',
                 'u32 CpuDrawPacket::VertexCount() const noexcept']),
            'preflight.inc':func(candidate,'RasterizerVulkan::DeferredHardwareWriter RasterizerVulkan::PrepareDeferredVertices('),
            'original-no-target.inc':original,
            'candidate-no-target.inc':branch,
        }
        # CodexAstraLocal: Old missing retention must fail the first full byte
        # comparison. A separate wrong-quaternion defect challenges the converter
        # rather than relying only on control-flow instrumentation.
        variants={
            'candidate':generated,
            'old_missing_retention':dict(generated,**{'candidate-no-target.inc':branch.replace(retain,'',1)}),
            'wrong_quaternion':dict(generated,**{'hardware-functions.inc':'\n'.join(bodies[:-1])+ '\n'+
                writer.replace('HardwareVertex{b, AreQuaternionsOpposite(a.quat, b.quat)}','HardwareVertex{b, false}',1)}),
        }
        need(variants['wrong_quaternion']!=generated,'Quaternion defect anchor drift')
        for name,seams in variants.items():
            directory=output/name;directory.mkdir()
            for filename,body in seams.items():(directory/filename).write_text(body+'\n')
            (directory/'fixture.cpp').write_text(fixture)
            binary=directory/'test';dep=directory/'fixture.d'
            argv=shlex.split(os.environ.get('CXX','c++'))+[
                '-std=c++20','-O2','-DNDEBUG','-DFMT_HEADER_ONLY','-fno-fast-math','-ffp-contract=off','-pthread',
                '-I'+str(source_root/'src'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'externals/fmt/include'),
                '-I'+str(ROOT/'externals/boost'),'-I'+str(ROOT/'externals/nihstro/include'),
                '-MMD','-MF',str(dep),str(directory/'fixture.cpp'),'-o',str(binary)]
            record={'name':name,'compile_argv':argv};proof['variants'].append(record)
            built=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=90)
            (directory/'build.stdout').write_bytes(built.stdout);(directory/'build.stderr').write_bytes(built.stderr)
            record['compile_exit']=built.returncode;need(built.returncode==0,'Compile failed: '+str(directory))
            result=subprocess.run([str(binary)],cwd=ROOT,capture_output=True,timeout=20)
            (directory/'run.stdout').write_bytes(result.stdout);(directory/'run.stderr').write_bytes(result.stderr)
            record['run_exit']=result.returncode
            if name=='candidate':
                need(result.returncode==0,'Candidate failed: '+str(directory));record['result']=json.loads(result.stdout)
                need(record['result']['scenarios']==144,'Population drift')
            else:
                need(result.returncode==1 and result.stderr==b'FAIL no-target retained original 88B bytes\n',
                     'Defect did not expose intended byte mismatch: '+name)
            text=dep.read_text().replace('\\\n',' ').split(':',1)[1]
            record['dependencies']={str(Path(p).resolve()):sha(Path(p)) for p in shlex.split(text)}
            print(name+': PASS',flush=True)
        proof['passed']=True
    finally:
        proof['inputs_after']={rel:sha(ROOT/rel) for rel in proof['inputs']}
        proof['unchanged']=proof['inputs']==proof['inputs_after']
        proof['artifacts']={str(p.relative_to(output)):sha(p) for p in output.rglob('*') if p.is_file()}
        (output/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
        print('Proof: '+str(output),flush=True)
    need(proof['unchanged'],'Source changed during finite control')

if __name__=='__main__':main()
