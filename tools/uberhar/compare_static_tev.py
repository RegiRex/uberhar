#!/usr/bin/env python3
"""CodexAstraLocal: Compare actual partial/generic/specialized TEV and independent fetch counts."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import random
import struct

import moderngl


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--defect", choices=("wrong_plan", "missing_buffer_advance", "duplicate_fetch"))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    cases = args.cases.resolve()
    root = Path(__file__).resolve().parents[2]
    original = root / "tools/uberhar/compare_tev.py"
    tree = ast.parse(original.read_text())
    # CodexAstraLocal: Retain the existing independently raw-register-derived fetch
    # oracle and exact combiner extraction, without importing its executable driver.
    definitions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                   and node.name in ("tev_body", "expected_fetches")]
    if len(definitions) != 2:
        raise AssertionError("Expected exact two established reference helpers")
    namespace = {"struct": struct}
    exec(compile(ast.Module(body=definitions, type_ignores=[]), str(original), "exec"), namespace)
    tev_body, expected_fetches = namespace["tev_body"], namespace["expected_fetches"]
    # CodexAstraLocal: Keep optional compiler disk cache local to this retained proof.
    os.environ['MESA_SHADER_CACHE_DIR'] = str(out / 'mesa-cache')
    ctx = moderngl.create_standalone_context(require=430, backend="egl")
    dynamic = (cases / "dynamic-0.frag").read_text()
    state_start = dynamic.index("layout(push_constant)")
    state_end = dynamic.index("} uber_tev;", state_start) + len("} uber_tev;")
    helpers = (dynamic[state_start:state_end] + "\n"
               + dynamic[dynamic.index("// AstraEH: TEV interpreter helpers begin."):dynamic.index("void main()")])
    helpers = helpers.replace("layout(push_constant) uniform UberTev",
                              "layout(std430, binding=1) readonly buffer UberTev")
    rounding = dynamic[dynamic.index("float byteround("):dynamic.index("float getLod(")]
    header = r'''#version 430
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Inputs { vec4 inputs[]; };
layout(std430,binding=2) writeonly buffer Outputs { uvec4 outputs[]; };
vec4 rounded_primary_color, primary_fragment_color, secondary_fragment_color;
vec4 const_color[6];
vec4 tev_combiner_buffer_color;
// CodexAstraLocal: Counts expose lost lazy sampling separately from color parity.
uvec4 fetch_counts = uvec4(0u);
vec4 sampleTexUnit0() { ++fetch_counts.x; return inputs[gl_GlobalInvocationID.x*14u+3u]; }
vec4 sampleTexUnit1() { ++fetch_counts.y; return inputs[gl_GlobalInvocationID.x*14u+4u]; }
vec4 sampleTexUnit2() { ++fetch_counts.z; return inputs[gl_GlobalInvocationID.x*14u+5u]; }
vec4 sampleTexUnit3() { ++fetch_counts.w; return inputs[gl_GlobalInvocationID.x*14u+6u]; }
'''
    tail = r'''
void main() {
    uint id=gl_GlobalInvocationID.x, base=id*14u;
    rounded_primary_color=inputs[base]; primary_fragment_color=inputs[base+1u];
    secondary_fragment_color=inputs[base+2u];
    for(uint i=0u;i<6u;++i) const_color[i]=inputs[base+7u+i];
    tev_combiner_buffer_color=inputs[base+13u];
    outputs[id*5u]=uvec4(round(specialized()*255.0));
    fetch_counts=uvec4(0u);
    outputs[id*5u+1u]=uvec4(round(interpreted()*255.0));
    outputs[id*5u+2u]=fetch_counts;
    fetch_counts=uvec4(0u);
    outputs[id*5u+3u]=uvec4(round(partial()*255.0));
    outputs[id*5u+4u]=fetch_counts;
}
'''
    samples = 256
    rng = random.Random(0x55424552)
    values = [rng.randrange(256) / 255.0 for _ in range(samples * 14 * 4)]
    for sample in range(samples // 2, samples):
        for texture in range(3, 7):
            start = sample * 56 + texture * 4
            values[start:start + 4] = [rng.random() for _ in range(4)]
    for sample, value in enumerate([0.0, 1.0, 127 / 255.0, 128 / 255.0]):
        values[sample*56:(sample+1)*56] = [value] * 56
    inputs = ctx.buffer(struct.pack(f"<{len(values)}f", *values))
    inputs.bind_to_storage_buffer(0)
    instructions = ctx.buffer(reserve=128)
    instructions.bind_to_storage_buffer(1)
    outputs = ctx.buffer(reserve=samples * 5 * 16)
    outputs.bind_to_storage_buffer(2)
    files = sorted(cases.glob("*.bin"), key=lambda p: int(p.stem))
    if [int(file.stem) for file in files] != list(range(816)):
        raise AssertionError("Expected all816 TEV cases exactly once")
    report = {"author": "CodexAstraLocal", "renderer": ctx.info["GL_RENDERER"],
              "scope": "host GL extracted TEV; production formulas with synthetic samples",
              "defect": args.defect, "cases": 0, "three_way_rgba_vectors": 0,
              "fetch_vectors_per_candidate": 0, "source_sha256": {str(original): digest(original),
              str(Path(__file__).resolve()): digest(Path(__file__).resolve())}, "case_artifacts": {}}
    # CodexAstraLocal: Finite defects alter the actual emitted candidate body or
    # its selected plan; they must fail an output/count assertion, not compilation.
    for file in files:
        index = int(file.stem)
        reference = file.with_suffix(".frag")
        candidate = cases / f"{(index + 1) % 816 if args.defect == 'wrong_plan' else index}-partial.frag"
        body = tev_body(candidate.read_text())
        if args.defect == "missing_buffer_advance":
            body = body.replace("combiner_buffer = next_combiner_buffer;", "")
        elif args.defect == "duplicate_fetch":
            for unit in range(4):
                needle = f"vec4 uber_static_texel{unit} = sampleTexUnit{unit}();"
                body = body.replace(needle, f"sampleTexUnit{unit}();\n" + needle)
        source = (header + rounding + helpers
                  + "vec4 specialized(){\n" + tev_body(reference.read_text()) + "}\n"
                  + "vec4 interpreted(){\n" + tev_body(dynamic) + "}\n"
                  + "vec4 partial(){\n" + body + "}\n" + tail)
        shader = ctx.compute_shader(source)
        constants = file.read_bytes()
        raw = file.with_suffix(".raw").read_bytes()
        if len(constants) != 128 or len(raw) != 96:
            raise AssertionError("Invalid source transport sizes")
        instructions.write(constants)
        shader.run(group_x=samples // 64)
        ctx.memory_barrier()
        result = outputs.read()
        rows = list(struct.iter_unpack("<4I", result))
        fetches = expected_fetches(raw)
        for sample in range(samples):
            specialized, generic, generic_fetches, partial, partial_fetches = rows[sample*5:sample*5+5]
            if specialized != generic or generic != partial or generic_fetches != fetches or partial_fetches != fetches:
                failure = {"case":index,"sample":sample,"specialized":specialized,
                           "generic":generic,"partial":partial,"expected_fetches":fetches,
                           "generic_fetches":generic_fetches,"partial_fetches":partial_fetches}
                (out / "failure.json").write_text(json.dumps(failure,indent=2)+"\n")
                (out / "failed.comp").write_text(source)
                (out / "failed-output.bin").write_bytes(result)
                raise AssertionError(f"TEV output/fetch mismatch: {failure}")
        shader.release()
        report["cases"] += 1
        report["three_way_rgba_vectors"] += samples
        report["fetch_vectors_per_candidate"] += samples
        report["case_artifacts"][str(index)] = {"reference":digest(reference),"partial":digest(candidate),
                                               "state":digest(file),"raw":digest(file.with_suffix('.raw')),
                                               "executed_source":hashlib.sha256(source.encode()).hexdigest(),
                                               "output":hashlib.sha256(result).hexdigest()}
        if index % 48 == 47:
            print(f"Compared {index+1}/816 three-way TEV programs",flush=True)
    (out / "report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(f"PASS {report['cases']} programs / {report['three_way_rgba_vectors']} exact three-way RGBA vectors",flush=True)
    if args.defect:
        raise AssertionError("Intentional defect escaped")


if __name__ == "__main__":
    main()
