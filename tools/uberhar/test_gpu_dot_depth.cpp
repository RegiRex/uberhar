// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Public synthetic programs/data exercise production CPU engines
// and the real GLSL generator. No title code, device artifact or copied oracle.
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <json.hpp>
#include <nihstro/inline_assembly.h>
#include "common/hash.h"
#include "common/logging/log.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit_x64_compiler.h"

// CodexAstraLocal: Only logging and the unused settings include are replaced.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
}

namespace {
using namespace Pica;
using namespace Pica::Shader::Generator;
using Json = nlohmann::json;
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;
using Row = std::array<float, 16>;

void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

template <typename T>
void Write(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
    Check(bool(file), "fixture write failed");
}

// CodexAstraLocal: Freeze a small reproducible finite corpus, plus explicit
// signed-zero/nonfinite cases; generation does not search for a passing witness.
std::vector<Row> Inputs(bool depth) {
    std::vector<Row> rows;
    u32 seed = 0x78347u;
    const auto next = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(static_cast<s32>(seed)) * 0x1p-31f;
    };
    for (u32 i = 0; i < (depth ? 4096U : 256U); ++i) {
        Row row{};
        row[3] = 1;
        row[12] = 1; // Relative uniform address in the DPHI fixture.
        for (u32 lane = 4; lane < (depth ? 7U : 12U); ++lane)
            row[lane] = next();
        if (depth)
            row[7] = -0.5f - row[4] - row[5] - row[6];
        rows.push_back(row);
    }
    if (depth)
        return rows;
    constexpr float infinity = std::numeric_limits<float>::infinity();
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<std::array<float, 4>, 9> lhs{{
        {{0, 0, 0, 0}}, {{-0.f, -0.f, -0.f, -0.f}}, {{0, -0.f, 0, -0.f}},
        {{0, 1, -2, 3}}, {{infinity, 0, 0, 0}}, {{nan, 1, 2, 3}},
        {{infinity, -infinity, 2, 3}}, {{1, 2, 3, 4}}, {{-0.f, 0, -0.f, 0}},
    }};
    const std::array<std::array<float, 4>, 9> rhs{{
        {{1, 1, 1, 1}}, {{1, 1, 1, 1}}, {{1, 1, 1, 1}},
        {{1, 1, infinity, 1}}, {{0, 0, 0, 0}}, {{1, 1, 1, 1}},
        {{1, 1, 1, 1}}, {{nan, 1, 1, 1}}, {{-1, 1, -1, 1}},
    }};
    for (u32 i = 0; i < lhs.size(); ++i) {
        Row row{};
        row[3] = row[12] = 1;
        std::copy(lhs[i].begin(), lhs[i].end(), row.begin() + 4);
        std::copy(rhs[i].begin(), rhs[i].end(), row.begin() + 8);
        rows.push_back(row);
    }
    return rows;
}

// CodexAstraLocal: The unsanitized option intentionally retains IEEE products;
// this scalar reference checks that policy separately from the always-sanitized
// production JIT. Volatile separates products and each pairwise rounding step.
float UnsanitizedReference(O opcode, const Row& row) {
    const std::array<float, 4> uniform{.75f, 0.f, -1.25f, 2.f};
    const std::array<u32, 4> first{3, 2, 1, 0}, second{1, 0, 3, 2};
    std::array<float, 4> product{};
    for (u32 lane = 0; lane < 4; ++lane) {
        const float a = lane == 3 && opcode != O::DP4 && opcode != O::DP3
                            ? 1.f : row[4 + first[lane]];
        const float b = opcode == O::DPHI ? uniform[second[lane]] : row[8 + second[lane]];
        volatile float p = a * b;
        product[lane] = p;
    }
    volatile float xy = product[0] + product[1];
    if (opcode == O::DP3) {
        volatile float result = xy + product[2];
        return result;
    }
    volatile float zw = product[2] + product[3];
    volatile float result = xy + zw;
    return result;
}

Json Emit(const std::filesystem::path& directory, std::string id, O opcode, bool depth,
          bool sanitize) {
    // CodexAstraLocal: DPHI uses the actual inverse source fields and relative
    // uniform addressing, which the minimal inline assembler does not encode.
    const nihstro::InlineAsm move{O::MOV, D::MakeOutput(0), "xyw", S::MakeInput(0)};
    const nihstro::InlineAsm address{O::MOVA, D{}, "x", S::MakeInput(3), "x", S{}, "",
                                    nihstro::InlineAsm::RelativeAddress::A1};
    const nihstro::InlineAsm dot = depth
        ? nihstro::InlineAsm{opcode, D::MakeOutput(0), "z", S::MakeFloat(0), S::MakeInput(1)}
        : nihstro::InlineAsm{opcode, D::MakeOutput(0), "z", S::MakeInput(1), "wzyx",
                            S::MakeInput(2), "yxwz"};
    auto binary = opcode == O::DPHI
        ? nihstro::InlineAsm::CompileToRawBinary({move, address, dot, {O::END}})
        : nihstro::InlineAsm::CompileToRawBinary({move, dot, {O::END}});
    const u32 dot_offset = opcode == O::DPHI ? 2U : 1U;
    if (opcode == O::DPHI) {
        auto& instruction = binary.program[dot_offset];
        instruction.common.src1i = S::MakeInput(1);
        instruction.common.src2i = S::MakeFloat(0);
        instruction.common.address_register_index = 1;
    }
    ShaderSetup setup;
    for (u32 i = 0; i < binary.program.size(); ++i)
        setup.UpdateProgramCode(i, binary.program[i].hex);
    for (u32 i = 0; i < binary.swizzle_table.size(); ++i)
        setup.UpdateSwizzleData(i, binary.swizzle_table[i].hex);
    setup.uniforms.f[0] = Common::Vec4<f24>::AssignToAll(f24::One());
    setup.uniforms.f[1] = {f24::FromFloat32(.75f), f24::Zero(), f24::FromFloat32(-1.25f),
                           f24::FromFloat32(2.f)};
    RegsInternal regs{};
    const u32 input_count = depth ? 2U : opcode == O::DPHI ? 4U : 3U;
    regs.vs.max_input_attribute_index.Assign(input_count - 1);
    regs.vs.output_mask.Assign(1);
    regs.rasterizer.vs_output_total.Assign(1);
    auto& map = regs.rasterizer.vs_output_attributes[0];
    map.map_x.Assign(Semantic::POSITION_X);
    map.map_y.Assign(Semantic::POSITION_Y);
    map.map_z.Assign(Semantic::POSITION_Z);
    map.map_w.Assign(Semantic::POSITION_W);
    for (u32 i = 0; i < input_count; ++i)
        regs.vs.input_attribute_to_register_map_low |= i << (4 * i);
    PicaVSConfig config{regs, setup};
    config.state.used_input_vertex_attributes = input_count;
    for (u32 i = 0; i < input_count; ++i)
        config.state.input_vertex_attributes[i] = {static_cast<u8>(i), 3, 4};
    ExtraVSConfig extra{};
    extra.sanitize_mul = sanitize;
    extra.separable_shader = extra.use_clip_planes = true;
    extra.load_flags.fill(AttribLoadFlags::Float);
    const auto legacy = GLSL::GenerateVertexShader(setup, config, extra);
    Check(!legacy.empty(), "legacy shader generation failed");
#ifndef UBERHAR_LEGACY_DOT_BASELINE
    extra.precise_jit_dot = true;
#endif
    const auto precise = GLSL::GenerateVertexShader(setup, config, extra);
    Check(!precise.empty(), "precise shader generation failed");
    std::ofstream{directory / (id + "-legacy.vert")} << "#version 430\n" << legacy;
    std::ofstream{directory / (id + "-precise.vert")} << "#version 430\n" << precise;
#ifndef UBERHAR_LEGACY_DOT_BASELINE
    Check(legacy.find("precise_jit_dot4") == std::string::npos,
          "default generation unexpectedly opted into JIT arithmetic");
    Check(precise.find("precise float sum_xy") != std::string::npos,
          "precise generation lost the pairwise rounding boundary");
    const auto assignment = [&](const std::string& source) {
        const auto begin = source.find("vs_out_attr0.z = ");
        Check(begin != std::string::npos, "missing dot destination");
        return source.substr(begin, source.find(';', begin) - begin);
    };
    Check((assignment(legacy) == assignment(precise)) == (opcode == O::DP3),
          "DP3 changed or a four-lane dot did not adopt precise generation");
#endif
    // CodexAstraLocal: Export actual semantic UBO fields; no uninitialized ABI padding.
    std::array<float, 404> uniforms{};
    for (u32 uniform = 0; uniform < 96; ++uniform)
        for (u32 lane = 0; lane < 4; ++lane)
            uniforms[20 + uniform * 4 + lane] = setup.uniforms.f[uniform][lane].ToFloat32();
    std::ofstream ubo{directory / (id + "-uniforms.bin"), std::ios::binary};
    Write(ubo, uniforms);
    std::ofstream input{directory / (id + "-inputs.bin"), std::ios::binary};
    std::ofstream interpreter_file{directory / (id + "-interpreter.bin"), std::ios::binary};
    std::ofstream jit_file{directory / (id + "-jit.bin"), std::ios::binary};
    std::ofstream raw_file{directory / (id + "-unsanitized.bin"), std::ios::binary};
    Shader::InterpreterEngine interpreter;
    interpreter.SetupBatch(setup, 0);
    Shader::JitShader jit;
    jit.Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
    const auto rows = Inputs(depth);
    u32 engine_differences{};
    for (const auto& row : rows) {
        Write(input, row);
        ShaderUnit interpreted{}, compiled{};
        for (u32 reg = 0; reg < 4; ++reg)
            for (u32 lane = 0; lane < 4; ++lane)
                interpreted.input[reg][lane] = compiled.input[reg][lane] =
                    f24::FromFloat32(row[reg * 4 + lane]);
        interpreter.Run(setup, interpreted);
        jit.Run(setup, compiled, 0);
        std::array<float, 4> cpu{}, native{}, unsanitized{};
        for (u32 lane = 0; lane < 4; ++lane) {
            cpu[lane] = interpreted.output[0][0][lane].ToFloat32();
            native[lane] = compiled.output[0][0][lane].ToFloat32();
        }
        unsanitized = native;
        if (!depth)
            unsanitized[2] = UnsanitizedReference(opcode, row);
        engine_differences += std::bit_cast<u32>(cpu[2]) != std::bit_cast<u32>(native[2]);
        Write(interpreter_file, cpu);
        Write(jit_file, native);
        Write(raw_file, unsanitized);
    }
    if (depth) {
        // CodexAstraLocal: Two fixed old-path failures have opposite depth order.
        const std::array<u32, 4> a{0x3f1e9e50, 0xbe65070d, 0x3e10fce0, 0xbf84cde2};
        const std::array<u32, 4> b{0x3f2dfbe1, 0xbd9e41e5, 0xbec05531, 0xbf3a090c};
        for (u32 lane = 0; lane < 4; ++lane) {
            Check(std::bit_cast<u32>(rows[23][4 + lane]) == a[lane], "witness23 changed");
            Check(std::bit_cast<u32>(rows[30][4 + lane]) == b[lane], "witness30 changed");
        }
        Check(engine_differences == 993, "interpreter/JIT corpus comparison changed");
    }
    return {{"id", id}, {"count", rows.size()}, {"sanitize", sanitize},
            {"dp3_unchanged", opcode == O::DP3}, {"depth", depth},
            {"input_count", input_count}, {"guest_config", config.Hash()},
            {"legacy_source", Common::HashableString(legacy).Hash()},
            {"precise_source", Common::HashableString(precise).Hash()},
            {"interpreter_jit_differences", engine_differences}};
}
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "output directory required");
        const std::filesystem::path directory{argv[1]};
        std::filesystem::create_directories(directory);
        Json cases = Json::array();
        cases.push_back(Emit(directory, "depth", O::DP4, true, true));
        for (const auto [name, opcode] : std::array{
                 std::pair{"dp4", O::DP4}, std::pair{"dph", O::DPH},
                 std::pair{"dphi", O::DPHI}, std::pair{"dp3", O::DP3}})
            for (bool sanitize : {false, true})
                cases.push_back(Emit(directory, std::string{name} + (sanitize ? "-safe" : "-raw"),
                                     opcode, false, sanitize));
        std::ofstream{directory / "native.vert"}
            << "#version 430\n" << GLSL::GenerateTrivialVertexShader(true, true);
        std::ofstream{directory / "cases.json"} << cases.dump(2) << '\n';
        std::puts("PASS: real x64 JIT/interpreter, fixed witnesses, DP4/DPH/DPHI and unchanged DP3 fixtures");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
