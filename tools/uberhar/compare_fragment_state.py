#!/usr/bin/env python3
"""AstraEH: Compare complete production fragment color/depth and discard behavior.

Mesa OpenGL provides the offscreen execution environment. Only descriptor-set,
push-constant and texel-buffer transport are adapted; fragment math and real
2D/projected sampling remain generated production GLSL. This is not an Adreno test.
"""
from pathlib import Path
import re
import struct
import sys
import moderngl

cases = Path(sys.argv[1])
ctx = moderngl.create_standalone_context(require=450, backend="egl")
print(f"Full fragment state test: {ctx.info['GL_RENDERER']}", flush=True)
vertex = """#version 450
layout(location=1) out vec4 primary_color;
layout(location=2) out vec2 texcoord0;
layout(location=3) out vec2 texcoord1;
layout(location=4) out vec2 texcoord2;
layout(location=5) out float texcoord0_w;
layout(location=6) out vec4 normquat;
layout(location=7) out vec3 view;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(p * 2.0, 0.8, 2.0);
    texcoord0 = p * 0.75 + 0.5;
    texcoord1 = p.yx * 0.65 + 0.5;
    texcoord2 = vec2(1.0) - texcoord0;
    texcoord0_w = 1.5;
    primary_color = vec4(0.4, 0.6, 0.8, 0.5);
    normquat = vec4(0.0, 0.0, 0.0, 1.0);
    view = vec3(0.1, 0.2, 1.0);
}
"""


def adapt(source):
    # AstraEH: Keep production calculations intact while replacing Vulkan-only transport.
    source = source.replace("#extension GL_EXT_control_flow_attributes : require", "")
    source = source.replace("[[dont_unroll]] ", "")
    source = re.sub(r"set\s*=\s*\d+\s*,\s*", "", source)
    source = source.replace("layout(push_constant) uniform UberTev",
                            "layout(std430, binding=1) readonly buffer UberTev")
    # AstraEH: A one-row 2D texture substitutes for each texel buffer, preserving
    # integer indexing and texelFetch (no filtering). Regular game textures are real samplers.
    for name in ("texture_buffer_lut_lf", "texture_buffer_lut_rg", "texture_buffer_lut_rgba"):
        source = source.replace(f"uniform samplerBuffer {name};", f"uniform sampler2D {name};")
        source = source.replace(f"texelFetch({name},", f"fetch_{name}(")
        declaration = f"uniform sampler2D {name};"
        source = source.replace(declaration, declaration +
            f"\nvec4 fetch_{name}(int i) {{ return texelFetch({name}, ivec2(i, 0), 0); }}")
    return source


# AstraEH: Nonconstant texels exercise actual coordinates, projected lookup,
# interpolation, border returns and alpha-test thresholds in the complete shader.
textures = []
for unit in range(3):
    data = bytes(v for y in range(16) for x in range(16) for v in
                 ((x * 17 + unit * 31) % 256, (y * 17 + unit * 47) % 256,
                  ((x + y) * 13) % 256, (0, 127, 128, 255)[(x + y + unit) % 4]))
    tex = ctx.texture((16, 16), 4, data)
    tex.build_mipmaps()
    tex.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
    tex.use(unit)
    textures.append(tex)
lut_values = [v for i in range(256) for v in (i / 255, 1 / 255, 0.4, 0.6)]
lut = ctx.texture((256, 1), 4, struct.pack("<1024f", *lut_values), dtype="f4")
for unit in range(3, 6):
    lut.use(unit)
uniforms = ctx.buffer(reserve=0x530)
uniforms.bind_to_uniform_block(2)
state = ctx.buffer(reserve=112)
state.bind_to_storage_buffer(1)
color = ctx.texture((32, 32), 4, dtype="f1")
depth = ctx.depth_texture((32, 32))
target = ctx.framebuffer([color], depth_attachment=depth)
target.use()
ctx.enable(moderngl.DEPTH_TEST)
ctx.depth_func = "<="
programs = {}


def draw(path):
    source = adapt(path.read_text())
    if source not in programs:
        program = ctx.program(vertex_shader=vertex, fragment_shader=source)
        programs[source] = (program, ctx.vertex_array(program, []))
    program, vao = programs[source]
    target.clear(0.37, 0.19, 0.53, 0.71, depth=1.0)
    vao.render(mode=moderngl.TRIANGLES, vertices=3)
    return target.read(components=4, alignment=1), depth.read(alignment=1)


count = 0
for i in range(192):
    uniforms.write((cases / f"{i}-uniforms.bin").read_bytes())
    raw = (cases / f"{i}-state.bin").read_bytes()
    if len(raw) != 108:
        raise AssertionError(f"Unexpected runtime ABI size: {len(raw)}")
    state.write(raw + bytes(112 - len(raw)))
    expected_color, expected_depth = draw(cases / f"{i}-specialized.frag")
    actual_color, actual_depth = draw(cases / f"{i}-generic.frag")
    if expected_color != actual_color:
        changed = sum(a != b for a, b in zip(expected_color, actual_color))
        raise AssertionError(f"Case {i}: {changed} differing RGBA8 components")
    expected = struct.unpack("<1024f", expected_depth)
    actual = struct.unpack("<1024f", actual_depth)
    if max(abs(a - b) for a, b in zip(expected, actual)) > 1e-6:
        raise AssertionError(f"Case {i}: depth/discard mismatch")
    count += 1024
    if i % 48 == 47:
        print(f"Compared {i + 1} full fragment states", flush=True)
print(f"PASS: {count} exact RGBA8 pixels and {count} depth/discard comparisons across 192 states", flush=True)
