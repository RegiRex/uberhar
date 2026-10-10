#!/usr/bin/env python3
"""CodexAstraLocal: Execute real accelerated enqueue and no-target cached-use branches."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from test_cached_buffer_lifetime import ROOT, definition, digest, replace_once, require


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/uberhar-probe/cached-buffer-draw-use')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    output=Path(tempfile.mkdtemp(prefix='run-',dir=args.output.resolve()))
    paths=[Path(__file__),ROOT/'tools/uberhar/test_cached_buffer_lifetime.py',
           ROOT/'tools/uberhar/test_cached_buffer_draw_use.cpp',
           ROOT/'src/video_core/renderer_vulkan/vk_rasterizer.cpp']
    proof={'author':'CodexAstraLocal','status':'INCOMPLETE','inputs':{str(p):digest(p) for p in paths},'variants':[]}
    try:
        source=paths[-1].read_text();fixture=paths[-2].read_text()
        draw=definition(source,'bool RasterizerVulkan::Draw(')
        no_target=definition(draw,'    if (!framebuffer->Handle())')
        accelerate=definition(source,'bool RasterizerVulkan::AccelerateDrawBatchInternal(')
        marker=definition(source,'void RasterizerVulkan::MarkCachedShaderBuffersUsed()')
        # CodexAstraLocal: Defects remove actual enqueue stamps or move them
        # before setup/refusal. Each must fail a specific observed branch outcome.
        variants=[('candidate',accelerate,None),
          ('missing-capture-stamp',replace_once(accelerate,'                MarkCachedShaderBuffersUsed();',''),
           b'FAIL actual accelerated draw stamps each ring once\n'),
          ('missing-ordinary-stamp',replace_once(accelerate,'    MarkCachedShaderBuffersUsed();\n\n    return true;','    return true;'),
           b'FAIL actual accelerated draw stamps each ring once\n'),
          ('premature-stamp',replace_once(accelerate.replace('MarkCachedShaderBuffersUsed();',''),
           '    if (is_indexed) {','    MarkCachedShaderBuffersUsed();\n    if (is_indexed) {'),
           b'FAIL actual accelerated final tick\n')]
        for label,body,error in variants:
            case=output/label;case.mkdir();(case/'fixture.cpp').write_text(fixture)
            (case/'draw-use.inc').write_text(body+'\n'+marker+'\n');(case/'no-target.inc').write_text(no_target+'\n')
            argv=[os.environ.get('CXX','c++'),'-std=c++20','-O2',str(case/'fixture.cpp'),'-o',str(case/'fixture')]
            built=subprocess.run(argv,capture_output=True,timeout=60)
            (case/'compile.stdout').write_bytes(built.stdout);(case/'compile.stderr').write_bytes(built.stderr)
            require(built.returncode==0,'draw-use compile failed: '+built.stderr.decode()[-5000:])
            result=subprocess.run([str(case/'fixture')],capture_output=True,timeout=15)
            (case/'run.stdout').write_bytes(result.stdout);(case/'run.stderr').write_bytes(result.stderr)
            if error:require(result.returncode==23 and result.stderr==error,'wrong draw-use defect: '+label+' '+result.stderr.decode())
            else:require(result.returncode==0 and result.stdout.startswith(b'PASS draw-use cases=68 '),'draw-use candidate '+result.stderr.decode())
            proof['variants'].append({'name':label,'argv':argv,'returncode':result.returncode})
            print(label+': PASS',flush=True)
        proof['status']='PASS'
    finally:
        proof['inputs_after']={str(p):digest(p) for p in paths};proof['unchanged']=proof['inputs']==proof['inputs_after']
        proof['artifacts']={str(p.relative_to(output)):digest(p) for p in output.rglob('*') if p.is_file()}
        (output/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n');print('Proof: '+str(output),flush=True)
    require(proof['unchanged'],'draw-use source drift')


if __name__=='__main__':main()
