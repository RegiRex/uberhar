#!/usr/bin/env python3
"""AstraEH: Execute production TEV GLSL on Mesa and compare with specialized GLSL.

This isolates combiner math from sampling and lighting: all four texture
functions return independently randomized colors. Full Vulkan fragment modules
are checked separately with glslang and spirv-val. This is not an Android GPU
or full-renderer test.
"""

from pathlib import Path
import random
import struct
import sys

import moderngl

cases = Path(sys.argv[1])
ctx = moderngl.create_standalone_context(require=430, backend="egl")
print(f"TEV differential test: {ctx.info['GL_RENDERER']}", flush=True)
dynamic = (cases / "dynamic-0.frag").read_text()


def tev_body(source):
    # AstraEH: Extract the actual combiner code, ending before the alpha-test statement.
    start = source.index("vec4 combiner_buffer =")
    end = source.index("if (false) discard;", start)
    return source[start:end] + "return combiner_output;\n"


# AstraEH: Replace only the Vulkan uniform transport; preserve generated interpreter formulas.
helpers = dynamic[dynamic.index("layout(push_constant)"):dynamic.index("void main()")]
helpers = helpers.replace(
    "layout(push_constant) uniform UberTev",
    "layout(std430, binding=1) readonly buffer UberTev",
)
rounding = dynamic[dynamic.index("float byteround("):dynamic.index("float getLod(")]
header = """#version 430
layout(local_size_x=64) in;
layout(std430, binding=0) readonly buffer Inputs { vec4 inputs[]; };
layout(std430, binding=2) writeonly buffer Outputs { uvec4 outputs[]; };
vec4 rounded_primary_color, primary_fragment_color, secondary_fragment_color;
vec4 const_color[6];
vec4 tev_combiner_buffer_color;
vec4 sampleTexUnit0() { return inputs[gl_GlobalInvocationID.x * 14u + 3u]; }
vec4 sampleTexUnit1() { return inputs[gl_GlobalInvocationID.x * 14u + 4u]; }
vec4 sampleTexUnit2() { return inputs[gl_GlobalInvocationID.x * 14u + 5u]; }
vec4 sampleTexUnit3() { return inputs[gl_GlobalInvocationID.x * 14u + 6u]; }
"""
main = """
void main() {
    uint id = gl_GlobalInvocationID.x;
    uint base = id * 14u;
    rounded_primary_color = inputs[base];
    primary_fragment_color = inputs[base + 1u];
    secondary_fragment_color = inputs[base + 2u];
    for (uint i = 0u; i < 6u; ++i) const_color[i] = inputs[base + 7u + i];
    tev_combiner_buffer_color = inputs[base + 13u];
    outputs[id * 2u] = uvec4(round(specialized() * 255.0));
    outputs[id * 2u + 1u] = uvec4(round(interpreted() * 255.0));
}
"""
samples = 256
rng = random.Random(0x55424552)
values = [rng.randrange(256) / 255.0 for _ in range(samples * 14 * 4)]
# AstraEH: Filtered texture samples are not restricted to byte values. Exercise those
# too, while retaining byte-quantized primary/lighting/constant colors.
for sample in range(samples // 2, samples):
    for texture in range(3, 7):
        start = sample * 56 + texture * 4
        values[start:start + 4] = [rng.random() for _ in range(4)]
for sample, value in enumerate([0.0, 1.0, 127 / 255.0, 128 / 255.0]):
    values[sample * 56:(sample + 1) * 56] = [value] * 56
inputs = ctx.buffer(struct.pack(f"<{len(values)}f", *values))
inputs.bind_to_storage_buffer(0)
instructions = ctx.buffer(reserve=112)
instructions.bind_to_storage_buffer(1)
outputs = ctx.buffer(reserve=samples * 2 * 4 * 4)
outputs.bind_to_storage_buffer(2)
count = 0
mismatches = 0
maximum_delta = 0
# AstraEH: Evaluate both paths on identical inputs and require exact quantized RGBA agreement.
for file in sorted(cases.glob("*.bin"), key=lambda p: int(p.stem)):
    specialized = file.with_suffix(".frag").read_text()
    source = (
        header + rounding + helpers
        + "vec4 specialized() {\n" + tev_body(specialized) + "}\n"
        + "vec4 interpreted() {\n" + tev_body(dynamic) + "}\n" + main
    )
    shader = ctx.compute_shader(source)
    instructions.write(file.read_bytes() + bytes(12))
    shader.run(group_x=samples // 64)
    ctx.memory_barrier()
    result = list(struct.iter_unpack("<4I", outputs.read()))
    for sample in range(samples):
        expected, actual = result[sample * 2:sample * 2 + 2]
        if expected != actual:
            mismatches += 1
            delta = max(abs(a - b) for a, b in zip(expected, actual))
            if delta > maximum_delta:
                maximum_delta = delta
                print(
                    f"case {file.stem} sample {sample}: specialized={expected}, dynamic={actual}; "
                    f"new maximum channel delta {delta}", flush=True
                )
    shader.release()
    count += samples
    if int(file.stem) % 32 == 0:
        print(f"Compared {count} outputs", flush=True)
if mismatches:
    raise AssertionError(f"{mismatches}/{count} mismatches; maximum channel delta {maximum_delta}")
print(f"PASS: {count} exact RGBA8 comparisons across {count // samples} six-stage programs")
