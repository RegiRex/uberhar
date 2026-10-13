#!/usr/bin/env python3
"""CodexAstraLocal: Check production ordered CPU overlap once on both wait transports."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from test_ready_cpu_fragments import function

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parents[2]
    source=root/'src/video_core/rasterizer_accelerated.cpp'
    text=source.read_text()
    parts=[function(text,'RasterizerAccelerated::HardwareVertex::HardwareVertex('),
           function(text,'static bool AreQuaternionsOpposite(')]
    # CodexAstraLocal: Substitute only the owner name. Reservation, rollback,
    # quaternion conversion and all 88 output bytes use the production bodies.
    for signature in ['bool RasterizerAccelerated::BeginVertexAppend(',
                      'void RasterizerAccelerated::RollbackVertexAppend(',
                      'void RasterizerAccelerated::AddTriangle(']:
        parts.append(function(text,signature).replace('RasterizerAccelerated::','AppendSink::',1))
    (out/'actual_append.inc').write_text('\n'.join(parts))
    inputs=[source,Path(__file__).resolve(),Path(__file__).with_suffix('.cpp')]+[
        root/'src'/p for p in ['common/uberhar_parallel_work.h',
        'video_core/pica/uberhar_vertex_parallel_batch.h','video_core/pica/uberhar_vertex_append.h',
        'video_core/pica/primitive_assembly.h','video_core/pica/primitive_assembly.cpp']]
    proof={'author':'CodexAstraLocal','inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'runs':[]}
    try:
        for name,defines in [('native',[]),('condition-variable',['-DPREFIX_FORCE_CV'])]:
            binary=out/name
            cmd=['c++','-std=c++20','-O2','-DNDEBUG','-DFMT_HEADER_ONLY','-pthread',*defines,
                 '-I'+str(out),'-Isrc','-Iexternals/fmt/include','-Iexternals/boost',
                 '-Iexternals/nihstro/include','-Iexternals/xxHash',str(Path(__file__).with_suffix('.cpp')),
                 'src/video_core/pica/primitive_assembly.cpp','-o',str(binary)]
            compiled=subprocess.run(cmd,cwd=root,capture_output=True,timeout=90)
            (out/(name+'.compile.log')).write_bytes(compiled.stdout+compiled.stderr)
            compiled.check_returncode()
            ran=subprocess.run([str(binary)],cwd=root,capture_output=True,timeout=30)
            (out/(name+'.run.log')).write_bytes(ran.stdout+ran.stderr)
            proof['runs'].append({'name':name,'exit':ran.returncode,'output':ran.stdout.decode()})
            ran.check_returncode()
        print('PASS:',out)
    finally:(out/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
if __name__=='__main__':main()
