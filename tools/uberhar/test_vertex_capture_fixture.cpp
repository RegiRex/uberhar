// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Synthetic, redistributable inputs exercise the offline replay
// without embedding a game's shader/data or claiming actual device captures.
#define main UberharReplayWorkerMain
#include "replay_vertex_capture.cpp"
#undef main
#include <nihstro/inline_assembly.h>

namespace {
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;

void Append(std::vector<u8>& bytes, u32 value) {
    for (u32 shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<u8>(value >> shift));
}

void Fixture(const std::filesystem::path& directory, const std::string& name) {
    RegsInternal regs{};
    ShaderSetup setup;
    ExtraVSConfig extra{};
    extra.sanitize_mul = true;
    extra.separable_shader = true;
    extra.load_flags.fill(AttribLoadFlags::Float);
    regs.lighting.disable.Assign(1);
    regs.vs.output_mask.Assign(1 << 3);
    regs.rasterizer.vs_output_total.Assign(1);
    auto& semantic = regs.rasterizer.vs_output_attributes[0];
    semantic.map_x.Assign(Semantic::QUATERNION_X);
    semantic.map_y.Assign(Semantic::QUATERNION_Y);
    semantic.map_z.Assign(Semantic::QUATERNION_Z);
    semantic.map_w.Assign(Semantic::QUATERNION_W);
    const bool carry = name == "carry" || name == "fifo_high";
    auto binary = nihstro::InlineAsm::CompileToRawBinary({
        {O::MOV, D::MakeOutput(3), name == "missing" || carry ? "xyz" : "xyzw",
         name == "uniform" ? S::MakeFloat(0) : S::MakeInput(0)},
        {O::END}});
    if (carry) {
        binary = nihstro::InlineAsm::CompileToRawBinary({
            {O::MOV, D::MakeOutput(3), "xyz", S::MakeInput(0)}, {O::NOP}, {O::NOP},
            {O::MOV, D::MakeOutput(3), "w", S::MakeInput(0)}, {O::END}});
        nihstro::Instruction compare{};
        compare.opcode = O::CMP;
        compare.common.src1 = S::MakeFloat(1);
        compare.common.src2 = S::MakeInput(0);
        compare.common.compare_op.x = nihstro::Instruction::Common::CompareOpType::GreaterThan;
        compare.common.compare_op.y = nihstro::Instruction::Common::CompareOpType::Equal;
        binary.program[1] = compare;
        nihstro::Instruction branch{};
        branch.opcode = O::IFC;
        branch.flow_control.op = nihstro::Instruction::FlowControlType::Op::JustX;
        branch.flow_control.refx = false;
        branch.flow_control.dest_offset = 4;
        binary.program[2] = branch;
    }
    for (u32 index = 0; index < binary.program.size(); ++index)
        setup.UpdateProgramCode(index, binary.program[index].hex);
    for (u32 index = 0; index < binary.swizzle_table.size(); ++index)
        setup.UpdateSwizzleData(index, binary.swizzle_table[index].hex);
    // CodexAstraLocal: A cleared trailing word still contributes to the historical
    // prefix length used by production hashes; bytes alone cannot recover it.
    if (name == "highwater") {
        setup.UpdateProgramCode(1000, 1);
        setup.UpdateProgramCode(1000, 0);
        setup.UpdateSwizzleData(100, 1);
        setup.UpdateSwizzleData(100, 0);
    }
    regs.pipeline.num_vertices = 96;
    const bool indexed = name == "fifo_high" || name == "u8_widened";
    const bool fixed = name == "fixed";
    const u32 minimum = name == "fifo_high" ? 60000 : name == "offset" ? 70000 : 0;
    regs.pipeline.vertex_offset = name == "offset" ? minimum : 0;
    const u32 span = name == "fifo_high" ? 65 : name == "u8_widened" ? 8 : 96;
    const u32 maximum = minimum + span - 1;
    u32 type = 3, components = 4, stride = 16, upload_stride = 16, native_format = 109;
    bool conversion = false, emulation = false;
    if (name == "signed" || name == "emulated3") {
        type = 2;
        components = 3;
        stride = name == "signed" ? 6 : 8;
        upload_stride = 8;
        native_format = name == "signed" ? 89 : 96;
        conversion = true;
        emulation = name == "emulated3";
        extra.load_flags[0] = static_cast<AttribLoadFlags>(emulation ? 10 : 2);
    } else if (name == "ubyte" || name == "sbyte_scaled") {
        type = name == "ubyte" ? 1 : 0;
        stride = upload_stride = 4;
        native_format = name == "ubyte" ? 41 : 40;
        conversion = name == "ubyte";
        extra.load_flags[0] = static_cast<AttribLoadFlags>(conversion ? 4 : 1);
    }
    std::vector<u8> payload;
    Json sections = Json::object();
    const auto section = [&](const std::string& label, const std::vector<u8>& data) {
        sections[label] = {{"offset", payload.size()}, {"size", data.size()}};
        payload.insert(payload.end(), data.begin(), data.end());
    };
    const auto words = [&](const auto& values) {
        std::vector<u8> data;
        for (const auto value : values)
            Append(data, value);
        return data;
    };
    section("regs", words(regs.reg_array));
    section("program", words(setup.GetProgramCode()));
    section("swizzle", words(setup.GetSwizzleData()));
    std::vector<u8> defaults(256), uniforms(1536), bools(16), integers(16);
    const std::array<float, 4> fixture_values{.123456789f, .5f, .75f, .875f};
    for (u32 index = 0; index < 4; ++index) {
        const auto word = std::bit_cast<u32>(fixture_values[index]);
        for (u32 byte = 0; byte < 4; ++byte)
            defaults[index * 4 + byte] = uniforms[index * 4 + byte] = u8(word >> (byte * 8));
    }
    section("defaults", defaults);
    section("uniform_f", uniforms);
    section("uniform_b", bools);
    section("uniform_i", integers);
    std::vector<u8> vertices;
    for (u32 vertex = 0; vertex < span; ++vertex) {
        if (type == 3) {
            for (u32 component = 0; component < 4; ++component) {
                float value = fixture_values[component];
                if (carry && component == 0)
                    value = vertex == 1 ? 1.f : -1.f;
                if (carry && component == 3)
                    value = vertex == 1 ? .75f : .25f;
                Append(vertices, std::bit_cast<u32>(value));
            }
        } else if (type == 2) {
            const std::array<s16, 4> values{-300, 21, 1, 999};
            for (u32 component = 0; component < stride / 2; ++component) {
                const auto value = values[component];
                vertices.push_back(u8(value));
                vertices.push_back(u8(u16(value) >> 8));
            }
        } else {
            for (const auto value : type == 1 ? std::array<u8, 4>{1, 2, 200, 255} :
                                               std::array<u8, 4>{252, 2, 100, 1})
                vertices.push_back(value);
        }
    }
    if (!fixed)
        section("vertex_0", vertices);
    std::vector<u8> fixed_bytes;
    for (float value : {0.f, 0.f, 0.f, 1.f})
        Append(fixed_bytes, std::bit_cast<u32>(value));
    if (fixed)
        fixed_bytes.insert(fixed_bytes.end(), defaults.begin(), defaults.begin() + 16);
    section("fixed", fixed_bytes);
    if (indexed) {
        std::vector<u8> indices;
        for (u32 vertex = 0; vertex < 96; ++vertex) {
            const u32 index = minimum + (name == "u8_widened" ? vertex % 8 :
                                       vertex < 65 ? vertex : vertex % 2 ? 0 : 64);
            indices.push_back(u8(index));
            indices.push_back(u8(index >> 8));
        }
        section("indices", indices);
    }
    std::vector<u8> pica(1616), vs(32), fs(1328);
    std::copy(uniforms.begin(), uniforms.end(), pica.begin() + 80);
    if (name == "uniform") {
        const auto changed = std::bit_cast<u32>(.9375f);
        for (u32 byte = 0; byte < 4; ++byte)
            pica[80 + byte] = u8(changed >> (byte * 8));
    }
    section("vs_pica", pica);
    section("vs_extra", vs);
    section("fs", fs);

    Json attributes = Json::array(), bindings = Json::array(), layout_bindings = Json::array();
    Json native = Json::array();
    for (u32 index = 0; index < 16; ++index) {
        attributes.push_back({{"binding", index == 0 && !fixed ? 0 : fixed ? 0 : 1},
            {"location", index}, {"offset", index == 0 && fixed ? 16 : 0},
            {"type", index == 0 && !fixed ? type : 3},
            {"size", index == 0 && !fixed ? components : 4},
            {"native_format", index == 0 && !fixed ? native_format : 109},
            {"needs_conversion", index == 0 && !fixed && conversion},
            {"needs_emulation", index == 0 && !fixed && emulation}});
        native.push_back({{"offset", 0}, {"stride", index == 0 && !fixed ? stride : 0},
            {"elements", index == 0 && !fixed ? components : 0},
            {"format", index == 0 && !fixed ? type : 0}, {"is_default", fixed || index != 0}});
    }
    if (!fixed) {
        bindings.push_back({{"binding", 0}, {"guest_offset", 0}, {"guest_stride", stride},
            {"upload_stride", upload_stride}, {"span", span}, {"section", "vertex_0"}});
        layout_bindings.push_back({{"binding", 0}, {"stride", upload_stride}, {"fixed", false}});
    }
    layout_bindings.push_back({{"binding", fixed ? 0 : 1}, {"stride", 0}, {"fixed", true}});
    PicaVSConfig config{regs, setup};
    config.state.used_input_vertex_attributes = 16;
    for (u32 index = 0; index < 16; ++index) {
        auto& attr = config.state.input_vertex_attributes[index];
        attr.location = index;
        attr.type = attributes[index]["type"].get<u8>();
        attr.size = attributes[index]["size"].get<u8>();
    }
    const auto shader = GLSL::GenerateVertexShader(setup, config, extra);
    Json flags = Json::array();
    for (const auto flag : extra.load_flags)
        flags.push_back(static_cast<u32>(flag));
    Json key{{"program_hash", Hash(setup.GetProgramCodeHash())},
             {"swizzle_hash", Hash(setup.GetSwizzleDataHash())}, {"entry", 0},
             {"vertex_count", 96}, {"input_count", 1}, {"output_mask", 8},
             {"color_address", "000000001f000000"}, {"depth_address", "000000001e000000"}};
    Json packet{{"id", 0}, {"swap", 11}, {"interval", 0}, {"attempt_ordinal", 37},
        {"recorded", true}, {"accepted", true}, {"completed", false},
        {"tick", "0000000000000011"}, {"status", "submitted"}, {"key", key},
        {"draw", {{"indexed", indexed}, {"count", 96}, {"minimum", minimum},
            {"maximum", maximum}, {"vertex_offset", regs.pipeline.vertex_offset},
            {"base_vertex", indexed ? -s32(minimum) : 0},
            {"original_index_width", indexed ? name == "u8_widened" ? 1 : 2 : 0},
            {"uploaded_index_width", indexed ? 2 : 0}}},
        {"sections", sections}, {"bindings", bindings}, {"native_inputs", native},
        {"program_words", setup.GetBiggestProgramSize()},
        {"swizzle_words", setup.GetBiggestSwizzleSize()},
        {"available_attributes", 1}, {"layout", {{"bindings", layout_bindings}, {"attributes", attributes}}},
        {"extra", {{"use_clip_planes", false}, {"use_geometry_shader", false},
            {"sanitize_mul", true}, {"separable_shader", true}, {"load_flags", flags}}},
        {"pipeline", {{"key", "0000000000000000"}, {"vs_config_hash", Hash(config.Hash())},
            {"fs_config_hash", "0000000000000000"}, {"gs_config_hash", "0000000000000000"},
            {"vs_source_hash", Hash(Common::ComputeHash64(shader.data(), shader.size()))},
            {"fragment_route", "synthetic"}}}, {"profile", Json::object()},
        {"ubo_ranges", {{"vs_pica", {{0, 4}, {16, 64}, {80, 1536}}},
            {"vs_extra", {{0, 8}, {16, 16}}}, {"fs", {{0, 1328}}}}}};
    Json manifest{{"schema", 1}, {"capture_id", name}, {"mode", "capture"},
        {"title_id", "0000000000000001"}, {"run", "0000000000000001"},
        {"version", "synthetic"}, {"revision", "host-fixture"}, {"first_swap", 11},
        {"window_swaps", 4}, {"packets_per_swap", 2}, {"selector", Json::object()},
        {"limits", Json::object()}, {"summary", Json::object()},
        {"discovery", Json::array()}, {"packets", Json::array({packet})}};
    const auto metadata = manifest.dump();
    std::vector<u8> container{'U', 'B', 'V', 'C', 'A', 'P', '0', '1'};
    Append(container, metadata.size());
    Append(container, payload.size());
    container.insert(container.end(), metadata.begin(), metadata.end());
    container.insert(container.end(), payload.begin(), payload.end());
    std::ofstream output{directory / (name + ".uvc"), std::ios::binary};
    output.write(reinterpret_cast<const char*>(container.data()), container.size());
    Require(output.good(), "fixture write failed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 2 && std::filesystem::is_directory(argv[1]), "fixture output directory required");
        for (const char* name : {"written", "missing", "carry", "fifo_high", "u8_widened",
                                "uniform", "signed", "ubyte", "sbyte_scaled", "emulated3", "fixed",
                                "highwater", "offset"})
            Fixture(argv[1], name);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
