#!/usr/bin/env python3
"""AstraEH: Compare production compute output to actual native triangle rasterization.

The shader is unchanged except for Vulkan descriptor/push-constant transport.
This tests Mesa offscreen execution, not Adreno performance or Android integration.
"""
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys
import moderngl

source = Path("src/video_core/renderer_vulkan/uberhar_compute_rect_shader.h").read_text()
source = source.split('R"glsl(', 1)[1].split(')glsl"', 1)[0]
output = Path("build/uberhar-probe/compute")
output.mkdir(parents=True, exist_ok=True)
(output / "rect.comp").write_text(source)
validator = shutil.which("glslangValidator")
if validator:
    subprocess.run([validator, "-V", "--target-env", "vulkan1.1", "-S", "comp",
                    str(output / "rect.comp"), "-o", str(output / "rect.spv")], check=True)
    subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(output / "rect.spv")], check=True)
elif "--require-vulkan" in sys.argv:
    raise SystemExit("Vulkan validators are mandatory for the release gate")

ctx = moderngl.create_standalone_context(require=450, backend="egl")
source = re.sub(r"set\s*=\s*0\s*,\s*", "", source)
source = source.replace("layout(push_constant) uniform RectState",
                        "layout(std430, binding=1) readonly buffer RectState")
compute = ctx.compute_shader(source)
native = ctx.program(vertex_shader="""#version 450
in vec2 position;
void main() { gl_Position = vec4(position, 0.0, 1.0); }
""", fragment_shader="""#version 450
uniform vec4 color;
out vec4 frag_color;
void main() { frag_color = color; }
""")
w, h = 67, 43
size = w * h * 4
seed = random.Random(1009)
reference = ctx.texture((w, h), 4, dtype="f1")
target = ctx.texture((w, h), 1, dtype="u4")
target.bind_to_image(0, read=False, write=True)
framebuffer = ctx.framebuffer([reference])
framebuffer.use()
ctx.viewport = (0, 0, w, h)
ctx.disable(moderngl.BLEND | moderngl.DEPTH_TEST | moderngl.CULL_FACE)
state = ctx.buffer(reserve=32)
state.bind_to_storage_buffer(1)
vertex_buffer = ctx.buffer(reserve=48)
vao = ctx.simple_vertex_array(native, vertex_buffer, "position")

def draw(rect, rgba):
    x, y, rw, rh = rect
    color = int.from_bytes(bytes(rgba), "little")
    state.write(struct.pack("4i4I", *rect, color, 0, 0, 0))
    compute.run((rw + 7) // 8, (rh + 7) // 8, 1)
    ctx.memory_barrier()
    x0, x1 = x / w * 2 - 1, (x + rw) / w * 2 - 1
    y0, y1 = y / h * 2 - 1, (y + rh) / h * 2 - 1
    vertex_buffer.write(struct.pack("12f", x0,y0,x1,y0,x1,y1,x0,y0,x1,y1,x0,y1))
    native["color"].value = tuple(c / 255 for c in rgba)
    vao.render(moderngl.TRIANGLES)

for case in range(256):
    background = bytes(seed.randrange(256) for _ in range(size))
    target.write(background)
    reference.write(background)
    # AstraEH: Successive overlapping draws also test preservation and ordered overwrite.
    for _ in range(1 + case % 4):
        rect = (seed.randrange(-3,w), seed.randrange(-3,h), seed.randrange(1,w+5), seed.randrange(1,h+5))
        draw(rect, [seed.randrange(256) for _ in range(4)])
    expected, actual = reference.read(), target.read()
    if expected != actual:
        differing = sum(a != b for a,b in zip(actual, expected))
        raise AssertionError(f"case {case}: {differing} differing bytes")
print(f"PASS: 256 compute/native pixel comparisons with clipping, partial workgroups, ordered overlaps; {ctx.info['GL_RENDERER']}")
