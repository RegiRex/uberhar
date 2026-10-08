#!/usr/bin/env python3
"""CodexAstraLocal: Production JIT/GLSL dot rounding and mixed-route D24 coverage.

The required host phase compiles real x64 JIT/interpreter/generator sources.
--render-only uses the emitted files with Mesa; --require-spirv also validates
Vulkan modules and executes host OpenGL SPIR-V in both optimizer modes. This
public synthetic regression makes no Thor/Adreno or title-causality claim.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess


# CodexAstraLocal: Frozen against the published 0.1.25 generator sources.
# Precise=false must preserve source identity for Custom and retained captures.
LEGACY_SHA256 = {
    'depth-legacy.vert': '5def2638a953941f883fc6cbaba1cd7080e64f5b7a8e5579c575cd1c49ea69d2',
    'dp3-raw-legacy.vert': '7c1159a33c28d284e5b691106a004c8382882d9cabeb2fa958a077d0932b000b',
    'dp3-safe-legacy.vert': '0db8663fe9dc16aed907dc014287b2b24234b2237029a5ceba82d335a9485bb0',
    'dp4-raw-legacy.vert': '2b35ebc425bf019488a4f4772c09643ad173f561326e7b9e4e548b5264c6a06b',
    'dp4-safe-legacy.vert': 'c004aeb9788176551890974f9332a14228ca632adfe77526bf29d8fa35c4836a',
    'dph-raw-legacy.vert': '49711a535100be1e6053c9c4891f8803df618bcf05075273b0a185191219ba8d',
    'dph-safe-legacy.vert': '85aa36e606ffca30ac9b97f892e15706542da33e5639e408561bf6bf59091f94',
    'dphi-raw-legacy.vert': '5bcb7d4e43996f8afc73a4bafa34817f1c229b13d4a148c234d1511657f8472b',
    'dphi-safe-legacy.vert': '47d33c1c8b06bbcff26fadc001845214f3fdf11e35dbf0aa45ec8f041d4ce400',
    'native.vert': '6e9bb659d5651ef5e40c4cff96e7ea76bc27a8539f17440b0a9e6afa6c7845a8',
}


def verify_cases(directory):
    # CodexAstraLocal: Missing/empty cases or weakened control lists must fail,
    # including when the render gate reuses files produced by the host phase.
    cases = json.loads((directory / 'cases.json').read_text())
    names = ['depth'] + [f'{operation}-{policy}' for operation in ('dp4', 'dph', 'dphi', 'dp3')
                         for policy in ('raw', 'safe')]
    assert [case['id'] for case in cases] == names
    assert set(LEGACY_SHA256) == {f'{name}-legacy.vert' for name in names} | {'native.vert'}
    for name, expected in LEGACY_SHA256.items():
        actual = hashlib.sha256((directory / name).read_bytes()).hexdigest()
        assert actual == expected, f'Unflagged production source changed: {name}: {actual}'
    for case in cases:
        name = case['id']
        assert case['count'] == (4096 if name == 'depth' else 265)
        assert case['depth'] is (name == 'depth')
        assert case['sanitize'] is (name == 'depth' or name.endswith('-safe'))
        assert case['dp3_unchanged'] is name.startswith('dp3-')
        assert case['input_count'] == (2 if name == 'depth' else 4 if name.startswith('dphi-') else 3)
        for kind, size in (('inputs', 64), ('jit', 16), ('interpreter', 16), ('unsanitized', 16)):
            assert (directory / f'{name}-{kind}.bin').stat().st_size == case['count'] * size
        assert (directory / f'{name}-uniforms.bin').stat().st_size == 1616
    return cases


# CodexAstraLocal: Build real x64 engines and generator sources, then
# require the complete frozen corpus before any render-only reuse.
def compile_cases(directory):
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('This gate executes the production x64 JIT; use an x86_64 host')
    directory.mkdir(parents=True, exist_ok=True)
    stubs = directory / 'stubs'
    (stubs / 'common').mkdir(parents=True, exist_ok=True)
    (stubs / 'common/settings.h').write_text(
        '// CodexAstraLocal: Unused settings dependency of shader_gen.cpp.\n#pragma once\n')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2',
                    '-DMICROPROFILE_ENABLED=0', '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL',
                    f'-I{stubs}', '-Isrc', '-Iexternals/fmt/include', '-Iexternals/boost',
                    '-Iexternals/xxHash', '-Iexternals/nihstro/include',
                    '-Iexternals/microprofile', '-Iexternals/xbyak', '-Iexternals/json',
                    'tools/uberhar/test_gpu_dot_depth.cpp',
                    'src/video_core/pica/shader_unit.cpp',
                    'src/video_core/pica/shader_setup.cpp',
                    'src/video_core/shader/shader_interpreter.cpp',
                    'src/video_core/shader/shader_jit_x64_compiler.cpp',
                    'src/video_core/shader/generator/glsl_shader_gen.cpp',
                    'src/video_core/shader/generator/glsl_shader_decompiler.cpp',
                    'src/video_core/shader/generator/shader_gen.cpp',
                    '-o', str(directory / 'fixture')], check=True)
    subprocess.run([str(directory / 'fixture'), str(directory)], check=True)
    verify_cases(directory)


class Host:
    # CodexAstraLocal: Vulkan-compatible depth range, queried real 24-bit depth,
    # and explicit current GL write masks prevent false-positive coverage tests.
    def __init__(self, directory):
        import moderngl
        self.mgl = moderngl
        self.directory = directory
        os.environ.setdefault('MESA_SHADER_CACHE_DIR', str(directory / 'mesa-cache'))
        self.ctx = moderngl.create_standalone_context(require=450, backend='egl')
        self.gl = ctypes.CDLL(ctypes.util.find_library('GL'))
        self.gl.glClipControl.argtypes = [ctypes.c_uint, ctypes.c_uint]
        self.gl.glClipControl(0x8CA1, 0x935F)
        self.gl.glGetError.restype = ctypes.c_uint
        self.gl.glColorMask.argtypes = [ctypes.c_ubyte] * 4
        self.gl.glDepthMask.argtypes = [ctypes.c_ubyte]
        self.gl.glReadPixels.argtypes = [ctypes.c_int] * 4 + [ctypes.c_uint] * 2 + [ctypes.c_void_p]
        self.gl.glBindRenderbuffer.argtypes = [ctypes.c_uint, ctypes.c_uint]
        self.gl.glGetRenderbufferParameteriv.argtypes = [ctypes.c_uint, ctypes.c_uint,
                                                        ctypes.POINTER(ctypes.c_int)]
        depth = self.ctx.depth_renderbuffer((32, 32))
        self.gl.glBindRenderbuffer(0x8D41, depth.glo)
        bits = ctypes.c_int()
        self.gl.glGetRenderbufferParameteriv(0x8D41, 0x8D54, ctypes.byref(bits))
        assert bits.value == 24, f'Expected real D24 depth, got {bits.value}'
        self.color = self.ctx.texture((32, 32), 4, dtype='f1')
        self.fbo = self.ctx.framebuffer([self.color], depth)
        self.fbo.use()
        self.extra = self.ctx.buffer(bytes(32))
        self.extra.bind_to_uniform_block(1)
        self.ubo = self.ctx.buffer(reserve=1616)
        self.ubo.bind_to_uniform_block(0)
        self.fragment = '''#version 430
// CodexAstraLocal: Exact finite Z-buffering depth expression, constant color.
layout(location=0) out vec4 out_color;
void main() {
    float z_over_w = -gl_FragCoord.z;
    float depth = z_over_w * -1.0 + 0.0;
    gl_FragDepth = depth;
    out_color = vec4(1.0);
}
'''
        (directory / 'depth.frag').write_text(self.fragment)
        assert self.gl.glGetError() == 0

    def vertex_array(self, program, data, gpu):
        # CodexAstraLocal: Keep raw fetched vectors, including NaNs/signed zero;
        # no CPU-decoded replacement input defines the GPU arithmetic oracle.
        buffer = self.ctx.buffer(data)
        if not gpu:
            return self.ctx.vertex_array(program, [(buffer, '4f', 'vert_position')]), buffer
        vao = self.ctx.vertex_array(program, [])
        for index in range(4):
            name = f'vs_in_typed_reg{index}'
            if name in program:
                vao.bind(program[name].location, 'f', buffer, '4f', offset=index * 16, stride=64)
        return vao, buffer

    def transport(self, source, data, gpu):
        program = self.ctx.program(vertex_shader=source, varyings=['gl_Position'])
        count = len(data) // (64 if gpu else 16)
        vao, buffer = self.vertex_array(program, data, gpu)
        output = self.ctx.buffer(reserve=count * 16)
        vao.transform(output, vertices=count, mode=self.mgl.POINTS)
        result = output.read()
        for obj in (output, vao, buffer, program):
            obj.release()
        return result

    def coverage(self, first, second):
        # CodexAstraLocal: Every case checks depth-only color remains blank,
        # then counts the second draw, with no epsilon or discarded geometry.
        self.fbo.use()
        self.fbo.color_mask = (True,) * 4
        self.fbo.depth_mask = True
        self.gl.glColorMask(1, 1, 1, 1)
        self.gl.glDepthMask(1)
        self.fbo.clear(0, 0, 0, 0, depth=1)
        self.ctx.enable(self.mgl.DEPTH_TEST)
        self.ctx.depth_func = '<='
        self.gl.glDepthMask(1)
        self.gl.glColorMask(0, 0, 0, 0)
        first()
        assert not any(self.fbo.read(components=4, alignment=1)), 'prepass wrote color'
        stored = ctypes.c_float()
        self.gl.glReadPixels(16, 16, 1, 1, 0x1902, 0x1406, ctypes.byref(stored))
        self.gl.glColorMask(1, 1, 1, 1)
        self.gl.glDepthMask(0)
        second()
        raw = self.fbo.read(components=4, alignment=1)
        assert self.gl.glGetError() == 0
        return {'visible': sum(raw[i] != 0 for i in range(0, len(raw), 4)),
                'stored_depth': stored.value}


def compare_bits(actual, expected, label, *, classification_only_nan=True):
    # CodexAstraLocal: NaN payload/sign are not a GLSL guarantee. Finite values,
    # infinity signs and signed-zero bits must match the selected reference.
    assert len(actual) == len(expected)
    for lane, (a, b) in enumerate(zip(struct.iter_unpack('<I', actual),
                                    struct.iter_unpack('<I', expected))):
        if a == b:
            continue
        af = struct.unpack('<f', struct.pack('<I', a[0]))[0]
        bf = struct.unpack('<f', struct.pack('<I', b[0]))[0]
        if classification_only_nan and math.isnan(af) and math.isnan(bf):
            continue
        raise AssertionError(f'{label}: lane{lane} differs {a[0]:08x} vs {b[0]:08x}')


def fixed_geometry(directory, route, index):
    # CodexAstraLocal: 32 complete list triangles reach the ready route's 96
    # vertex minimum. This does not simulate the renderer's other admission gates.
    raw = bytearray()
    row = (directory / 'depth-inputs.bin').read_bytes()[index * 64:index * 64 + 64]
    native = struct.unpack_from('<4f', (directory / 'depth-jit.bin').read_bytes(), index * 16)
    for x, y in [(-1, -1), (3, -1), (-1, 3)] * 32:
        if route == 'native':
            raw.extend(struct.pack('<4f', x, y, native[2], native[3]))
        else:
            raw.extend(struct.pack('<4f', x, y, 0, 1))
            raw.extend(row[16:])
    return bytes(raw)


# CodexAstraLocal: Require both fixed failure directions and all seven
# coverage controls so missing witnesses cannot turn into a passing gate.
def check_outcomes(outcomes, index):
    assert index in (23, 30)
    assert set(outcomes) == {'native->native', 'legacy->legacy', 'native->legacy',
                             'legacy->native', 'native->precise', 'precise->native', 'precise->precise'}
    for pair, outcome in outcomes.items():
        expected = 0 if pair == ('legacy->native' if index == 30 else 'native->legacy') else 1024
        assert outcome['visible'] == expected, (index, pair, outcome, expected)


# CodexAstraLocal: Test exact numerical controls and real D24 coverage
# independently; shader validation alone does not execute either draw.
def render(directory, require_spirv):
    host = Host(directory)
    native = (directory / 'native.vert').read_text()
    cases = verify_cases(directory)
    numerical = []
    for case in cases:
        name = case['id']
        host.ubo.write((directory / (name + '-uniforms.bin')).read_bytes())
        raw = (directory / (name + '-inputs.bin')).read_bytes()
        precise = host.transport((directory / (name + '-precise.vert')).read_text(), raw, True)
        legacy = host.transport((directory / (name + '-legacy.vert')).read_text(), raw, True)
        if case['dp3_unchanged']:
            compare_bits(precise, legacy, name + ': unchanged DP3')
        else:
            expected_file = 'jit' if case['sanitize'] else 'unsanitized'
            expected = host.transport(native, (directory / f'{name}-{expected_file}.bin').read_bytes(), False)
            compare_bits(precise, expected, name + ': production precise/reference')
        numerical.append({'id': name, 'vertices': case['count'], 'passed': True})
        if case['depth']:
            expected = host.transport(native, (directory / 'depth-jit.bin').read_bytes(), False)
            compare_bits(precise, expected, 'all4096 finite witnesses', classification_only_nan=False)
            for index, bits in ((23, (0x3f1e9e50, 0xbe65070d, 0x3e10fce0, 0xbf84cde2)),
                                (30, (0x3f2dfbe1, 0xbd9e41e5, 0xbec05531, 0xbf3a090c))):
                assert struct.unpack_from('<4I', raw, index * 64 + 16) == bits
                assert legacy[index * 16:index * 16 + 16] != expected[index * 16:index * 16 + 16]
    host.ubo.write((directory / 'depth-uniforms.bin').read_bytes())
    programs = {'native': host.ctx.program(vertex_shader=native, fragment_shader=host.fragment)}
    for policy in ('legacy', 'precise'):
        programs[policy] = host.ctx.program(
            vertex_shader=(directory / f'depth-{policy}.vert').read_text(), fragment_shader=host.fragment)
    pairs = [('native', 'native'), ('legacy', 'legacy'), ('native', 'legacy'),
             ('legacy', 'native'), ('native', 'precise'), ('precise', 'native'), ('precise', 'precise')]
    coverage = []
    for index in (23, 30):
        arrays = {route: host.vertex_array(program, fixed_geometry(directory, route, index), route != 'native')
                  for route, program in programs.items()}
        draws = {route: (lambda vao=vao: vao.render(mode=host.mgl.TRIANGLES, vertices=96))
                 for route, (vao, _) in arrays.items()}
        outcomes = {f'{a}->{b}': host.coverage(draws[a], draws[b]) for a, b in pairs}
        check_outcomes(outcomes, index)
        coverage.append({'witness': index, 'outcomes': outcomes})
        for vao, buffer in arrays.values():
            vao.release()
            buffer.release()
    report = {'author': 'CodexAstraLocal', 'renderer': host.ctx.info['GL_RENDERER'],
              'depth_bits': 24, 'vertices_per_draw': 96, 'numerical': numerical,
              'coverage': coverage, 'scope': 'Host synthetic x64 JIT/Mesa; no Adreno/title proof'}
    if require_spirv:
        report['spirv'] = spirv_gate(host, cases, pairs)
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} production numerical cases, both D24 route witnesses and precise repairs'
          f' on {host.ctx.info["GL_RENDERER"]}')


# CodexAstraLocal: Required SPIR-V tools may come from PATH or the known
# local build, but their absence must fail rather than skip validation.
def tool(name, local):
    found = shutil.which(name)
    if not found and Path(local).is_file():
        found = str(Path(local).resolve())
    if not found:
        raise RuntimeError(f'Required shader validator missing: {name}')
    return found


def spirv_gate(host, cases, pairs):
    # CodexAstraLocal: Match production SpvOptions: optimizeSize always enabled,
    # disableOptimizer either true or false. Validate Vulkan 1.1/SPIR-V1.3 too.
    compiler = tool('glslangValidator', 'build/uberhar-validators/glslang/StandAlone/glslang')
    validator = tool('spirv-val', 'build/uberhar-validators/spirv-tools/tools/spirv-val')
    disassembler = tool('spirv-dis', 'build/uberhar-validators/spirv-tools/tools/spirv-dis')
    directory = host.directory / 'spirv'
    directory.mkdir(exist_ok=True)
    modules = []
    for optimizer in ('disabled', 'enabled'):
        shaders = [(f'{case["id"]}-{policy}', 'vert') for case in cases
                   for policy in ('legacy', 'precise')]
        shaders += [('native', 'vert'), ('depth', 'frag')]
        for name, stage in shaders:
            for environment in ('vulkan', 'opengl'):
                # CodexAstraLocal: All variants validate as Vulkan; the fixed
                # coverage witness also executes actual host OpenGL SPIR-V.
                if environment == 'opengl' and name not in ('depth-legacy', 'depth-precise', 'native', 'depth'):
                    continue
                source = host.directory / f'{name}.{stage}'
                output = directory / f'{environment}-{optimizer}-{name}.spv'
                target = 'vulkan1.1' if environment == 'vulkan' else 'opengl'
                command = [compiler, '-V' if environment == 'vulkan' else '-G',
                           '--target-env', target]
                if optimizer == 'disabled':
                    command.append('-Od')
                command += ['-Os', '-S', stage, str(source), '-o', str(output)]
                result = subprocess.run(command, text=True, capture_output=True, check=True)
                output.with_suffix('.log').write_text(result.stdout + result.stderr)
                subprocess.run([validator, '--target-env', target if environment == 'vulkan' else 'opengl4.5',
                                str(output)], check=True)
                assembly = subprocess.check_output([disassembler, str(output)], text=True)
                output.with_suffix('.spvasm').write_text(assembly)
                decorated = set(re.findall(r'OpDecorate\s+(%\S+)\s+NoContraction', assembly))
                adds = set(re.findall(r'(%\S+)\s+=\s+OpFAdd\b', assembly))
                if name.endswith('-precise') and not name.startswith('dp3-'):
                    assert len(adds) == 3 and adds <= decorated, (name, optimizer, adds, decorated)
                modules.append({'environment': environment, 'optimizer': optimizer, 'shader': name,
                                'validated': True, 'no_contraction_adds': len(adds & decorated),
                                'sha256': hashlib.sha256(output.read_bytes()).hexdigest(), 'command': command})
    assert len(modules) == 48
    # CodexAstraLocal: Import/specialize/link the binary itself, not regenerated
    # GLSL. Vulkan validation above is deliberately not called Vulkan execution.
    gl = host.gl
    U, I, P, B = ctypes.c_uint, ctypes.c_int, ctypes.c_void_p, ctypes.c_ubyte
    signatures = {
        'glCreateShader': (U, [U]), 'glShaderBinary': (None, [I, ctypes.POINTER(U), U, P, I]),
        'glSpecializeShader': (None, [U, ctypes.c_char_p, U, ctypes.POINTER(U), ctypes.POINTER(U)]),
        'glGetShaderiv': (None, [U, U, ctypes.POINTER(I)]),
        'glGetShaderInfoLog': (None, [U, I, ctypes.POINTER(I), ctypes.c_char_p]),
        'glCreateProgram': (U, []), 'glAttachShader': (None, [U, U]),
        'glLinkProgram': (None, [U]), 'glGetProgramiv': (None, [U, U, ctypes.POINTER(I)]),
        'glGetProgramInfoLog': (None, [U, I, ctypes.POINTER(I), ctypes.c_char_p]),
        'glUseProgram': (None, [U]), 'glDeleteShader': (None, [U]),
        'glDeleteProgram': (None, [U]), 'glGenVertexArrays': (None, [I, ctypes.POINTER(U)]),
        'glBindVertexArray': (None, [U]), 'glDeleteVertexArrays': (None, [I, ctypes.POINTER(U)]),
        'glBindBuffer': (None, [U, U]), 'glEnableVertexAttribArray': (None, [U]),
        'glVertexAttribPointer': (None, [U, I, U, B, I, P]),
        'glDrawArrays': (None, [U, I, I]),
    }
    assert 'GL_ARB_gl_spirv' in host.ctx.extensions, 'Host lacks required SPIR-V execution support'
    for name, (result, arguments) in signatures.items():
        function = getattr(gl, name)
        function.restype, function.argtypes = result, arguments

    def program(optimizer, route):
        stages = []
        for kind, name in ((0x8B31, 'native' if route == 'native' else f'depth-{route}'), (0x8B30, 'depth')):
            shader = gl.glCreateShader(kind)
            stages.append(shader)
            path = directory / f'opengl-{optimizer}-{name}.spv'
            raw = path.read_bytes()
            handle, data = U(shader), ctypes.create_string_buffer(raw)
            gl.glShaderBinary(1, ctypes.byref(handle), 0x9551, data, len(raw))
            gl.glSpecializeShader(shader, b'main', 0, None, None)
            status, log = I(), ctypes.create_string_buffer(8192)
            gl.glGetShaderiv(shader, 0x8B81, ctypes.byref(status))
            gl.glGetShaderInfoLog(shader, len(log), None, log)
            assert status.value, (path, log.value.decode(errors='replace'))
        result = gl.glCreateProgram()
        for stage in stages:
            gl.glAttachShader(result, stage)
        gl.glLinkProgram(result)
        status, log = I(), ctypes.create_string_buffer(8192)
        gl.glGetProgramiv(result, 0x8B82, ctypes.byref(status))
        gl.glGetProgramInfoLog(result, len(log), None, log)
        assert status.value, log.value.decode(errors='replace')
        for stage in stages:
            gl.glDeleteShader(stage)
        assert gl.glGetError() == 0
        return result

    def vertex_array(route, index):
        buffer = host.ctx.buffer(fixed_geometry(host.directory, route, index))
        vao = U()
        gl.glGenVertexArrays(1, ctypes.byref(vao))
        gl.glBindVertexArray(vao)
        gl.glBindBuffer(0x8892, buffer.glo)
        for attribute in range(1 if route == 'native' else 2):
            gl.glEnableVertexAttribArray(attribute)
            gl.glVertexAttribPointer(attribute, 4, 0x1406, 0, 16 if route == 'native' else 64, P(attribute * 16))
        return vao, buffer

    def draw(program_id, vao):
        gl.glUseProgram(program_id)
        gl.glBindVertexArray(vao)
        gl.glDrawArrays(0x0004, 0, 96)

    execution = []
    for optimizer in ('disabled', 'enabled'):
        programs = {route: program(optimizer, route) for route in ('legacy', 'precise', 'native')}
        for index in (23, 30):
            arrays = {route: vertex_array(route, index) for route in programs}
            draws = {route: (lambda p=programs[route], vao=arrays[route][0]: draw(p, vao)) for route in programs}
            outcomes = {f'{a}->{b}': host.coverage(draws[a], draws[b]) for a, b in pairs}
            check_outcomes(outcomes, index)
            execution.append({'optimizer': optimizer, 'witness': index, 'outcomes': outcomes})
            for vao, buffer in arrays.values():
                gl.glDeleteVertexArrays(1, ctypes.byref(vao))
                buffer.release()
        for handle in programs.values():
            gl.glDeleteProgram(handle)
    return {'modules': modules, 'execution': execution,
            'scope': 'Vulkan validation; actual host GL SPIR-V coverage in both optimizer modes'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/gpu-dot-depth'))
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--render-only', action='store_true')
    parser.add_argument('--require-spirv', action='store_true')
    args = parser.parse_args()
    if not args.render_only:
        compile_cases(args.output)
    if args.render or args.render_only:
        render(args.output, args.require_spirv)
    elif args.require_spirv:
        parser.error('--require-spirv requires --render or --render-only')


if __name__ == '__main__':
    main()
