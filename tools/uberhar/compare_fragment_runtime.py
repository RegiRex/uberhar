#!/usr/bin/env python3
"""AstraEH: Compare complete production fragment color/depth and discard behavior.

Mesa OpenGL provides the offscreen execution environment. Only descriptor-set,
push-constant and texel-buffer transport are adapted; fragment math and real
2D/projected sampling remain generated production GLSL. This is not an Adreno test.
"""
from pathlib import Path
import re
from collections import OrderedDict
import struct
import sys
import moderngl

# CodexAstraLocal: This finite qualification retains complete production fragment
# execution; the inherited adapter only translates Vulkan transport for host GL.
import argparse, hashlib, json, os
parser = argparse.ArgumentParser()
parser.add_argument("cases", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--defect", choices=("constant_result", "canonical_direction"))
args = parser.parse_args()
cases = args.cases
args.output.mkdir(parents=True, exist_ok=False)
os.environ["MESA_SHADER_CACHE_DIR"] = str(args.output / "mesa-cache")
counts = json.loads((cases / "counts.json").read_text())
assert counts["cases"] == 144 and counts["rgb"] >= 13 and counts["alpha"] >= 13 and counts["canonical_slots"] > 10
case_results = []
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
# AstraEH: Separate table contents reveal incorrect physical-light LUT selection.
lut_values = [v for table in range(24) for i in range(256) for v in
              (0.05 + (table + 1) / 32 * i / 255, (table + 1) / (32 * 255), 0.4, 0.6)]
lut = ctx.texture((24 * 256, 1), 4, struct.pack(f"<{len(lut_values)}f", *lut_values), dtype="f4")
for unit in range(3, 6):
    lut.use(unit)
uniforms = ctx.buffer(reserve=0x530)
uniforms.bind_to_uniform_block(2)
state = ctx.buffer(reserve=128)
state.bind_to_storage_buffer(1)
color = ctx.texture((32, 32), 4, dtype="f1")
depth = ctx.depth_texture((32, 32))
target = ctx.framebuffer([color], depth_attachment=depth)
target.use()
ctx.enable(moderngl.DEPTH_TEST)
ctx.depth_func = "<="
# AstraPro: Bound the oracle's driver objects so the complete corpus can run on
# memory-limited workers. Eviction changes only host test-object lifetime, never
# shader source, state cases, expected pixels, or the pass/fail comparison.
programs = OrderedDict()
MAX_RESIDENT_PROGRAMS = 16


def draw(path):
    source = adapt(path.read_text())
    # CodexAstraLocal: Intentional arithmetic defects must be caught by a real
    # rendered output mismatch, not a compile error or an unexecuted predicate.
    if args.defect == "canonical_direction" and "-generic" in path.name:
        source = source.replace("light_vector = vec3(0.0, 0.0, 1.0); light_distance = 1.0;",
                                "light_vector = vec3(1.0, 0.0, 0.0); light_distance = 1.0;")
    if args.defect == "constant_result" and "-generic" in path.name:
        source = source.replace("float((instruction.z >> 15u) & 1u)", "1.0 - float((instruction.z >> 15u) & 1u)")
    if source not in programs:
        if len(programs) >= MAX_RESIDENT_PROGRAMS:
            _, (old_program, old_vao) = programs.popitem(last=False)
            old_vao.release()
            old_program.release()
        program = ctx.program(vertex_shader=vertex, fragment_shader=source)
        programs[source] = (program, ctx.vertex_array(program, []))
    programs.move_to_end(source)
    program, vao = programs[source]
    target.clear(0.37, 0.19, 0.53, 0.71, depth=1.0)
    vao.render(mode=moderngl.TRIANGLES, vertices=3)
    return target.read(components=4, alignment=1), depth.read(alignment=1)


# CodexAstraLocal: Every candidate uses the same mutable uniforms and state as
# the independent ordinary-formula full shader. State changes under identical
# module structure are preserved, and OFF is compared as a separate path.
# CodexAstraLocal: Compare original normalized vector bits and consumed distance
# before final RGBA quantization can hide a change. Extract both code blocks from
# actual generated GLSL; only the uniform transport and shader entry are adapted.
def direction_body(path):
    source = path.read_text()
    start = source.index("light_vector = (uber_light_ops")
    end = source.index("spot_dir =", start)
    return source[start:end]
base = (cases / "96-generic.frag").read_text()
start = base.index("layout(push_constant)")
end = base.index("} uber_tev;", start) + len("} uber_tev;")
transport = base[start:end].replace("layout(push_constant) uniform UberTev", "layout(std430,binding=1) readonly buffer UberTev")
normal_header = """#version 450
layout(local_size_x=8) in;
layout(std430,binding=0) readonly buffer Directions { vec4 directions[8]; };
layout(std430,binding=2) writeonly buffer Normalized { uvec4 results[]; };
struct Light { vec3 position; }; Light light_src[8];
""" + transport
normal_tail = """
void main(){
uint slot=gl_GlobalInvocationID.x;
uint uber_light_slot=slot;
uint uber_light=(uber_tev.lighting_sources >> (slot*3u)) & 7u;
uint word=slot<4u?uber_tev.lighting_ops_lo:uber_tev.lighting_ops_hi;
uint uber_light_ops=(word >> ((slot & 3u)*7u)) & 127u;
for(uint i=0u;i<8u;++i)light_src[i].position=directions[i].xyz;
vec3 view=vec3(0.1,0.2,1.0),light_vector;float light_distance=0.0;
"""
normal_source = (normal_header + normal_tail + direction_body(cases / "96-off.frag") +
    "results[slot*2u]=floatBitsToUint(vec4(light_vector,(uber_light_ops&4u)!=0u?light_distance:0.0));\n" +
    direction_body(cases / "96-generic.frag") +
    "results[slot*2u+1u]=floatBitsToUint(vec4(light_vector,(uber_light_ops&4u)!=0u?light_distance:0.0));}\n")
(args.output / "direction.comp").write_text(normal_source)
normal_shader=ctx.compute_shader(normal_source)
normal_inputs=ctx.buffer(reserve=128);normal_inputs.bind_to_storage_buffer(0)
normal_outputs=ctx.buffer(reserve=256);normal_outputs.bind_to_storage_buffer(2)
normal_vectors=0
for i in range(96,144):
    normal_inputs.write((cases / f"{i}-directions.bin").read_bytes())
    state.write((cases / f"{i}-state.bin").read_bytes())
    normal_shader.run(group_x=1);ctx.memory_barrier()
    rows=list(struct.iter_unpack("<4I",normal_outputs.read()))
    for slot in range(1+i%4):
        assert rows[slot*2] == rows[slot*2+1], ("normalized vector/distance bits", i,slot,rows[slot*2:slot*2+2])
        normal_vectors+=1
normal_shader.release();normal_inputs.release();normal_outputs.release()

count = 0
for i in range(counts["cases"]):
    uniforms.write((cases / f"{i}-uniforms.bin").read_bytes())
    raw = (cases / f"{i}-state.bin").read_bytes()
    assert len(raw) == 128
    state.write(raw)
    expected_color, expected_depth = draw(cases / f"{i}-specialized.frag")
    row = {"case": i, "reference": hashlib.sha256(expected_color + expected_depth).hexdigest()}
    # CodexAstraLocal: Independent liveness OFF must retain arithmetic/memo
    # consumers; compare exact output instead of treating all-OFF as equivalent.
    for route in ("generic", "partial", "off", "liveness-off"):
        state.write((cases / f"{i}-{route}-state.bin").read_bytes()
                    if route in ("off", "liveness-off") else raw)
        actual_color, actual_depth = draw(cases / f"{i}-{route}.frag")
        expected = struct.unpack("<1024f", expected_depth)
        actual = struct.unpack("<1024f", actual_depth)
        delta = max(abs(a-b) for a,b in zip(expected, actual))
        if expected_color != actual_color or delta > 1e-6:
            failure = {"case": i, "route": route, "defect": args.defect,
                       "color_bytes": sum(a != b for a,b in zip(expected_color, actual_color)), "depth_delta": delta}
            (args.output / "failure.json").write_text(json.dumps(failure, indent=2))
            (args.output / "expected-color.bin").write_bytes(expected_color)
            (args.output / "actual-color.bin").write_bytes(actual_color)
            raise AssertionError(f"Runtime fragment output mismatch: {failure}")
        row[route] = hashlib.sha256(actual_color + actual_depth).hexdigest()
        count += 1024
    case_results.append(row)
    if i % 24 == 23: print(f"Compared {i+1}/144 runtime fragment states", flush=True)
if args.defect: raise AssertionError("Intentional runtime defect escaped")
report = {"renderer": ctx.info["GL_RENDERER"], "cases": counts["cases"], "exact_rgba8_pixels": count,
          "depth_comparisons": count, "normalized_vectors_exact_bits": normal_vectors, "preparation": counts, "results": case_results,
          "scope": "Actual generated GLSL on host GL; Vulkan transport adapted; hardware timing and Adreno qualification remain pending"}
(args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"PASS runtime fragments: {count} exact RGBA8 comparisons, original/generic/static/OFF", flush=True)

# AstraPro: Explicitly release remaining test objects after the full comparison.
for program, vao in programs.values():
    vao.release()
    program.release()
