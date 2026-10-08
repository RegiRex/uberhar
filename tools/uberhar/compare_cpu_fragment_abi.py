#!/usr/bin/env python3
"""CodexAstraLocal: Compare production CPU ABI/trivial VS with complete fragment routes.

This synthetic host OpenGL gate covers finite normal OutputVertex fixtures, exact
CPU quaternion correction and both clipping interfaces. SanitizeVertex epsilon
branches and NaN/Inf/subnormal behavior are not exercised. Lower-left host
coordinates and zero-to-one depth do not prove Vulkan viewport equivalence.
Compared framebuffer pixels include clear/discard/clip regions. Optional binary
checks execute host OpenGL SPIR-V and validate Vulkan siblings without executing
Vulkan or Adreno. Production selection/ownership is a separate regression.
"""
from pathlib import Path
from collections import OrderedDict
import ast,ctypes,ctypes.util,hashlib,json,math,os,re,struct,sys
import moderngl
# CodexAstraLocal: Resolve only generated host artifacts; preserve each complete validation run.
import argparse
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('artifacts', type=Path)
parser.add_argument('--require-spirv', action='store_true')
args = parser.parse_args()
out = args.artifacts.resolve()
if (out / 'latest.json').is_file():
    out = Path(json.loads((out / 'latest.json').read_text())['directory'])
manifest = json.loads((out / 'manifest.json').read_text())
for name, expected_hash in manifest['fixture_sha256'].items():
    assert hashlib.sha256((out / name).read_bytes()).hexdigest() == expected_hash, name
for name, expected_hash in manifest['corpus_sha256'].items():
    assert hashlib.sha256((out / 'corpus-false' / name).read_bytes()).hexdigest() == expected_hash, name
assert len(manifest['corpus_sha256']) == 4224
run = Path(tempfile.mkdtemp(prefix='render-', dir=out))
os.environ['MESA_SHADER_CACHE_DIR']=str(run/'mesa-cache')
ctx=moderngl.create_standalone_context(require=450,backend='egl')
gl=ctypes.CDLL(ctypes.util.find_library('GL'))
U,I,P,B=ctypes.c_uint,ctypes.c_int,ctypes.c_void_p,ctypes.c_ubyte
sigs={'glClipControl':(None,[U,U]),'glGetError':(U,[]),'glEnable':(None,[U]),'glDisable':(None,[U]),'glUseProgram':(None,[U]),'glGenVertexArrays':(None,[I,ctypes.POINTER(U)]),'glBindVertexArray':(None,[U]),'glBindBuffer':(None,[U,U]),'glEnableVertexAttribArray':(None,[U]),'glVertexAttribPointer':(None,[U,I,U,B,I,P]),'glDrawArrays':(None,[U,I,I]),'glBindBufferBase':(None,[U,U,U]),'glBeginTransformFeedback':(None,[U]),'glEndTransformFeedback':(None,[]),'glDeleteVertexArrays':(None,[I,ctypes.POINTER(U)]),'glDepthMask':(None,[B])}
for n,(r,a) in sigs.items():f=getattr(gl,n);f.restype=r;f.argtypes=a
gl.glClipControl(0x8CA1,0x935F) # CodexAstraLocal: Lower-left host coordinates, Vulkan zero-to-one depth.
# CodexAstraLocal: Reuse the existing tested transport-only adapter verbatim through AST extraction.
base=(ROOT / 'tools/uberhar/compare_fragment_state.py').read_text()
node=next(x for x in ast.parse(base).body if isinstance(x,ast.FunctionDef) and x.name=='adapt')
exec(compile(ast.Module(body=[node],type_ignores=[]),'<retained adapt>','exec'))
setup=base[base.index('# AstraEH: Nonconstant texels'):base.index('# AstraPro: Bound the oracle')]
exec(compile(setup,'<retained texture/LUT setup>','exec'))
# CodexAstraLocal: Independent aligned rings expose actual per-draw binding offsets without carrying pointers.
uniforms.release();state.release()
fs_stride=0x600;vs_stride=64;state_stride=256
uniforms=ctx.buffer(reserve=3*fs_stride);extra=ctx.buffer(reserve=3*vs_stride);state=ctx.buffer(reserve=3*state_stride)
fixtures=out/'fixtures';cases=out/'corpus-false';abi=json.loads((fixtures/'abi.json').read_text())
assert abi['stride']==88 and [a['offset'] for a in abi['attributes']]==[0,16,32,40,48,56,60,76]
vertex={clip:adapt((fixtures/('trivial-clip.vert' if clip else 'trivial.vert')).read_text()) for clip in (False,True)}
# CodexAstraLocal: A distinct texture bank makes descriptor reuse observable; it is bounded to the same six units.
texture_banks=[textures,[]]
for unit in range(3):
    data=bytes(v for y in range(16) for x in range(16) for v in ((255-x*13+unit*17)%256,(255-y*11+unit*37)%256,((x+2*y)*29+unit*41)%256,(255,0,128,127)[(2*x+y+unit)%4]))
    t=ctx.texture((16,16),4,data);t.build_mipmaps();t.filter=(moderngl.LINEAR_MIPMAP_LINEAR,moderngl.LINEAR);texture_banks[1].append(t)
vaos={};vbos={}
for f in range(4):
    raw=(fixtures/f'vertices-{f}.bin').read_bytes();vbos[f]=ctx.buffer(raw)
    vao=U();gl.glGenVertexArrays(1,ctypes.byref(vao));vaos[f]=vao
    gl.glBindVertexArray(vao);gl.glBindBuffer(0x8892,vbos[f].glo)
    for a in abi['attributes']:
        gl.glEnableVertexAttribArray(a['location']);gl.glVertexAttribPointer(a['location'],a['size'],0x1406,0,abi['stride'],P(a['offset']))
assert gl.glGetError()==0
programs=OrderedDict();compiled=0;max_resident=0

def program(i,route,clip):
    global compiled,max_resident
    fs=adapt((cases/f'{i}-{route}.frag').read_text());key=(vertex[clip],fs)
    if key not in programs:
        if len(programs)>=16:
            _,p=programs.popitem(last=False);p.release()
        programs[key]=ctx.program(vertex_shader=vertex[clip],fragment_shader=fs);compiled+=1;max_resident=max(max_resident,len(programs))
    programs.move_to_end(key);return programs[key]

def bind(i,u,slot,bank,stale_state=None):
    # CodexAstraLocal: Data and dynamic ranges are rebound for every draw, including S→G.
    ub=(cases/f'{i}-uniforms.bin').read_bytes();st=(cases/f'{i if stale_state is None else stale_state}-state.bin').read_bytes();vs=(fixtures/f'vs-uniform-{u}.bin').read_bytes()
    assert len(ub)==0x530 and len(st)==128 and len(vs)==32
    uniforms.write(ub,slot*fs_stride);uniforms.bind_to_uniform_block(2,offset=slot*fs_stride,size=0x530)
    extra.write(vs,slot*vs_stride);extra.bind_to_uniform_block(1,offset=slot*vs_stride,size=32)
    state.write(st,slot*state_stride);state.bind_to_storage_buffer(1,offset=slot*state_stride,size=128)
    for unit,t in enumerate(texture_banks[bank]):t.use(unit)
    for unit in (3,4,5):lut.use(unit)

def draw(i,route,fixture,clip,u,slot=0,bank=0,stale_state=None,wrong_program=None,clear=True):
    bind(i,u,slot,bank,stale_state)
    gl.glEnable(0x3000) if clip else gl.glDisable(0x3000)
    gl.glEnable(0x3001) if clip else gl.glDisable(0x3001)
    target.use();ctx.enable(moderngl.DEPTH_TEST);gl.glDepthMask(1);ctx.depth_func='<='
    if clear:target.clear(.37,.19,.53,.71,depth=1.)
    p=program(i if wrong_program is None else wrong_program,route,clip)
    gl.glUseProgram(p.glo);gl.glBindVertexArray(vaos[fixture]);gl.glDrawArrays(0x0004,0,len((fixtures/f'vertices-{fixture}.bin').read_bytes())//88)
    assert gl.glGetError()==0
    return target.read(components=4,alignment=1),depth.read(alignment=1)

# CodexAstraLocal: Finite fixtures must never let NaN/Inf bypass an ordered tolerance comparison.
def require_finite(values, label):
    if not all(math.isfinite(value) for value in values):
        raise AssertionError(f'Nonfinite numeric output in {label}')


def comparison(a,b):
    ac,ad=a;bc,bd=b
    expected_depth = struct.unpack('<1024f', ad)
    actual_depth = struct.unpack('<1024f', bd)
    require_finite(expected_depth, 'reference depth')
    require_finite(actual_depth, 'actual depth')
    delta=max(abs(x-y) for x,y in zip(expected_depth,actual_depth))
    return {'rgba_components_different':sum(x!=y for x,y in zip(ac,bc)),'max_depth_delta':delta,'equal':ac==bc and delta<=1e-6}
# CodexAstraLocal: Exercise the rejection through the real comparison and shared TF finite predicate.
nonfinite_controls = []
for invalid in (float('nan'), float('inf'), -float('inf')):
    for side in ('reference', 'actual'):
        values = [1.0] * 1024
        values[-1] = invalid
        good = (bytes(4096), struct.pack('<1024f', *([1.0] * 1024)))
        bad = (bytes(4096), struct.pack('<1024f', *values))
        try:
            comparison(bad if side == 'reference' else good, bad if side == 'actual' else good)
        except AssertionError:
            nonfinite_controls.append({'family': 'depth', 'side': side, 'value': str(invalid)})
        else:
            raise AssertionError('Nonfinite depth mutant escaped')
    try:
        require_finite((0.0, invalid), 'TF negative control')
    except AssertionError:
        nonfinite_controls.append({'family': 'TF finite predicate', 'value': str(invalid)})
    else:
        raise AssertionError('Nonfinite TF mutant escaped')

# CodexAstraLocal: Transform feedback independently checks every transported lane before fragment liveness hides one.
tf_rows=[];negative=[]
for clip in (False,True):
    names=['gl_Position','primary_color','texcoord0','texcoord1','texcoord2','texcoord0_w','normquat','view']+(['gl_ClipDistance'] if clip else [])
    p=ctx.program(vertex_shader=vertex[clip],varyings=names)
    count=24 if clip else 22
    for fixture in range(4):
        raw=(fixtures/f'vertices-{fixture}.bin').read_bytes();n=len(raw)//88
        for u in range(4):
            extra.write((fixtures/f'vs-uniform-{u}.bin').read_bytes(),0);extra.bind_to_uniform_block(1,offset=0,size=32)
            buffer=ctx.buffer(reserve=n*count*4)
            gl.glUseProgram(p.glo);gl.glBindVertexArray(vaos[fixture]);gl.glBindBufferBase(0x8C8E,0,buffer.glo);gl.glEnable(0x8C89);gl.glBeginTransformFeedback(0x0000);gl.glDrawArrays(0x0000,0,n);gl.glEndTransformFeedback();gl.glDisable(0x8C89)
            assert gl.glGetError()==0
            actual=struct.unpack('<'+str(n*count)+'f',buffer.read());expected=[]
            for k in range(n):
                lanes=list(struct.unpack_from('<22f',raw,k*88));pos=lanes[:4];ndc=pos[2]/pos[3]
                if 0<ndc<.000001:pos[2]=0.
                if -1.00001<ndc<-1:pos[2]=-pos[3]
                if u&1:pos[1]=-pos[1]
                lanes[:4]=[pos[0],pos[1],-pos[2],pos[3]];expected+=lanes
                if clip:expected += [-pos[2],pos[0]+.25*pos[1]+.1*pos[3] if u&2 else 0.]
            differing=[]
            for k,(a,e) in enumerate(zip(actual,expected)):
                require_finite((a,e), 'transform feedback/reference')
                if k%count<22:
                    if struct.pack('<f',a)!=struct.pack('<f',e):differing.append(k)
                elif abs(a-e)>1e-6:differing.append(k)
            assert not differing,(clip,fixture,u,differing)
            tf_rows.append({'clip':clip,'fixture':fixture,'ubo':u,'vertices':n,'varying_lanes_bit_exact':22*n,'clip_distance_tolerance':1e-6 if clip else None})
            buffer.release()
    p.release()
print('PASS 32 actual software/trivial-VS transform controls',flush=True)

# CodexAstraLocal: Compare actual TF bits before/after an intentional wrong binding; no modeled shader result.
names=['gl_Position','primary_color','texcoord0','texcoord1','texcoord2','texcoord0_w','normquat','view']
p=ctx.program(vertex_shader=vertex[False],varyings=names)
fixture=1;n=6
extra.write((fixtures/'vs-uniform-1.bin').read_bytes(),0);extra.bind_to_uniform_block(1,offset=0,size=32)
def capture():
    b=ctx.buffer(reserve=n*88);gl.glUseProgram(p.glo);gl.glBindVertexArray(vaos[fixture]);gl.glBindBufferBase(0x8C8E,0,b.glo);gl.glEnable(0x8C89);gl.glBeginTransformFeedback(0);gl.glDrawArrays(0,0,n);gl.glEndTransformFeedback();gl.glDisable(0x8C89);assert gl.glGetError()==0;r=b.read();b.release();return r
reference=capture();mutations=[]
gl.glGetIntegerv.argtypes=[U,ctypes.POINTER(I)]
alignments={}
for key,enum,strides in [('UBO',0x8A34,[64,0x600]),('SSBO',0x90DF,[256])]:
    value=I();gl.glGetIntegerv(enum,ctypes.byref(value));assert value.value>0
    assert all(s%value.value==0 for s in strides),(key,value.value,strides)
    alignments[key]={'queried_alignment':value.value,'chosen_host_strides':strides}
assert gl.glGetError()==0
for name,stride,offsets in [('wrong_stride84',84,[a['offset'] for a in abi['attributes']]),('stale_uv2_from_uv1',88,[0,16,32,40,40,56,60,76]),('stale_color_from_position',88,[0,0,32,40,48,56,60,76])]:
    gl.glBindVertexArray(vaos[fixture]);gl.glBindBuffer(0x8892,vbos[fixture].glo)
    for a,offset in zip(abi['attributes'],offsets):gl.glVertexAttribPointer(a['location'],a['size'],0x1406,0,stride,P(offset))
    err=gl.glGetError();assert err==0,(name,'mutation setup',hex(err))
    actual=capture();changed=sum(a!=b for a,b in zip(reference,actual));assert changed>0,name
    mutations.append({'mutation':name,'different_tf_bytes':changed,'rejected':True})
    gl.glBindVertexArray(vaos[fixture]);gl.glBindBuffer(0x8892,vbos[fixture].glo)
    for a in abi['attributes']:gl.glVertexAttribPointer(a['location'],a['size'],0x1406,0,88,P(a['offset']))
extra.write((fixtures/'vs-uniform-0.bin').read_bytes(),0);wrong=capture();assert wrong!=reference
mutations.append({'mutation':'stale_vertex_flip_UBO','different_tf_bytes':sum(a!=b for a,b in zip(reference,wrong)),'rejected':True})
p.release()
# CodexAstraLocal: Reference renders complete before the actual contiguous mixed sequence; reads do not insert other draws.
sequences=[]
for trio in [(192,416,928),(1055,143,700),(49,415,927),(416,700,700)]:
    draw_specs=[(i,j%3+1,True,j%4,j%3,j%2) for j,i in enumerate(trio)]
    expected=[draw(i,'specialized',f,c,u,slot,bank) for i,f,c,u,slot,bank in draw_specs]
    actual=[draw(i,route,f,c,u,slot,bank) for (i,f,c,u,slot,bank),route in zip(draw_specs,['generic','specialized','generic'])]
    comparisons=[comparison(e,a) for e,a in zip(expected,actual)];assert all(c['equal'] for c in comparisons)
    sequences.append({'states':trio,'routes':['generic','specialized','generic'],'draws_contiguous':True,'readback_after_every_draw':True,'clear_before_each_draw':True,'comparisons':comparisons})
# CodexAstraLocal: The generic after specialized must use the current constants even when the FS identity did not change.
# This is a bad-resource witness, not an execution model of the candidate's dirty-state owner.
witness=None
for a,b in [(416,700),(192,416),(416,928),(49,415),(1055,700)]:
    expected=draw(b,'specialized',1,False,0,2,0)
    wrong=draw(b,'generic',1,False,0,2,0,stale_state=a)
    diff=comparison(expected,wrong)
    if not diff['equal']:witness={'sequence':[a,b,b],'last_draw_stale_state_from':a,'same_final_FS_state':True,**diff};break
assert witness is not None
# CodexAstraLocal: Independent framebuffer coverage witnesses prevent vacuous both-routes-empty success.
target.use();target.clear(.37,.19,.53,.71,depth=1.);clear=target.read(components=4,alignment=1);clear_depth=depth.read(alignment=1)
coverage=[]
for fixture,clip,u in [(0,False,0),(3,True,3)]:
    rendered=draw(192,'specialized',fixture,clip,u)
    touched=sum(rendered[0][4*k:4*k+4]!=clear[4*k:4*k+4] or rendered[1][4*k:4*k+4]!=clear_depth[4*k:4*k+4] for k in range(1024))
    assert touched==1024 if fixture==0 else 0<touched<1024,(fixture,touched)
    coverage.append({'case':192,'fixture':fixture,'clip':clip,'ubo':u,'touched_framebuffer_pixels':touched,'untouched_pixels':1024-touched})

# CodexAstraLocal: Full corpus sees changing nonconstant CPU outputs, both trivial variants and real clip/flip controls.
rows=[];failures=[];stale_witness=None;wrong_fs_witness=None
limit=1056
for i in range(limit):
    variants=[(0,False,0),(1,False,1),(2,True,0),(3,True,3)]
    for fixture,clip,u in variants:
        expected=draw(i,'specialized',fixture,clip,u,i%3,i%2)
        actual=draw(i,'generic',fixture,clip,u,(i+1)%3,i%2)
        row={'case':i,'fixture':fixture,'clip':clip,'ubo':u,**comparison(expected,actual),'specialized_color_sha256':hashlib.sha256(expected[0]).hexdigest(),'generic_color_sha256':hashlib.sha256(actual[0]).hexdigest()}
        rows.append(row)
        if not row['equal']:failures.append(row)
        if i>0 and stale_witness is None:
            wrong=draw(i,'generic',fixture,clip,u,2,i%2,stale_state=i-1);diff=comparison(expected,wrong)
            if not diff['equal']:stale_witness={'current_case':i,'stale_case':i-1,'fixture':fixture,**diff}
        if i>0 and wrong_fs_witness is None:
            wrong=draw(i,'specialized',fixture,clip,u,2,i%2,wrong_program=i-1);diff=comparison(expected,wrong)
            if not diff['equal']:wrong_fs_witness={'current_case':i,'stale_case':i-1,'fixture':fixture,**diff}
    if i%48==47:print(f'Compared {i+1}/{limit} states; differences={len(failures)}',flush=True)

# CodexAstraLocal: Count compared framebuffer locations, including untouched regions, and retain all exclusions.
report = {
    'author': 'CodexAstraLocal', 'renderer': ctx.info['GL_RENDERER'],
    'complete_state_cases': limit, 'paired_draws': len(rows),
    'compared_framebuffer_pixels_including_clear_discard_clip': len(rows) * 1024,
    'depth_tolerance': 1e-6, 'transform_checks': tf_rows,
    'contiguous_mixed_sequences': sequences, 'host_buffer_range_alignment': alignments,
    'coverage_witnesses': coverage, 'negative_binding_controls': mutations, 'nonfinite_rejection_controls': nonfinite_controls,
    'same_value_S_to_G_stale_constants_witness': witness,
    'negative_stale_dynamic_state': stale_witness,
    'negative_wrong_specialized_state': wrong_fs_witness,
    'compiled_programs': compiled, 'max_resident_programs': max_resident,
    'comparisons': rows, 'failures': failures,
    'scope': 'Actual CPU88B/software layout/trivialVS with generated fragment shaders on host OpenGL; descriptor sets, push constants and texel buffers use transport adapters. No Vulkan viewport, title, Adreno or performance proof.',
    'epsilon_sanitation_and_special_values_tested': False,
}
(run / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
assert stale_witness is not None and wrong_fs_witness is not None
assert not failures, f'{len(failures)} CPU ABI corpus mismatches; see {run}'
for cached_program in programs.values():
    cached_program.release()
programs.clear()
print(f'PASS {len(rows)} paired CPU ABI draws, 32 transform controls and four contiguous mixed sequences', flush=True)

if args.require_spirv:
    assert 'GL_ARB_gl_spirv' in ctx.extensions
    # CodexAstraLocal: CI uses installed official tools; a local source build is an explicit fallback.
    def tool(name, fallback):
        found = shutil.which(name)
        if found:
            return Path(found)
        path = ROOT / fallback
        if not path.is_file():
            raise RuntimeError(f'Missing required shader validator: {name}')
        return path
    compiler = tool('glslangValidator', 'build/uberhar-validators/glslang/StandAlone/glslang')
    validator = tool('spirv-val', 'build/uberhar-validators/spirv-tools/tools/spirv-val')
    modules=run/'modules';modules.mkdir();compiled_modules={};module_rows=[]
    # CodexAstraLocal: Match actual optional FS optimization separately from required trivial VS/generic policy.
    def compile_module(text,stage,optimized,environment):
        # CodexAstraLocal: glslang -V supplies its own VULKAN macro; retain the identical enabled branch.
        if environment=='vulkan':text=text.replace('#define VULKAN 1\n','')
        sha=hashlib.sha256(text.encode()).hexdigest();key=(sha,stage,optimized,environment)
        if key in compiled_modules:return compiled_modules[key]
        name=f'{sha[:16]}-{stage}-{int(optimized)}-{environment}';input=modules/(name+'.'+stage);output=modules/(name+'.spv');input.write_text(text)
        cmd=[str(compiler),'-V' if environment=='vulkan' else '-G','--target-env','vulkan1.1' if environment=='vulkan' else 'opengl']
        if not optimized:cmd.append('-Od')
        cmd+=['-Os','-S',stage,str(input),'-o',str(output)]
        r=subprocess.run(cmd,text=True,capture_output=True,timeout=60);(modules/(name+'.compile.log')).write_text(r.stdout+r.stderr);r.check_returncode()
        val=[str(validator),'--target-env','vulkan1.1' if environment=='vulkan' else 'opengl4.5',str(output)]
        r=subprocess.run(val,text=True,capture_output=True,timeout=30);(modules/(name+'.validate.log')).write_text(r.stdout+r.stderr);r.check_returncode()
        module_rows.append({'environment':environment,'optimized':optimized,'stage':stage,'source_sha256':sha,'binary_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'compile_argv':cmd,'validate_argv':val});compiled_modules[key]=output;return output
    sigs2={'glCreateShader':(U,[U]),'glShaderBinary':(None,[I,ctypes.POINTER(U),U,P,I]),'glSpecializeShader':(None,[U,ctypes.c_char_p,U,ctypes.POINTER(U),ctypes.POINTER(U)]),'glGetShaderiv':(None,[U,U,ctypes.POINTER(I)]),'glGetShaderInfoLog':(None,[U,I,ctypes.POINTER(I),ctypes.c_char_p]),'glCreateProgram':(U,[]),'glAttachShader':(None,[U,U]),'glLinkProgram':(None,[U]),'glGetProgramiv':(None,[U,U,ctypes.POINTER(I)]),'glGetProgramInfoLog':(None,[U,I,ctypes.POINTER(I),ctypes.c_char_p]),'glDeleteShader':(None,[U]),'glDeleteProgram':(None,[U])}
    for n,(r,a) in sigs2.items():f=getattr(gl,n);f.restype=r;f.argtypes=a
    raw_programs=OrderedDict();max_raw=0
    # CodexAstraLocal: Import and specialize compiled SPIR-V itself; never substitute regenerated GLSL for this result.
    def raw_program(i,route,clip):
        global max_raw
        vs=(fixtures/('trivial-clip.vert' if clip else 'trivial.vert')).read_text();fs=(cases/f'{i}-{route}.frag').read_text();key=(vs,fs,route)
        if key in raw_programs:raw_programs.move_to_end(key);return raw_programs[key]
        if len(raw_programs)>=16:_,old=raw_programs.popitem(last=False);gl.glDeleteProgram(old)
        shaders=[]
        for text,stage,kind,opt in [(vs,'vert',0x8B31,False),(fs,'frag',0x8B30,route=='specialized')]:
            compile_module(text,stage,opt,'vulkan')
            binary=compile_module(adapt(text),stage,opt,'opengl').read_bytes()
            shader=gl.glCreateShader(kind);handle=U(shader);raw=ctypes.create_string_buffer(binary)
            gl.glShaderBinary(1,ctypes.byref(handle),0x9551,raw,len(binary));gl.glSpecializeShader(shader,b'main',0,None,None)
            status=I();log=ctypes.create_string_buffer(16384);gl.glGetShaderiv(shader,0x8B81,ctypes.byref(status));gl.glGetShaderInfoLog(shader,len(log),None,log)
            assert status.value,(i,route,clip,log.value.decode(errors='replace'));shaders.append(shader)
        p=gl.glCreateProgram()
        for shader in shaders:gl.glAttachShader(p,shader)
        gl.glLinkProgram(p);status=I();log=ctypes.create_string_buffer(16384);gl.glGetProgramiv(p,0x8B82,ctypes.byref(status));gl.glGetProgramInfoLog(p,len(log),None,log)
        assert status.value,(i,route,clip,log.value.decode(errors='replace'))
        for shader in shaders:gl.glDeleteShader(shader)
        assert gl.glGetError()==0
        raw_programs[key]=p;max_raw=max(max_raw,len(raw_programs));return p

    def binary_draw(i,route,fixture,clip,u,slot,bank):
        bind(i,u,slot,bank);target.use();ctx.enable(moderngl.DEPTH_TEST);ctx.depth_func='<=';gl.glDepthMask(1)
        for capability in (0x3000,0x3001):gl.glEnable(capability) if clip else gl.glDisable(capability)
        target.clear(.37,.19,.53,.71,depth=1.);gl.glUseProgram(raw_program(i,route,clip));gl.glBindVertexArray(vaos[fixture]);gl.glDrawArrays(0x0004,0,3 if fixture==0 else 6);assert gl.glGetError()==0
        return target.read(components=4,alignment=1),depth.read(alignment=1)
    selected=[5,15,24,47,95,143,144,191,192,199,207,223,255,319,415,416,423,480,544,608,672,736,800,864,927,928,943,959,991,1007,1039,1055]
    rows=[];failures=[]
    for k,i in enumerate(selected):
        for fixture,clip,u in [(0,False,0),(1,False,1),(2,True,0),(3,True,3)]:
            specialized=binary_draw(i,'specialized',fixture,clip,u,0,k%2);generic=binary_draw(i,'generic',fixture,clip,u,1,k%2)
            row={'case':i,'fixture':fixture,'clip':clip,'ubo':u,**comparison(specialized,generic),'specialized_color_sha256':hashlib.sha256(specialized[0]).hexdigest(),'generic_color_sha256':hashlib.sha256(generic[0]).hexdigest()};rows.append(row)
            if not row['equal']:failures.append(row)
        print(f'Executed optimized binary states {k+1}/{len(selected)}; differences={len(failures)}',flush=True)
    report={'author':'CodexAstraLocal','renderer':ctx.info['GL_RENDERER'],'selected_states':selected,'paired_draws':len(rows),'compared_framebuffer_pixels_including_clear_discard':len(rows)*1024,'max_resident_linked_programs':max_raw,'module_validation':module_rows,'comparisons':rows,'failures':failures,'scope':'Actual Mesa GL_ARB_gl_spirv binary specialization/link/draw: optimized specialized FS versus unoptimized generic FS, required trivial VS unoptimized. Vulkan1.1 sibling modules validated only. All32 selected states use actual CPU88B data and production trivialVS; no Vulkan/Adreno pixel or timing proof.'}
    with(run/'binary-report.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
    for p in raw_programs.values():gl.glDeleteProgram(p)
    assert not failures,f'{len(failures)} optimized binary mismatches'
    print(f'PASS {len(rows)} binary pairs and {len(module_rows)} validated modules', flush=True)

# CodexAstraLocal: Release bounded driver objects and fingerprint the durable gate actually executed.
for vao in vaos.values():
    gl.glDeleteVertexArrays(1, ctypes.byref(vao))
proof = {
    'author': 'CodexAstraLocal', 'gate_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'artifact_manifest_sha256': hashlib.sha256((out / 'manifest.json').read_bytes()).hexdigest(),
    'results': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in run.glob('*.json')},
    'required_binary_checks_executed': args.require_spirv,
}
(run / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
print(f'CPU fragment ABI evidence: {run}', flush=True)
