#!/usr/bin/env python3
"""CodexAstraLocal: Production-helper masked replacement and exact endpoint pixel reference.

Synthetic original inputs only; no title capture, Vulkan execution, device work or timing.
Host GL adapts only Vulkan shader transports and uses lower-left/zero-to-one.
"""
import argparse, ast, ctypes, ctypes.util, hashlib, json, os, random, re, shutil, struct, subprocess, tempfile
from pathlib import Path
ROOT=Path.cwd().resolve()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixtures',type=Path);parser.add_argument('--output',type=Path)
parser.add_argument('--require-vulkan',action='store_true')
args=parser.parse_args();src=args.fixtures.resolve()
if args.output:
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
else:
    out=Path(tempfile.mkdtemp(prefix='render-',dir=src))
if not __debug__:raise SystemExit('This proof requires active assertions')
script_bytes=Path(__file__).read_bytes();(out/'render-source.py').write_bytes(script_bytes)
manifest=json.loads((src/'provenance.json').read_text())
for p,h in manifest['artifact_sha256'].items():assert sha(src/p)==h,p
for p,h in manifest['source_sha256'].items():assert sha(ROOT/p)==h,p
os.environ['MESA_SHADER_CACHE_DIR']=str(out/'mesa-cache')
import moderngl
ctx=moderngl.create_standalone_context(require=450,backend='egl')
gl=ctypes.CDLL(ctypes.util.find_library('GL'))
U,I,P,B=ctypes.c_uint,ctypes.c_int,ctypes.c_void_p,ctypes.c_ubyte
sigs={'glClipControl':(None,[U,U]),'glGetError':(U,[]),'glEnable':(None,[U]),'glDisable':(None,[U]),
      'glUseProgram':(None,[U]),'glGenVertexArrays':(None,[I,ctypes.POINTER(U)]),
      'glBindVertexArray':(None,[U]),'glBindBuffer':(None,[U,U]),
      'glEnableVertexAttribArray':(None,[U]),'glVertexAttribPointer':(None,[U,I,U,B,I,P]),
      'glDrawArrays':(None,[U,I,I]),'glDeleteVertexArrays':(None,[I,ctypes.POINTER(U)]),
      'glColorMask':(None,[B,B,B,B]),'glDepthMask':(None,[B]),
      'glBlendEquationSeparate':(None,[U,U]),'glBlendFuncSeparate':(None,[U,U,U,U])}
for name,(r,a) in sigs.items():f=getattr(gl,name);f.restype=r;f.argtypes=a
gl.glClipControl(0x8CA1,0x935F)
assert gl.glGetError()==0

# CodexAstraLocal: Reuse the proven transport-only fragment adapter exactly.
adapter_path=ROOT/'tools/uberhar/compare_fragment_state.py'
node=next(n for n in ast.parse(adapter_path.read_text()).body
          if isinstance(n,ast.FunctionDef) and n.name=='adapt')
exec(compile(ast.Module(body=[node],type_ignores=[]),'<production-test transport>','exec'))
baseline=src/'baseline';blend_mutant=src/'blend-mutant';geometry_mutant=src/'geometry-mutant'
fixtures=json.loads((baseline/'fixtures.json').read_text())
mutated=json.loads((blend_mutant/'fixtures.json').read_text())
geometry_mutated=json.loads((geometry_mutant/'fixtures.json').read_text())
assert len(fixtures['cases'])==len(mutated['cases'])==len(geometry_mutated['cases'])==114
assert fixtures['stride']==88 and [a['offset'] for a in fixtures['attributes']]==[0,16,32,40,48,56,60,76]
assert all(row['admitted'] for row in fixtures['cases'][:90])
assert not any(row['admitted'] for row in fixtures['cases'][90:])
assert fixtures['old_accepts']==2 and fixtures['new_accepts']==88 and fixtures['rejected']==24
for row in fixtures['cases']:
    i=row['id']
    for folder in (blend_mutant,geometry_mutant):
        for suffix in ('vertices.bin','state.bin','fs.bin','vs.bin','generic.frag'):
            assert (baseline/f'{i}-{suffix}').read_bytes()==(folder/f'{i}-{suffix}').read_bytes()
for i in (92,93,94,95): assert mutated['cases'][i]['admitted'],i
for i in (107,108,109): assert geometry_mutated['cases'][i]['admitted'],i
compute_path=src/'compute.comp'
compute_source=compute_path.read_text()
compute_gl=re.sub(r'set\s*=\s*0\s*,\s*','',compute_source).replace(
    'layout(push_constant) uniform RectState','layout(std430, binding=1) readonly buffer RectState')
compute=ctx.compute_shader(compute_gl)
# CodexAstraLocal: Deliberate exact-byte preservation failures must be detected
# independently of candidate admission, including ordered read-after-write use.
mask_mutant=ctx.compute_shader(compute_gl.replace(
    'value = (previous & ~state.byte_mask) | (value & state.byte_mask);',
    'value = state.color;'))
stale_mutant=ctx.compute_shader(compute_gl.replace('uint previous = imageLoad(target_image, pixel).r;',
                                                'uint previous = 0u;'))
vs_sources={clip:(baseline/('trivial-clip.vert' if clip else 'trivial.vert')).read_text()
            for clip in (False,True)}

# CodexAstraLocal: Validate the exact original Vulkan modules before adapting them for GL.
modules={('comp',compute_source)}
modules.update(('vert',s) for s in vs_sources.values())
modules.update(('frag',(baseline/f'{r["id"]}-generic.frag').read_text()) for r in fixtures['cases'])
validator=shutil.which('glslangValidator') or ROOT/'build/uberhar-validators/glslang/StandAlone/glslang'
spirv_val=shutil.which('spirv-val') or ROOT/'build/uberhar-validators/spirv-tools/tools/spirv-val'
have_validators=Path(validator).is_file() and Path(spirv_val).is_file()
if args.require_vulkan and not have_validators:
    raise SystemExit('Vulkan validators are mandatory for this gate')
module_rows=[]
for i,(stage,source) in enumerate(sorted(modules) if have_validators else []):
    # CodexAstraLocal: glslang -V defines VULKAN=100 itself; remove only the
    # host fixture's equivalent truthy transport define, never shader arithmetic.
    source=source.replace('#define VULKAN 1\n','')
    path=out/f'module-{i}.{stage}';path.write_text(source);spv=path.with_suffix('.spv')
    commands=[[str(validator),'-V','--target-env','vulkan1.1','-S',stage,str(path),'-o',str(spv)],
              [str(spirv_val),'--target-env','vulkan1.1',str(spv)]]
    logs=[]
    for cmd in commands:
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=60);logs.append(p.stdout+p.stderr)
        (out/f'module-{i}.log').write_text(''.join(logs));p.check_returncode()
    module_rows.append({'stage':stage,'source_sha256':sha(path),'spirv_sha256':sha(spv),'commands':commands})

w,h=fixtures['target'];size=w*h*4
reference=ctx.texture((w,h),4,dtype='f1');target=ctx.texture((w,h),1,dtype='u4')
framebuffer=ctx.framebuffer([reference]);framebuffer.use()
target.bind_to_image(0,read=True,write=True)
packet_buffer=ctx.buffer(reserve=32);fs=ctx.buffer(reserve=0x530)
extra=ctx.buffer(reserve=32);state=ctx.buffer(reserve=128)
vbo=ctx.buffer(reserve=6*88);vao=U();gl.glGenVertexArrays(1,ctypes.byref(vao))
gl.glBindVertexArray(vao);gl.glBindBuffer(0x8892,vbo.glo)
for a in fixtures['attributes']:
    gl.glEnableVertexAttribArray(a['location'])
    gl.glVertexAttribPointer(a['location'],a['size'],0x1406,0,88,P(a['offset']))
# CodexAstraLocal: Dynamic TEV carries valid bindings even though the chosen states
# consume only primary/constant/previous color; no missing resource is the oracle.
resources=[]
for unit in range(6):
    t=ctx.texture((8,1),4,bytes([23+unit,57,89,255]*8));t.filter=(moderngl.NEAREST,moderngl.NEAREST)
    t.use(unit);resources.append(t)
programs={}
def program(row):
    vs=adapt(vs_sources[row['clip_interface']]);frag=adapt((baseline/f'{row["id"]}-generic.frag').read_text())
    key=(vs,frag)
    if key not in programs:
        assert len(programs)<8,'unexpected fragment family expansion'
        programs[key]=ctx.program(vertex_shader=vs,fragment_shader=frag)
    return programs[key]
def graphics(row,wrong_flip=False):
    i=row['id'];vb=(baseline/f'{i}-vertices.bin').read_bytes()
    assert len(vb)==6*88;vbo.write(vb)
    ub=(baseline/f'{i}-fs.bin').read_bytes();st=(baseline/f'{i}-state.bin').read_bytes()
    vs=bytearray((baseline/f'{i}-vs.bin').read_bytes())
    assert len(ub)==0x530 and len(st)==128 and len(vs)==32
    if wrong_flip:struct.pack_into('<I',vs,4,not bool(struct.unpack_from('<I',vs,4)[0]))
    fs.write(ub);fs.bind_to_uniform_block(2);extra.write(vs);extra.bind_to_uniform_block(1)
    state.write(st);state.bind_to_storage_buffer(1)
    framebuffer.use();ctx.viewport=tuple(row['viewport'])
    x0,y0,x1,y1=row['drawrect'];ctx.scissor=(x0,y0,x1-x0,y1-y0)
    ctx.disable(moderngl.BLEND|moderngl.DEPTH_TEST|moderngl.CULL_FACE)
    gl.glDisable(0x0B90);gl.glDepthMask(0)
    mask=row['color_mask'];gl.glColorMask(*[(mask>>c)&1 for c in range(4)])
    # CodexAstraLocal: Exact supported enum mapping mirrors PicaToVK::BlendFunc;
    # the reference executes actual blend state rather than packet replacement.
    if row['blend']:
        factors={0:0,1:1,6:0x0302,7:0x0303,8:0x0304}
        assert row['blend_equations']==[0,0]
        gl.glBlendEquationSeparate(0x8006,0x8006)
        gl.glBlendFuncSeparate(*[factors[v] for v in row['blend_factors']])
        gl.glEnable(0x0BE2)
    for plane in (0x3000,0x3001):(gl.glEnable if row['clip_interface'] else gl.glDisable)(plane)
    gl.glUseProgram(program(row).glo);gl.glBindVertexArray(vao);gl.glDrawArrays(0x0004,0,6)
    assert gl.glGetError()==0
def dispatch(row,folder=baseline,color_mutant=False,kernel=compute):
    data=bytearray((folder/f'{row["id"]}-packet.bin').read_bytes());assert len(data)==32
    if color_mutant:data[16]^=1
    x,y,rw,rh=struct.unpack_from('<4i',data);assert rw>0 and rh>0
    packet_buffer.write(data);packet_buffer.bind_to_storage_buffer(1)
    kernel.run((rw+7)//8,(rh+7)//8,1);ctx.memory_barrier()
def background(seed,row=None):
    rng=random.Random(seed)
    color=None if row is None else struct.pack('<I',row['packed_color'])
    # CodexAstraLocal: Every enabled lane initially differs from replacement,
    # so useful-pixel accounting cannot be satisfied by a coincidental no-op.
    return bytes((rng.randrange(1,256)^color[c]) if color is not None else rng.randrange(256)
                 for _ in range(w*h) for c in range(4))
def compare(expected,actual):
    assert len(expected)==len(actual)==size,'incomplete framebuffer comparison'
    return {'different_components':sum(a!=b for a,b in zip(expected,actual)),
            'different_pixels':sum(expected[k:k+4]!=actual[k:k+4] for k in range(0,size,4)),
            'expected_sha256':hashlib.sha256(expected).hexdigest(),
            'actual_sha256':hashlib.sha256(actual).hexdigest()}

rows=[]
for row in fixtures['cases'][:90]:
    bg=background(901+row['id'],row);reference.write(bg);target.write(bg)
    graphics(row);expected=reference.read();dispatch(row);actual=target.read()
    result=compare(expected,actual)
    touched=sum(expected[k:k+4]!=bg[k:k+4] for k in range(0,size,4))
    assert touched==row['pixels'] and 0<touched<w*h,(row,touched)
    assert result['different_pixels']==0,(row['id'],result)
    for c in range(4):
        if not row['color_mask']&(1<<c):
            assert expected[c::4]==bg[c::4] and actual[c::4]==bg[c::4]
    rows.append({'id':row['id'],'old_admitted':row['old_admitted'],
                 'color_mask':row['color_mask'],'touched_pixels':touched,
                 'untouched_pixels':w*h-touched,**result})
print('PASS 90 original-vertex pixel pairs: 2 inherited and 88 newly admitted',flush=True)

# CodexAstraLocal: These genuinely consecutive sequences change masks, endpoint
# sources, geometry and flip. All references run before their compute sequence.
sequences=[]
for ids in ((0,32,61,76),(12,43,28,65),(38,51,16,80),(47,22,6,86)):
    bg=background(2000+ids[0]);reference.write(bg);target.write(bg)
    for i in ids:graphics(fixtures['cases'][i])
    expected=reference.read()
    for i in ids:dispatch(fixtures['cases'][i])
    result=compare(expected,target.read());assert result['different_pixels']==0,(ids,result)
    sequences.append({'ids':ids,**result})

negative=[]
# CodexAstraLocal: Removing the actual candidate blend proof admits the same
# original input/state, and real fixed-function blending disproves replacement.
for i in (92,93,94,95):
    row=fixtures['cases'][i];bg=background(4800+i)
    reference.write(bg);target.write(bg);graphics(row);expected=reference.read()
    dispatch(row,blend_mutant);result=compare(expected,target.read())
    assert result['different_pixels']>0,(i,result)
    negative.append({'control':row['control'],'baseline_rejected':True,
                     'mutant_admitted':True,'identical_original_bytes':True,**result})
for name,kernel in (('disabled_channel_overwrite',mask_mutant),('stale_rmw_zero',stale_mutant)):
    row=fixtures['cases'][32];bg=background(4900)
    reference.write(bg);target.write(bg);graphics(row);expected=reference.read()
    dispatch(row,kernel=kernel);result=compare(expected,target.read())
    assert result['different_pixels']>0,result
    negative.append({'control':name,**result})
row=fixtures['cases'][0];bg=background(5000)
reference.write(bg);target.write(bg);graphics(row);expected=reference.read()
dispatch(row,color_mutant=True);result=compare(expected,target.read());assert result['different_pixels']>0
negative.append({'control':'packet_red_bit_changed',**result})
# CodexAstraLocal: The translated asymmetric case makes stale flip state visible;
# a vertically symmetric rectangle cannot serve as this sensitivity control.
row=fixtures['cases'][10];reference.write(bg);target.write(bg);graphics(row,wrong_flip=True)
expected=reference.read();dispatch(row);result=compare(expected,target.read());assert result['different_pixels']>0
negative.append({'control':'stale_vertex_flip_uniform',**result})
assert len(rows)==90 and len(sequences)==4 and len(negative)==8
assert gl.glGetError()==0
assert Path(__file__).read_bytes()==script_bytes,'runner changed during validation'
for p,hsh in manifest['source_sha256'].items():assert sha(ROOT/p)==hsh,p
report={'author':'CodexAstraLocal','renderer':ctx.info['GL_RENDERER'],
        'source_manifest_sha256':sha(src/'provenance.json'),'render_script_sha256':sha(Path(__file__)),
        'transport_adapter_sha256':sha(adapter_path),'candidate_compute_shader_sha256':sha(compute_path),
        'host_scope':'GL lower-left zero-to-one, original88B vertices, production shaders; no Vulkan execution',
        'accepted_cases':rows,'ordered_sequences':sequences,'negative_controls':negative,
        'validated_vulkan_modules':module_rows,'resident_graphics_programs':len(programs),
        'original_vertices_preserved':True,'no_device_or_title_claim':True,
        'new_geometry_rejections_proved_by_removed_guard':[107,108,109],
        'conservative_near_endpoint_rejections':[104,105],
        'grid_checks':fixtures['grid_checks'],'raw_state_and_fragment_data_original':True}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
gl.glDeleteVertexArrays(1,ctypes.byref(vao))
for p in programs.values():p.release()
print('PASS 4 ordered sequences, 8 required pixel failures, '+str(len(module_rows))+' validated Vulkan modules',flush=True)
print(out,flush=True)
