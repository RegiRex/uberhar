#!/usr/bin/env python3
"""CodexAstraLocal: Run the real output guard, CPU interpreter and GLSL generator.

The required phase executes production admission with modeled device/cache plumbing.
--render additionally demonstrates zero-versus-one output W in generated shaders on
host Mesa. Neither phase establishes Dark Moon coverage or Adreno image parity.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess


def render(directory: Path) -> None:
    # CodexAstraLocal: Execute the emitted vertex shader unchanged; transform feedback
    # observes its semantic output without modeling a game's fragment stage or GPU.
    import moderngl

    os.environ.setdefault("MESA_SHADER_CACHE_DIR", str(directory / "mesa-cache"))
    context = moderngl.create_standalone_context(require=430, backend="egl")
    target = context.simple_framebuffer((1, 1))
    target.use()
    uniforms = context.buffer(bytes(64))
    uniforms.bind_to_uniform_block(1)
    cases = json.loads((directory / "cases.json").read_text())
    if [case["shader"] for case in cases] != ["missing", "written"]:
        raise AssertionError("Expected both the partial-output divergence and written-W control")
    for case in cases:
        shader = (directory / f'{case["shader"]}.vert').read_text()
        program = context.program(vertex_shader=shader, varyings=["normquat"])
        source = context.buffer(struct.pack("<4f", .125, .5, .75, .875))
        output = context.buffer(reserve=16)
        vao = context.vertex_array(program, [(source, "4f", "vs_in_typed_reg0")])
        vao.transform(output, vertices=1, mode=moderngl.POINTS)
        context.finish()
        actual = struct.unpack("<4f", output.read())
        if actual != (.125, .5, .75, case["gpu_w"]):
            raise AssertionError(f'{case["shader"]}: unexpected GPU output {actual}')
        if (actual[3] != case["cpu_w"]) != (case["shader"] == "missing"):
            raise AssertionError("Expected divergence only when the consumed W was never written")
        vao.release()
        output.release()
        source.release()
        program.release()
    print(f'PASS: generated partial-output W divergence and written-W control on '
          f'{context.info["GL_RENDERER"]}')
    uniforms.release()
    target.release()
    context.release()


def main() -> None:
    # CodexAstraLocal: --source allows the same behavioral regression against the
    # pre-guard production method; no rewritten policy is substituted in the test.
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("src/video_core/renderer_vulkan/vk_rasterizer.cpp"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/gpu-output-guard"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--render-only", action="store_true")
    args = parser.parse_args()
    directory = args.output.parent / f"{args.output.name}-cases"
    if args.render_only:
        render(directory)
        return
    directory.mkdir(parents=True, exist_ok=True)
    source = args.source.read_text()
    start = source.index("bool RasterizerVulkan::AccelerateDrawBatchReady(bool is_indexed)")
    end = source.index("\n}\n", start) + 3
    (directory / "gpu_output_admission.inc").write_text(source[start:end])
    # CodexAstraLocal: shader_gen.cpp does not use its settings include. Keep all
    # generator, bytecode, hash, interpreter and output-conversion definitions real.
    stubs = directory / "stubs"
    (stubs / "common").mkdir(parents=True, exist_ok=True)
    (stubs / "common/settings.h").write_text(
        "// CodexAstraLocal: Unused settings interface in the production generator.\n#pragma once\n")
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] \
        if args.sanitize else ["-O2"]
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", *flags,
                    "-DMICROPROFILE_ENABLED=0", "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL",
                    f"-I{directory}", f"-I{stubs}", "-Isrc", "-Iexternals/fmt/include",
                    "-Iexternals/boost", "-Iexternals/xxHash", "-Iexternals/nihstro/include",
                    "-Iexternals/microprofile", "tools/uberhar/test_gpu_output_guard.cpp",
                    "src/video_core/pica/shader_unit.cpp", "src/video_core/pica/shader_setup.cpp",
                    "src/video_core/pica/output_vertex.cpp",
                    "src/video_core/shader/shader_interpreter.cpp",
                    "src/video_core/shader/generator/glsl_shader_gen.cpp",
                    "src/video_core/shader/generator/glsl_shader_decompiler.cpp",
                    "src/video_core/shader/generator/shader_gen.cpp", "-o", str(args.output)],
                   check=True)
    subprocess.run([str(args.output), str(directory)], check=True)
    if args.render:
        render(directory)


if __name__ == "__main__":
    main()
