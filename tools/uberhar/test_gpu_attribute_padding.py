#!/usr/bin/env python3
"""CodexAstraUlt: Compare GPU vertex component padding with production CPU inputs.

Compile the actual native loader, interpreter, GLSL generator and extracted
Vulkan extra-config selection. Only device format capabilities are modeled.
The required host phase checks CPU results and generated assignments; --render
also executes those shaders with Mesa transform feedback. This does not emulate
Android vertex fetching or establish which formats a game uses on its device.
"""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


# CodexAstraUlt: Keep real PICA decoding, shader execution and code generation;
# substitute just the queried device capabilities and unused settings include.
HEADER = r'''
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nihstro/inline_assembly.h>
#include "common/logging/log.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/shader_interpreter.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
}

namespace Vulkan {
using namespace Pica::Shader::Generator;
struct FormatTraits {
    bool needs_conversion{}, needs_emulation{};
};
struct TestDevice {
    FormatTraits traits;
    bool UseGeometryShaders() const { return false; }
    bool IsFragmentShaderBarycentricSupported() const { return false; }
    bool IsShaderClipDistanceSupported() const { return false; }
    const FormatTraits& GetTraits(Pica::PipelineRegs::VertexAttributeFormat, u32) const {
        return traits;
    }
};
struct PipelineCache {
    TestDevice instance;
    struct { bool enable_accurate_mul = true; } profile;
    // CodexAstraLocal: The extracted production selector also scopes precise
    // DP4/DPH generation to immutable GPU eligibility and an enabled CPU JIT.
    // An optional CPU-fragment worker does not grant this GPU capability.
    void* ready_vertex_worker{};
    bool allow_ready_gpu_vertices{};
    bool shader_jit_enabled{};
    ExtraVSConfig CalcExtraConfig(const PicaVSConfig& config);
};
'''


TESTS = r'''
} // namespace Vulkan

using namespace Pica;
using namespace Pica::Shader::Generator;
using Format = PipelineRegs::VertexAttributeFormat;

void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

template <typename T>
void Pack(std::vector<u8>& bytes, const std::array<float, 4>& values, u32 elements) {
    bytes.resize(elements * sizeof(T));
    for (u32 c = 0; c < elements; ++c) {
        const T value = static_cast<T>(values[c]);
        std::memcpy(bytes.data() + c * sizeof(T), &value, sizeof(T));
    }
}

int main(int argc, char** argv) {
    Check(argc == 2, "missing output directory");
    const std::filesystem::path directory{argv[1]};
    std::ofstream manifest{directory / "cases.json"};
    manifest << "[\n";
    using O = nihstro::OpCode::Id;
    using D = nihstro::DestRegister;
    using S = nihstro::SourceRegister;
    ShaderSetup setup;
    const auto binary = nihstro::InlineAsm::CompileToRawBinary(
        {{O::MOV, D::MakeOutput(0), S::MakeInput(0)}, {O::END}});
    for (u32 i = 0; i < binary.program.size(); ++i)
        setup.UpdateProgramCode(i, binary.program[i].hex);
    for (u32 i = 0; i < binary.swizzle_table.size(); ++i)
        setup.UpdateSwizzleData(i, binary.swizzle_table[i].hex);

    RegsInternal regs{};
    regs.vs.output_mask.Assign(1);
    regs.vs.max_input_attribute_index.Assign(0);
    regs.rasterizer.vs_output_total.Assign(1);
    using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;
    auto& map = regs.rasterizer.vs_output_attributes[0];
    map.map_x.Assign(Semantic::QUATERNION_X);
    map.map_y.Assign(Semantic::QUATERNION_Y);
    map.map_z.Assign(Semantic::QUATERNION_Z);
    map.map_w.Assign(Semantic::QUATERNION_W);
    Shader::InterpreterEngine engine;
    engine.SetupBatch(setup, 0);

    u32 cases = 0;
    for (Format format : {Format::BYTE, Format::UBYTE, Format::SHORT, Format::FLOAT}) {
        // CodexAstraUlt: Native xyz and xyzw are positive controls. Only the
        // unsupported xyz path fetches an unrelated fourth lane and replaces it.
        for (u32 elements : {3U, 4U}) {
            for (bool emulated : {false, true}) {
                if (emulated && elements != 3)
                    continue;
                for (bool converted : {false, true}) {
                    if (converted && format == Format::FLOAT)
                        continue;
                    const std::array<float, 4> values = format == Format::UBYTE
                        ? std::array<float, 4>{2, 253, 4, 17}
                        : std::array<float, 4>{2, -3, 4, -17};
                    std::vector<u8> memory;
                    switch (format) {
                    case Format::BYTE: Pack<s8>(memory, values, elements); break;
                    case Format::UBYTE: Pack<u8>(memory, values, elements); break;
                    case Format::SHORT: Pack<s16>(memory, values, elements); break;
                    case Format::FLOAT: Pack<float>(memory, values, elements); break;
                    }
                    NativeVertexInputPlan plan;
                    const NativeInputAttribute description{
                        0, static_cast<u32>(memory.size()), elements, format, false};
                    Check(plan.Prepare(regs.vs, 1, 0, 0,
                        [&](u32) { return description; },
                        [&](PAddr) { return std::span<const u8>{memory}; }) ==
                        NativeVertexInputPlan::Result::Ready, "CPU preparation failed");
                    ShaderUnit unit;
                    AttributeBuffer defaults{};
                    plan.Load(unit, defaults, 0);
                    engine.Run(setup, unit);
                    std::array<float, 4> expected;
                    for (u32 c = 0; c < 4; ++c) {
                        expected[c] = unit.output[0][0][c].ToFloat32();
                        Check(expected[c] == (c < elements ? values[c] : 1.f),
                              "CPU loading/interpreter changed the component value");
                    }

                    PicaVSConfig config{regs, setup};
                    config.state.used_input_vertex_attributes = 1;
                    config.state.input_vertex_attributes[0] = {
                        0, static_cast<u8>(format), static_cast<u8>(elements)};
                    Vulkan::PipelineCache cache;
                    cache.instance.traits = {converted, emulated};
                    const auto extra = cache.CalcExtraConfig(config);
                    // CodexAstraLocal: Exercise the exact extracted selector's
                    // two immutable gates without changing the padding fixture.
                    for (bool ready : {false, true}) for (bool jit : {false, true})
                    for (bool worker : {false, true}) {
                        // CodexAstraLocal: Keep worker presence independent so
                        // Native's CPU-only compiler cannot change dot generation.
                        cache.ready_vertex_worker = worker ? &cache : nullptr;
                        cache.allow_ready_gpu_vertices = ready;
                        cache.shader_jit_enabled = jit;
                        Check(bool(cache.CalcExtraConfig(config).precise_jit_dot) == (ready && jit),
                              "precise JIT arithmetic escaped the Combo/CPU-JIT gates");
                    }
                    cache.ready_vertex_worker = nullptr;
                    cache.allow_ready_gpu_vertices = false;
                    cache.shader_jit_enabled = false;
                    const auto generated = GLSL::GenerateVertexShader(setup, config, extra);
                    Check(!generated.empty(), "shader generation failed");
                    // CodexAstraUlt: Without a graphics context this gate still
                    // rejects the old emitted w=0. --render checks its execution.
                    const bool pads_one = generated.find("vs_in_reg0.w = 1;") != std::string::npos;
                    Check(pads_one == emulated, "GPU padding must assign one only for emulated xyz");
                    Check(generated.find("vs_in_reg0.w = 0;") == std::string::npos,
                          "GPU emitted the old missing-w zero");

                    const char type = converted ? (format == Format::UBYTE ? 'I' : 'i') : 'f';
                    const std::string declaration = type == 'f' ? "in vec4 vs_in_typed_reg0"
                        : type == 'I' ? "in uvec4 vs_in_typed_reg0" : "in ivec4 vs_in_typed_reg0";
                    Check(generated.find(declaration) != std::string::npos,
                          "GPU input cast does not match the queried format traits");
                    std::ofstream{directory / (std::to_string(cases) + ".vert")}
                        << "#version 430\n" << generated;
                    std::array<float, 4> fetched = expected;
                    if (emulated)
                        fetched[3] = 91; // Different from zero, one and the ordinary xyzw input.
                    if (cases)
                        manifest << ",\n";
                    manifest << "{\"id\":" << cases << ",\"type\":\"" << type
                             << "\",\"emulated\":" << (emulated ? "true" : "false")
                             << ",\"input\":[";
                    for (u32 c = 0; c < 4; ++c)
                        manifest << (c ? "," : "") << fetched[c];
                    manifest << "],\"expected\":[";
                    for (u32 c = 0; c < 4; ++c)
                        manifest << (c ? "," : "") << expected[c];
                    manifest << "]}";
                    ++cases;
                }
            }
        }
    }
    manifest << "\n]\n";
    Check(cases == 21, "format/padding case coverage changed");
    std::printf("PASS: %u production CPU/interpreter and generated GPU padding cases; "
                "native xyz/xyzw, emulated xyz, scaled and integer-cast controls\n", cases);
}
'''


def render(cases_dir: Path) -> None:
    # CodexAstraUlt: Execute the generated vertex stage unchanged on host Mesa.
    # Four fetched lanes model Vulkan's format expansion; this is not a Vulkan
    # vertex-fetch test. CPU expectations come from the compiled production path.
    import moderngl

    os.environ.setdefault("MESA_SHADER_CACHE_DIR", str(cases_dir / "mesa-cache"))
    ctx = moderngl.create_standalone_context(require=430, backend="egl")
    fbo = ctx.simple_framebuffer((1, 1))
    fbo.use()
    zeros = ctx.buffer(bytes(64))
    zeros.bind_to_uniform_block(1)
    cases = json.loads((cases_dir / "cases.json").read_text())
    if [case["id"] for case in cases] != list(range(21)):
        raise AssertionError("Expected all 21 cases exactly once")
    for case in cases:
        program = ctx.program(vertex_shader=(cases_dir / f'{case["id"]}.vert').read_text(),
                              varyings=["normquat"])
        values = case["input"]
        data = struct.pack(f'<4{case["type"]}', *values)
        source = ctx.buffer(data)
        output = ctx.buffer(reserve=16)
        attribute_type = "u" if case["type"] == "I" else case["type"]
        vao = ctx.vertex_array(program, [(source, f"4{attribute_type}", "vs_in_typed_reg0")])
        vao.transform(output, vertices=1, mode=moderngl.POINTS)
        ctx.finish()
        actual = struct.unpack("<4f", output.read())
        if actual != tuple(case["expected"]):
            raise AssertionError(f'case {case["id"]}: GPU {actual}, CPU {case["expected"]}')
        vao.release()
        output.release()
        source.release()
        program.release()
    print(f'PASS: 21 generated vertex shaders executed with CPU parity on {ctx.info["GL_RENDERER"]}')
    zeros.release()
    fbo.release()
    ctx.release()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("src/video_core/shader/generator/glsl_shader_gen.cpp"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/gpu-attribute-padding"))
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--gl-only", "--render-only", dest="render_only", action="store_true",
                        help="execute the existing output's shaders without recompiling the probe")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    cases_dir = args.output.parent / f"{args.output.name}-cases"
    if args.render_only:
        render(cases_dir)
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cases_dir.mkdir(parents=True, exist_ok=True)
    stubs = cases_dir / "stubs"
    (stubs / "common").mkdir(parents=True, exist_ok=True)
    (stubs / "common/settings.h").write_text(
        "// CodexAstraUlt: shader_gen.cpp does not use its settings include.\n#pragma once\n")
    cache = Path("src/video_core/renderer_vulkan/vk_pipeline_cache.cpp").read_text()
    functions = "\n".join(extract_function(cache, signature) for signature in (
        "AttribLoadFlags MakeAttribLoadFlag(", "ExtraVSConfig PipelineCache::CalcExtraConfig("))
    cpp = args.output.with_suffix(".cpp")
    cpp.write_text(HEADER + functions + TESTS)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] \
        if args.sanitize else ["-O2"]
    command = [os.environ.get("CXX", "c++"), "-std=c++20", *flags,
               "-DMICROPROFILE_ENABLED=0", "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL",
               f"-I{stubs}", "-Isrc", "-Iexternals/fmt/include", "-Iexternals/boost",
               "-Iexternals/xxHash", "-Iexternals/nihstro/include", "-Iexternals/microprofile",
               str(cpp), str(args.source),
               "src/video_core/shader/generator/glsl_shader_decompiler.cpp",
               "src/video_core/shader/generator/shader_gen.cpp",
               "src/video_core/pica/shader_setup.cpp", "src/video_core/pica/shader_unit.cpp",
               "src/video_core/shader/shader_interpreter.cpp", "-o", str(args.output)]
    subprocess.run(command, check=True)
    subprocess.run([str(args.output), str(cases_dir)], check=True)
    if args.render:
        render(cases_dir)


if __name__ == "__main__":
    main()
