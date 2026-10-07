// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Offline evidence only. Reuse production interpreter, input
// transport, output conversion and the exact 64-entry FIFO; never modify device
// execution or claim this host invocation reproduces Adreno/fragment behavior.
#include <array>
#include <bit>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>
#include <json.hpp>
#include "common/logging/log.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/pica/uberhar_vertex_output.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/shader_interpreter.h"

namespace Common::Log {
// CodexAstraLocal: Errors fail this isolated replay process instead of pretending
// an unsupported opcode/semantic was reproduced. Ordinary emulator logging is absent.
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
}
} // namespace Common::Log

namespace {
using Json = nlohmann::json;
using namespace Pica;
using namespace Pica::Shader::Generator;
constexpr std::size_t MaxFile = 4 * 1024 * 1024;

void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

u32 Word(std::span<const u8> bytes, std::size_t offset) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4, "short LE32 word");
    return u32{bytes[offset]} | (u32{bytes[offset + 1]} << 8) |
           (u32{bytes[offset + 2]} << 16) | (u32{bytes[offset + 3]} << 24);
}

u32 Number(const Json& value, u32 maximum = 0xffffffff) {
    Require(value.is_number_unsigned() || (value.is_number_integer() && value.get<s64>() >= 0),
            "expected unsigned integer");
    const auto number = value.get<u64>();
    Require(number <= maximum, "integer exceeds supported range");
    return static_cast<u32>(number);
}

std::string Hash(u64 value) {
    return fmt::format("{:016x}", value);
}

u64 HexValue(const Json& value) {
    // CodexAstraLocal: Upper/lowercase are identical protocol values. Parse the
    // entire fixed-width identity rather than comparing a producer's formatting.
    const auto& text = value.get_ref<const std::string&>();
    Require(text.size() == 16, "identity must have sixteen hex digits");
    u64 result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, 16);
    Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size(), "invalid hex identity");
    return result;
}

// CodexAstraLocal: Decode only bounded inline sections. No manifest-controlled
// filenames, host pointers, struct padding, or absolute guest memory allocations.
struct Packet {
    std::vector<u8> bytes;
    Json manifest;
    Json info;
    std::span<const u8> payload;

    Packet(const std::filesystem::path& path, u32 index) {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        Require(input.good(), "cannot open capture");
        const auto size = input.tellg();
        Require(size >= 16 && size <= static_cast<std::streamoff>(MaxFile), "invalid file size");
        bytes.resize(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        Require(input.good(), "capture changed during read");
        Require(std::equal(bytes.begin(), bytes.begin() + 8, "UBVCAP01"), "bad magic");
        const auto metadata = Word(bytes, 8), data = Word(bytes, 12);
        Require(metadata >= 2 && metadata <= 128 * 1024 && data <= MaxFile - 256 * 1024 &&
                    16ull + metadata + data == bytes.size(), "bad container lengths");
        manifest = Json::parse(bytes.begin() + 16, bytes.begin() + 16 + metadata);
        Require(manifest.at("schema") == 1, "unsupported schema");
        const auto& packets = manifest.at("packets");
        Require(packets.is_array() && packets.size() <= 8 && index < packets.size(),
                "invalid packet index");
        info = packets.at(index);
        Require(info.at("recorded") == true && info.at("accepted") == true,
                "draw was not submission-accepted");
        payload = std::span{bytes}.subspan(16 + metadata);
    }

    std::span<const u8> Section(const std::string& name, std::size_t size = 0) const {
        const auto& section = info.at("sections").at(name);
        const auto offset = Number(section.at("offset"), payload.size());
        const auto length = Number(section.at("size"), payload.size());
        Require(length <= payload.size() - offset && (!size || length == size), "bad section");
        return payload.subspan(offset, length);
    }
};

void WriteFloat(std::ostream& output, float value) {
    const u32 word = std::bit_cast<u32>(value);
    const std::array<u8, 4> bytes{u8(word), u8(word >> 8), u8(word >> 16), u8(word >> 24)};
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// CodexAstraLocal: Emit only 22 defined semantic scalars, excluding OutputVertex
// padding. This is before primitive quaternion correction and rasterization.
void WriteVertex(std::ostream& output, const OutputVertex& vertex) {
    for (const auto* vector : {&vertex.pos, &vertex.quat, &vertex.color})
        for (const auto value : std::span{vector->AsArray(), 4})
            WriteFloat(output, value.ToFloat32());
    for (const auto value : std::span{vertex.tc0.AsArray(), 2})
        WriteFloat(output, value.ToFloat32());
    for (const auto value : std::span{vertex.tc1.AsArray(), 2})
        WriteFloat(output, value.ToFloat32());
    WriteFloat(output, vertex.tc0_w.ToFloat32());
    for (const auto value : std::span{vertex.view.AsArray(), 3})
        WriteFloat(output, value.ToFloat32());
    for (const auto value : std::span{vertex.tc2.AsArray(), 2})
        WriteFloat(output, value.ToFloat32());
}

struct Replay {
    RegsInternal regs{};
    ShaderSetup setup;
    AttributeBuffer defaults{};
    ExtraVSConfig extra{};
    std::vector<u32> indices;
    std::array<NativeInputAttribute, 16> inputs{};
    std::array<std::span<const u8>, 16> mappings{};
    NativeVertexInputPlan input_plan;
    u32 minimum{}, span{};
    bool indexed{};

    explicit Replay(const Packet& packet) {
        static_assert(std::endian::native == std::endian::little,
                      "production PICA loader requires a validated little-endian replay host");
        const auto& info = packet.info;
        auto raw = packet.Section("regs", 3072);
        for (u32 index = 0; index < regs.reg_array.size(); ++index)
            regs.reg_array[index] = Word(raw, index * 4);
        for (const auto name : {"program", "swizzle"}) {
            raw = packet.Section(name, 16384);
            std::array<u32, 4096> words;
            for (u32 index = 0; index < words.size(); ++index)
                words[index] = Word(raw, index * 4);
            // CodexAstraLocal: Identity uses historical written lengths, not the
            // last currently nonzero word. Full execution arrays remain intact.
            if (std::string_view{name} == "program")
                setup.UpdateProgramCode(words, Number(info.at("program_words"), 4096));
            else
                setup.UpdateSwizzleData(words, Number(info.at("swizzle_words"), 4096));
        }
        const auto& key = info.at("key");
        Require(setup.GetProgramCodeHash() == HexValue(key.at("program_hash")) &&
                    setup.GetSwizzleDataHash() == HexValue(key.at("swizzle_hash")),
                "program/swizzle identity does not match frozen bytes");
        Require(Number(key.at("entry"), 4095) == regs.vs.main_offset &&
                    Number(key.at("input_count"), 16) == regs.vs.max_input_attribute_index + 1 &&
                    Number(key.at("output_mask"), 65535) == regs.vs.output_mask,
                "draw key does not match PICA register image");
        Require(regs.rasterizer.vs_output_total <= 7, "unsupported output semantic count");
        raw = packet.Section("defaults", 256);
        for (u32 index = 0; index < 64; ++index)
            defaults[index / 4][index % 4] = f24::FromFloat32(std::bit_cast<float>(Word(raw, index * 4)));
        raw = packet.Section("uniform_f", 1536);
        for (u32 index = 0; index < 384; ++index)
            setup.uniforms.f[index / 4][index % 4] =
                f24::FromFloat32(std::bit_cast<float>(Word(raw, index * 4)));
        raw = packet.Section("uniform_b", 16);
        for (u32 index = 0; index < 16; ++index) {
            Require(raw[index] <= 1, "invalid boolean uniform");
            setup.uniforms.b[index] = raw[index];
        }
        raw = packet.Section("uniform_i", 16);
        for (u32 index = 0; index < 16; ++index)
            setup.uniforms.i[index / 4][index % 4] = raw[index];

        const auto& flags = info.at("extra");
        extra.use_clip_planes = flags.at("use_clip_planes").get<bool>();
        extra.use_geometry_shader = flags.at("use_geometry_shader").get<bool>();
        extra.sanitize_mul = flags.at("sanitize_mul").get<bool>();
        extra.separable_shader = flags.at("separable_shader").get<bool>();
        // CodexAstraLocal: Legacy 0.1.25 packets have no arithmetic-policy flag;
        // preserve their source identity while reconstructing new captures exactly.
        if (flags.contains("precise_jit_dot")) {
            Require(flags.at("precise_jit_dot").is_boolean(), "invalid precise JIT dot flag");
            extra.precise_jit_dot = flags.at("precise_jit_dot").get<bool>();
        }
        Require(!extra.use_geometry_shader, "geometry-shader replay unsupported");
        Require(flags.at("load_flags").size() == 16, "invalid load flag count");
        for (u32 index = 0; index < 16; ++index)
            extra.load_flags[index] = static_cast<AttribLoadFlags>(Number(flags["load_flags"][index], 15));

        const auto& draw = info.at("draw");
        const u32 count = Number(draw.at("count"), 4096);
        Require(count > 0 && count == regs.pipeline.num_vertices &&
                    count == Number(key.at("vertex_count")), "vertex count identity mismatch");
        minimum = Number(draw.at("minimum"));
        const auto maximum = Number(draw.at("maximum"));
        Require(maximum >= minimum && u64{maximum} - minimum < 4096, "invalid index span");
        span = maximum - minimum + 1;
        indexed = draw.at("indexed").get<bool>();
        if (indexed) {
            const auto width = Number(draw.at("uploaded_index_width"), 2);
            Require(width && maximum <= 65535, "invalid indexed draw");
            raw = packet.Section("indices", count * width);
            for (u32 index = 0; index < count; ++index) {
                const u32 value = raw[index * width] |
                                  (width == 2 ? u32{raw[index * width + 1]} << 8 : 0);
                Require(value >= minimum && value <= maximum, "index exceeds uploaded span");
                indices.push_back(value);
            }
        } else {
            Require(count == span && minimum == Number(draw.at("vertex_offset")) &&
                        minimum == regs.pipeline.vertex_offset, "nonindexed offset mismatch");
            for (u32 index = 0; index < count; ++index)
                indices.push_back(minimum + index);
        }

        // CodexAstraLocal: Compact copied rows are the only memory source. Rebase
        // loader accesses to zero without rebasing the original FIFO/index keys.
        const auto& descriptors = info.at("native_inputs");
        Require(descriptors.size() == 16 && info.at("bindings").size() <= 12,
                "invalid input descriptor count");
        for (u32 attr = 0; attr <= regs.vs.max_input_attribute_index; ++attr) {
            const auto& descriptor = descriptors.at(attr);
            auto& input = inputs[attr];
            input.is_default = descriptor.at("is_default").get<bool>();
            input.offset = attr + 1; // Bounded synthetic address, never a guest pointer.
            if (input.is_default)
                continue;
            input.elements = Number(descriptor.at("elements"), 4);
            const auto format = Number(descriptor.at("format"), 3);
            input.format = static_cast<PipelineRegs::VertexAttributeFormat>(format);
            input.stride = Number(descriptor.at("stride"), 255);
            Require(input.elements && input.stride, "unconfigured/zero-stride native input");
            const auto offset = Number(descriptor.at("offset"));
            const u32 width = input.elements * (format == 3 ? 4 : format == 2 ? 2 : 1);
            for (const auto& binding : info.at("bindings")) {
                const auto guest_offset = Number(binding.at("guest_offset"));
                const auto stride = Number(binding.at("guest_stride"), 255);
                if (input.stride != stride || offset < guest_offset ||
                    u64{offset} - guest_offset + width > stride)
                    continue;
                Require(Number(binding.at("span"), 4096) == span, "binding span mismatch");
                auto view = packet.Section(binding.at("section").get<std::string>(), stride * span)
                                .subspan(offset - guest_offset);
                if (!mappings[attr].empty()) {
                    Require(view.size() == mappings[attr].size() &&
                                std::equal(view.begin(), view.end(), mappings[attr].begin()),
                            "ambiguous overlapping uploaded native input");
                } else {
                    mappings[attr] = view;
                }
            }
            Require(!mappings[attr].empty(), "native loader cannot reconstruct captured input");
        }
        const auto prepared = input_plan.Prepare(
            regs.vs, Number(info.at("available_attributes"), 16), 0, span - 1,
            [&](u32 attr) { return inputs[attr]; },
            [&](PAddr address) -> std::span<const u8> {
                Require(address >= 1 && address <= 16, "bad synthetic loader address");
                return mappings[address - 1];
            });
        Require(prepared == NativeVertexInputPlan::Result::Ready, "production native input rejected");
    }

    NativeVertexCounts Run(const std::filesystem::path& directory, bool reset_per_miss) {
        Shader::InterpreterEngine engine;
        engine.SetupBatch(setup, regs.vs.main_offset);
        ShaderUnit unit;
        NativeVertexPlan output_plan{regs.vs, regs.rasterizer};
        Require(output_plan.Supported(), "production output transport unsupported");
        std::ofstream output{directory / (reset_per_miss ? "cpu_fresh.bin" : "cpu.bin"),
                             std::ios::binary};
        std::ofstream input;
        if (!reset_per_miss)
            input.open(directory / "cpu_inputs.bin", std::ios::binary);
        std::vector<AttributeBuffer> loaded(span);
        NativeVertexSamples samples;
        u32 submitted = 0;
        const auto counts = RunNativeVertexBatch<false>(
            indices.size(), indexed, [&](u32 index) { return indices[index]; },
            [&]<bool>(u32 vertex, u32) {
                if (reset_per_miss)
                    unit = ShaderUnit{};
                const auto relative = vertex - minimum;
                Require(input_plan.CanLoad(relative), "index changed after reconstruction");
                input_plan.Load(unit, defaults, relative);
                loaded[relative] = unit.input;
                engine.Run(setup, unit);
                return output_plan.Convert(unit);
            },
            [&](const OutputVertex& vertex) {
                WriteVertex(output, vertex);
                if (!reset_per_miss) {
                    for (const auto& attr : loaded[indices[submitted] - minimum])
                        for (const auto value : std::span{attr.AsArray(), 4})
                            WriteFloat(input, value.ToFloat32());
                }
                ++submitted;
            }, samples);
        Require(output.good() && (reset_per_miss || input.good()), "cannot write replay results");
        return counts;
    }

    Json Generate(const Packet& packet, const std::filesystem::path& directory) {
        PicaVSConfig config{regs, setup};
        const auto& attributes = packet.info.at("layout").at("attributes");
        Require(attributes.size() == 16, "invalid layout attribute count");
        config.state.used_input_vertex_attributes = attributes.size();
        for (u32 index = 0; index < attributes.size(); ++index) {
            auto& attr = config.state.input_vertex_attributes[index];
            attr.location = Number(attributes[index].at("location"), 15);
            attr.type = Number(attributes[index].at("type"), 3);
            attr.size = Number(attributes[index].at("size"), 4);
        }
        const auto source = GLSL::GenerateVertexShader(setup, config, extra);
        Require(!source.empty(), "production GLSL generation failed");
        const u64 expected = HexValue(packet.info.at("pipeline").at("vs_config_hash"));
        Require(!expected || expected == config.Hash(),
                "VS configuration hash differs from captured selected pipeline");
        const u64 expected_source = HexValue(packet.info.at("pipeline").at("vs_source_hash"));
        const auto source_hash = Common::ComputeHash64(source.data(), source.size());
        Require(!expected_source || expected_source == source_hash,
                "generated GLSL hash differs from captured selected pipeline");
        std::ofstream shader{directory / "vertex.vert"};
        shader << "#version 430\n" << source;
        Require(shader.good(), "cannot write generated shader");
        // CodexAstraLocal: Apply the real trivial VS to CPU output when comparing
        // clip/viewport transport. Do not duplicate its float rounding in Python.
        std::ofstream native_shader{directory / "native.vert"};
        native_shader << "#version 430\n" << GLSL::GenerateTrivialVertexShader(
            extra.use_clip_planes, extra.separable_shader);
        Require(native_shader.good(), "cannot write native transport shader");
        return {{"program_hash", Hash(setup.GetProgramCodeHash())},
                {"swizzle_hash", Hash(setup.GetSwizzleDataHash())},
                {"vs_config_hash", Hash(config.Hash())}, {"vs_source_hash", Hash(source_hash)},
                {"pipeline_identity_available", expected != 0},
                {"source_identity_available", expected_source != 0}};
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 4, "usage: replay_vertex_capture capture.uvc packet-index existing-output-dir");
        const unsigned long index = std::stoul(argv[2]);
        Require(index < 8, "packet index exceeds capture cap");
        Packet packet{argv[1], static_cast<u32>(index)};
        const std::filesystem::path directory{argv[3]};
        Require(std::filesystem::is_directory(directory), "output directory does not exist");
        Replay replay{packet};
        auto metadata = replay.Generate(packet, directory);
        const auto counts = replay.Run(directory, false);
        replay.Run(directory, true);
        metadata["cpu_invocations"] = counts.invocations;
        metadata["cpu_fifo_hits"] = counts.hits;
        metadata["vertices"] = replay.indices.size();
        metadata["scope"] = "Production CPU pre-primitive vertices; fresh-unit output is a "
                            "counterfactual diagnostic, not the native oracle.";
        std::ofstream output{directory / "cpu.json"};
        output << metadata.dump(2) << '\n';
        Require(output.good(), "cannot write replay metadata");
        std::cout << metadata.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "replay: " << error.what() << '\n';
        return 2;
    }
}
