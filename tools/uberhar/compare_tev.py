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
    # AstraEH: The generic path now interprets alpha testing after this explicit boundary.
    marker = "// AstraEH: TEV output ends;" if "// AstraEH: TEV output ends;" in source else "if (false) discard;"
    end = source.index(marker, start)
    # AstraEH: Desktop GL does not expose this Vulkan frontend hint. Strip only
    # the hint here; the Vulkan gate verifies DontUnroll survives into SPIR-V.
    return source[start:end].replace("[[dont_unroll]] ", "") + "return combiner_output;\n"


# AstraEH: Replace only the Vulkan uniform transport; preserve generated interpreter formulas.
state_start = dynamic.index("layout(push_constant)")
state_end = dynamic.index("} uber_tev;", state_start) + len("} uber_tev;")
helpers = (dynamic[state_start:state_end] + "\n"
           + dynamic[dynamic.index("// AstraEH: TEV interpreter helpers begin."):dynamic.index("void main()")])
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
// AstraEH: Count interpreter fetches separately from the specialized reference.
uvec4 fetch_counts = uvec4(0u);
vec4 sampleTexUnit0() { ++fetch_counts.x; return inputs[gl_GlobalInvocationID.x * 14u + 3u]; }
vec4 sampleTexUnit1() { ++fetch_counts.y; return inputs[gl_GlobalInvocationID.x * 14u + 4u]; }
vec4 sampleTexUnit2() { ++fetch_counts.z; return inputs[gl_GlobalInvocationID.x * 14u + 5u]; }
vec4 sampleTexUnit3() { ++fetch_counts.w; return inputs[gl_GlobalInvocationID.x * 14u + 6u]; }
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
    outputs[id * 3u] = uvec4(round(specialized() * 255.0));
    fetch_counts = uvec4(0u);
    outputs[id * 3u + 1u] = uvec4(round(interpreted() * 255.0));
    outputs[id * 3u + 2u] = fetch_counts;
}
"""


def expected_fetches(constants):
    """AstraEH: Independently enumerate live texture operands, ignoring dead ones."""
    used = set()
    for stage, (sources, modifiers, ops, scales) in enumerate(
        struct.iter_unpack("<4I", constants[:96])
    ):
        color_op, alpha_op = ops & 15, (ops >> 16) & 15
        if (color_op == alpha_op == 0 and sources & 0x000f000f == 0x000f000f
                and modifiers & 0x0000700f == 0
                and scales & 3 in (0, 3) and (scales >> 16) & 3 in (0, 3)):
            continue
        for channel, operation in ((0, color_op), (16, alpha_op)):
            if channel == 16 and color_op == 7:
                continue
            count = 1 if operation == 0 else 3 if operation in (4, 8, 9) else 2
            for operand in range(count):
                source = (sources >> (channel + 4 * operand)) & 15
                if stage == 0 and source == 15:
                    source = (sources >> (channel + 8)) & 15
                if 3 <= source <= 6:
                    used.add(source - 3)
    return tuple(int(unit in used) for unit in range(4))


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
outputs = ctx.buffer(reserve=samples * 3 * 4 * 4)
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
    constants = file.read_bytes()
    instructions.write(constants + bytes(112 - len(constants)))
    fetches = expected_fetches(constants)
    shader.run(group_x=samples // 64)
    ctx.memory_barrier()
    result = list(struct.iter_unpack("<4I", outputs.read()))
    for sample in range(samples):
        expected, actual, sampled = result[sample * 3:sample * 3 + 3]
        if sampled != fetches:
            raise AssertionError(
                f"case {file.stem} sample {sample}: texture fetch counts {sampled}, expected {fetches}"
            )
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
print(f"PASS: {count} texture-use checks; exactly one fetch per referenced TEV texture unit")
