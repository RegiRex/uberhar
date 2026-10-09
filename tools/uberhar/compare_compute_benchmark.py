#!/usr/bin/env python3
"""CodexAstraLocal: Exact supported scratch pixels using original CPU vertex bytes.

This finite host GL check validates generated shaders and the independent image
oracle. Vulkan modules are validated, not executed; app-owned lifetime/device
timestamps and actual Vulkan viewport behavior remain separate evidence.
"""
import argparse
import ast
import ctypes
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT=Path.cwd().resolve()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifacts',type=Path)
parser.add_argument('--require-vulkan',action='store_true')
args=parser.parse_args();src=args.artifacts.resolve()
if (src/'latest.json').is_file():src=Path(json.loads((src/'latest.json').read_text())['directory'])
proof=json.loads((src/'provenance.json').read_text())
for p,h in proof['source_sha256'].items():assert sha(ROOT/p)==h,p
for p,h in proof['artifacts'].items():assert sha(src/p)==h,p
out=Path(tempfile.mkdtemp(prefix='render-',dir=src))
os.environ['MESA_SHADER_CACHE_DIR']=str(out/'mesa-cache')
import moderngl
ctx=moderngl.create_standalone_context(require=450,backend='egl')
gl=ctypes.CDLL(ctypes.util.find_library('GL'))
U,I,P,B=ctypes.c_uint,ctypes.c_int,ctypes.c_void_p,ctypes.c_ubyte
sigs={'glClipControl':(None,[U,U]),'glGetError':(U,[]),'glEnable':(None,[U]),'glDisable':(None,[U]),
      'glUseProgram':(None,[U]),'glGenVertexArrays':(None,[I,ctypes.POINTER(U)]),
      'glBindVertexArray':(None,[U]),'glBindBuffer':(None,[U,U]),
      'glEnableVertexAttribArray':(None,[U]),'glVertexAttribPointer':(None,[U,I,U,B,I,P]),
      'glDrawArrays':(None,[U,I,I]),'glColorMask':(None,[B,B,B,B]),'glLogicOp':(None,[U])}
for name,(result,arguments) in sigs.items():f=getattr(gl,name);f.restype=result;f.argtypes=arguments
# CodexAstraLocal: Explicit transport coordinates match previous host ABI gates;
# they do not establish target viewport/driver equivalence by themselves.
gl.glClipControl(0x8CA1,0x935F)
assert gl.glGetError()==0
adapter_path=ROOT/'tools/uberhar/compare_fragment_state.py'
node=next(n for n in ast.parse(adapter_path.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='adapt')
exec(compile(ast.Module(body=[node],type_ignores=[]),'<existing transport adapter>','exec'))
folder=src/'baseline/data/fixtures'
manifest=json.loads((folder/'manifest.json').read_text())
assert manifest['stride']==88 and [a['offset'] for a in manifest['attributes']]==[0,16,32,40,48,56,60,76]
assert len(manifest['cases'])==4
compute_source=(src/'compute.comp').read_text()
compute_gl=re.sub(r'set\s*=\s*0\s*,\s*','',compute_source).replace(
    'layout(push_constant) uniform RectState','layout(std430, binding=1) readonly buffer RectState')
compute=ctx.compute_shader(compute_gl)

# CodexAstraLocal: All original Vulkan profile siblings are validated before
# applying the transport-only host GLSL adapter; no shader arithmetic is changed.
sources={('comp',compute_source)}
sources.update(('vert',p.read_text()) for p in folder.glob('*.vert'))
sources.update(('frag',p.read_text()) for p in folder.glob('*.frag'))
validator=shutil.which('glslangValidator') or ROOT/'build/uberhar-validators/glslang/StandAlone/glslang'
spirv=shutil.which('spirv-val') or ROOT/'build/uberhar-validators/spirv-tools/tools/spirv-val'
available=Path(validator).is_file() and Path(spirv).is_file()
if args.require_vulkan and not available:raise SystemExit('Vulkan validators required')
modules=[]
for i,(stage,source) in enumerate(sorted(sources) if available else []):
    p=out/f'module-{i}.{stage}';p.write_text(source.replace('#define VULKAN 1\n',''));binary=p.with_suffix('.spv')
    commands=[[str(validator),'-V','--target-env','vulkan1.1','-S',stage,str(p),'-o',str(binary)],
              [str(spirv),'--target-env','vulkan1.1',str(binary)]]
    logs=[]
    for command in commands:
        r=subprocess.run(command,capture_output=True,text=True,timeout=60);logs.append(r.stdout+r.stderr)
        (out/f'module-{i}.log').write_text(''.join(logs));r.check_returncode()
    modules.append({'stage':stage,'source_sha256':sha(p),'binary_sha256':sha(binary),'commands':commands})

packet=ctx.buffer(reserve=32);fs=ctx.buffer(reserve=0x530);extra=ctx.buffer(reserve=32);state=ctx.buffer(reserve=128)
vbo=ctx.buffer(reserve=12*88);vao=U();gl.glGenVertexArrays(1,ctypes.byref(vao));gl.glBindVertexArray(vao);gl.glBindBuffer(0x8892,vbo.glo)
for a in manifest['attributes']:
    gl.glEnableVertexAttribArray(a['location']);gl.glVertexAttribPointer(a['location'],a['size'],0x1406,0,88,P(a['offset']))
# CodexAstraLocal: Generic shader branches receive valid dummy resources even
# though these synthetic states select only primary/previous color.
resources=[]
for unit in range(6):
    texture=ctx.texture((8,1),4,bytes([17,53,89,255]*8));texture.filter=(moderngl.NEAREST,moderngl.NEAREST)
    texture.use(unit);resources.append(texture)
rows=[];controls=[]
for case in manifest['cases']:
    index=case['id'];w,h=case['width'],case['height'];mask=case['channels']
    bg=(folder/f'{index}-background.bin').read_bytes();expected=(folder/f'{index}-expected.bin').read_bytes()
    assert len(bg)==len(expected)==w*h*4
    original=(folder/f'{index}-vertices.bin').read_bytes();packets=(folder/f'{index}-packets.bin').read_bytes()
    assert len(original)==12*88 and len(packets)==64
    vbo.write(original)
    fs.write((folder/f'{index}-uniforms.bin').read_bytes());fs.bind_to_uniform_block(2)
    extra.write((folder/f'{index}-vs.bin').read_bytes());extra.bind_to_uniform_block(1)
    rgba=ctx.texture((w,h),4,dtype='f1');integer=ctx.texture((w,h),1,dtype='u4');wrong=ctx.texture((w,h),1,dtype='u4')
    fbo=ctx.framebuffer([rgba]);fbo.use();ctx.viewport=(0,0,w,h);ctx.scissor=(0,0,w,h)
    ctx.disable(moderngl.BLEND|moderngl.DEPTH_TEST|moderngl.CULL_FACE);gl.glDisable(0x0B90)
    gl.glColorMask(*[(mask>>lane)&1 for lane in range(4)])
    def dispatch(kind='normal'):
        integer.write(bg);wrong.write(bg)
        (wrong if kind=='wrong-image' else integer).bind_to_image(0,read=True,write=True)
        for operation in range(0 if kind=='no-work' else 32):
            rect=(operation&1)^(kind=='wrong-order')
            data=bytearray(packets[rect*32:(rect+1)*32])
            if kind=='mask-all':struct.pack_into('<I',data,20,0xffffffff)
            packet.write(data);packet.bind_to_storage_buffer(1)
            _,_,rw,rh=struct.unpack_from('<4i',data);compute.run((rw+7)//8,(rh+7)//8,1);ctx.memory_barrier()
        return integer.read()
    actual=dispatch()
    assert actual==expected,(index,'compute mismatch')
    for profile in range(4):
        rgba.write(bg);fbo.use()
        # CodexAstraLocal: ModernGL framebuffer binding restores its own color
        # mask; apply the actual case mask after that binding for every reference.
        gl.glColorMask(*[(mask>>lane)&1 for lane in range(4)])
        state.write((folder/f'{index}-{profile}-state.bin').read_bytes());state.bind_to_storage_buffer(1)
        for plane in (0x3000,0x3001):(gl.glEnable if profile&1 else gl.glDisable)(plane)
        # CodexAstraLocal: Blend-disabled Copy follows the production hardware
        # logic flag; the other profile executes the generator's emulation branch.
        (gl.glDisable if profile&2 else gl.glEnable)(0x0BF2);gl.glLogicOp(0x1503)
        program=ctx.program(vertex_shader=adapt((folder/f'{profile}.vert').read_text()),
                            fragment_shader=adapt((folder/f'{index}-{profile}.frag').read_text()))
        gl.glUseProgram(program.glo);gl.glBindVertexArray(vao)
        for operation in range(32):gl.glDrawArrays(0x0004,(operation&1)*6,6)
        assert gl.glGetError()==0
        reference=rgba.read()
        if reference!=expected or actual!=expected:
            (out/f'failure-{index}-{profile}-graphics.bin').write_bytes(reference)
            (out/f'failure-{index}-{profile}-compute.bin').write_bytes(actual)
            (out/f'failure-{index}-{profile}-expected.bin').write_bytes(expected)
            raise AssertionError((index,profile,'graphics/oracle mismatch'))
        for lane in range(4):
            if not mask&(1<<lane):assert reference[lane::4]==actual[lane::4]==bg[lane::4]
        rows.append({'case':index,'clip_planes':bool(profile&1),'hardware_logic':not bool(profile&2),
                     'graphics_commands':32,'compute_commands':32,'checked_pixels':w*h,
                     'changed_pixels':case['changed_pixels'],'rgba_sha256':hashlib.sha256(actual).hexdigest()})
        program.release()
    if index==1:
        for kind in ('wrong-order','no-work','wrong-image','mask-all'):
            bad=dispatch(kind);different=sum(a!=b for a,b in zip(bad,expected));assert different>0,kind
            controls.append({'control':kind,'different_bytes':different,'rejected':True})
    fbo.release();rgba.release();integer.release();wrong.release()
assert len(rows)==16 and len(controls)==4
report={'author':'CodexAstraLocal','renderer':ctx.info['GL_RENDERER'],'pairs':rows,'negative_controls':controls,
        'modules':modules,'scope':'host original88B graphics/compute/independent integer image; no device timing or Vulkan execution'}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'provenance.json').write_text(json.dumps({'source_sha256':{str(Path(__file__)):sha(Path(__file__)),str(adapter_path):sha(adapter_path)},
    'input_provenance_sha256':sha(src/'provenance.json'),'report_sha256':sha(out/'report.json')},indent=2)+'\n')
print(f'PASS 16 original-input benchmark pairs, 4 pixel defects, {len(modules)} Vulkan modules: {out}',flush=True)
