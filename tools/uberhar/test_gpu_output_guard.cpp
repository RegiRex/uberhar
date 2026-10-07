// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Exercise the recovered output guard with production CPU
// execution, output packing and GLSL generation. The extracted admission uses
// modeled device/cache plumbing; these tests do not establish title correctness.
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <nihstro/inline_assembly.h>
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/renderer_vulkan/uberhar_gpu_output_policy.h"
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/shader_interpreter.h"

// CodexAstraLocal: Count the actual bounded INFO calls and fail unexpected errors.
namespace TestLog {
u32 records{};
}
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error)
        throw std::runtime_error(fmt::vformat(format, args));
    if (level == Level::Info)
        ++TestLog::records;
}
} // CodexAstraLocal: namespace Common::Log

// CodexAstraLocal: The renderer shell observes work reached after real admission.
// Shader words, register layouts, input guards and hash invalidation remain real.
namespace Vulkan {
struct RasterizerVulkan {
    // CodexAstraLocal: Existing admission fixtures leave capture disabled; a
    // separate production-session regression exercises diagnostic ownership.
    struct DisabledCapture {
        template <typename... T> void BeginDraw(T&&...) {}
        void EndDraw() {}
    };
    DisabledCapture* vertex_capture{};
    Pica::RegsInternal regs{};
    struct {
        Pica::ShaderSetup vs_setup;
        bool combo{true};
        auto GetReadyGpuVertexAdmission() const {
            return ReadyVertexPolicy::Classify(combo, false, true, false,
                Pica::PipelineRegs::TriangleTopology::List, 96, false, true);
        }
    } pica;
    struct {
        bool UseGeometryShaders() const { return false; }
        bool IsFragmentShaderBarycentricSupported() const { return false; }
    } instance;
    struct Memory {
        struct Ref {
            const u8* GetPtr() const { return bytes.data(); }
            std::size_t GetSize() const { return bytes.size(); }
            std::array<u8, 192> bytes{};
        };
        u32 reads{};
        Ref GetPhysicalRef(PAddr) { ++reads; return {}; }
    } memory;
    struct {
        u32 preflights{};
        u64 GetProgramID() const { return 0; }
        bool ReadyGpuFragmentPreflight(const Pica::RegsInternal&, bool) {
            ++preflights;
            return true;
        }
    } pipeline_cache;
    std::vector<u32> vertex_batch;
    bool user_config{}, ready_vertex_attempt{};
    void* ready_vertex_pipeline{};
    u64 ready_vertex_zero_stride_rejections{}, ready_vertex_quaternion_rejections{};
    std::array<u64, static_cast<std::size_t>(ReadyVertexPolicy::InputLayoutIssue::Count)>
        ready_vertex_layout_rejections{};
    ReadyVertexPolicy::OutputWriteMemo<> ready_vertex_output_writes;
    u64 ready_vertex_output_checks{}, ready_vertex_output_w_checks{},
        ready_vertex_output_rejections{};
    u32 accelerated{};
    bool AccelerateDrawBatch(bool) { ++accelerated; return true; }
    bool AccelerateDrawBatchReady(bool is_indexed);
};

#include "gpu_output_admission.inc"
} // CodexAstraLocal: namespace Vulkan

namespace {
using namespace Pica;
using namespace Pica::Shader::Generator;
using namespace Vulkan::ReadyVertexPolicy;
using O = nihstro::OpCode::Id;
using D = nihstro::DestRegister;
using S = nihstro::SourceRegister;
using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;

void Check(bool result, const char* reason) {
    if (!result)
        throw std::runtime_error(reason);
}

// CodexAstraLocal: Assemble actual guest programs and preserve untouched words.
void Assemble(ShaderSetup& setup, std::initializer_list<nihstro::InlineAsm> assembly) {
    const auto binary = nihstro::InlineAsm::CompileToRawBinary(assembly);
    for (u32 i = 0; i < binary.program.size(); ++i)
        setup.UpdateProgramCode(i, binary.program[i].hex);
    for (u32 i = 0; i < binary.swizzle_table.size(); ++i)
        setup.UpdateSwizzleData(i, binary.swizzle_table[i].hex);
}

// CodexAstraLocal: Use an eligible unlit list with a nonzero physical output index,
// avoiding accidental reliance on a packed index being the same register number.
void InitFixture(Vulkan::RasterizerVulkan& renderer, bool writes_w) {
    auto& regs = renderer.regs;
    regs.lighting.disable.Assign(1);
    regs.pipeline.num_vertices = 96;
    auto& attributes = regs.pipeline.vertex_attributes;
    attributes.format0.Assign(PipelineRegs::VertexAttributeFormat::FLOAT);
    attributes.size0.Assign(3);
    auto& loader = attributes.attribute_loaders[0];
    loader.component_count.Assign(1);
    loader.byte_count.Assign(16);
    regs.vs.output_mask.Assign(1U << 3);
    regs.rasterizer.vs_output_total.Assign(1);
    auto& map = regs.rasterizer.vs_output_attributes[0];
    map.map_x.Assign(Semantic::QUATERNION_X);
    map.map_y.Assign(Semantic::QUATERNION_Y);
    map.map_z.Assign(Semantic::QUATERNION_Z);
    map.map_w.Assign(Semantic::QUATERNION_W);
    Assemble(renderer.pica.vs_setup,
             {{O::MOV, D::MakeOutput(3), writes_w ? "xyzw" : "xyz", S::MakeInput(0)},
              {O::END}});
}

// CodexAstraLocal: Preserve the inherited two-stage output transport as the oracle.
OutputVertex CpuOutput(ShaderUnit& unit, const RegsInternal& regs) {
    AttributeBuffer packed{};
    unit.WriteOutput(regs.vs, packed);
    return OutputVertex{regs.rasterizer, packed};
}

// CodexAstraLocal: This is the old-behavior regression. Missing physical W stays
// zero for three invocations of one real CPU unit, but the generated GPU default
// is one. Require the production admission to reject before index/cache/uploads.
void TestDefaultAndAdmission(const std::filesystem::path& directory) {
    std::ofstream manifest{directory / "cases.json"};
    manifest << "[\n";
    for (bool writes_w : {false, true}) {
        Vulkan::RasterizerVulkan renderer;
        InitFixture(renderer, writes_w);
        auto& setup = renderer.pica.vs_setup;
        Shader::InterpreterEngine engine;
        engine.SetupBatch(setup, 0);
        ShaderUnit unit;
        for (u32 vertex = 0; vertex < 3; ++vertex) {
            const float value = (vertex + 1) / 8.0f;
            unit.input[0] = Common::Vec4<f24>{f24::FromFloat32(value), f24::FromFloat32(.5f),
                f24::FromFloat32(.75f), f24::FromFloat32(.875f)};
            engine.Run(setup, unit);
            const auto output = CpuOutput(unit, renderer.regs);
            Check(output.quat.x.ToFloat32() == value, "production CPU fixture did not execute");
            Check(output.quat.w.ToFloat32() == (writes_w ? .875f : 0.f),
                  "per-batch CPU output initialization/preservation changed");
        }

        PicaVSConfig config{renderer.regs, setup};
        ExtraVSConfig extra{};
        extra.sanitize_mul = true;
        extra.load_flags.fill(AttribLoadFlags::Float);
        const auto shader = GLSL::GenerateVertexShader(setup, config, extra);
        Check(shader.find("vs_out_attr0 = vec4(0.0, 0.0, 0.0, 1.0);") != std::string::npos,
              "generated output defaults changed; reassess the narrow guard");
        const std::string name = writes_w ? "written" : "missing";
        std::ofstream{directory / (name + ".vert")} << "#version 430\n" << shader;
        if (writes_w)
            manifest << ",\n";
        manifest << "{\"shader\":\"" << name << "\",\"cpu_w\":" << (writes_w ? .875f : 0.f)
                 << ",\"gpu_w\":" << (writes_w ? .875f : 1.f) << "}";

        Check(ConsumedOutputW(renderer.regs.vs, renderer.regs.rasterizer) == (1U << 3),
              "physical-to-packed consumed-W mapping changed");
        const u32 records = TestLog::records;
        for (u32 repeat = 0; repeat < 12; ++repeat)
            Check(renderer.AccelerateDrawBatchReady(repeat % 2) == writes_w,
                  "consumed never-written W reached GPU, or written W was rejected");
        Check(renderer.ready_vertex_output_checks == 12 &&
              renderer.ready_vertex_output_w_checks == 12, "output check counters changed");
        Check(renderer.ready_vertex_output_rejections == (writes_w ? 0 : 12),
              "fallback totals omit repeated rejected draws");
        Check(TestLog::records - records == (writes_w ? 0 : 8), "detail log limit changed");
        Check(renderer.memory.reads == (writes_w ? 6 : 0) &&
              renderer.pipeline_cache.preflights == (writes_w ? 12 : 0) &&
              renderer.accelerated == (writes_w ? 12 : 0), "rejection performed GPU preparation");
        Check(renderer.ready_vertex_output_writes.Scans() == 1 &&
              renderer.ready_vertex_output_writes.Hits() == 11, "stable shader was rescanned");
        Check(!renderer.ready_vertex_attempt, "ready attempt leaked beyond the call");
    }
    manifest << "\n]\n";

    // CodexAstraLocal: Native/Custom and an unconsumed W bypass memoization.
    Vulkan::RasterizerVulkan bypass;
    InitFixture(bypass, false);
    bypass.pica.combo = false;
    Check(!bypass.AccelerateDrawBatchReady(false) && bypass.ready_vertex_output_checks == 0,
          "disabled optional route touched output policy");
    bypass.pica.combo = true;
    bypass.regs.rasterizer.vs_output_attributes[0].map_w.Assign(Semantic::INVALID);
    Check(bypass.AccelerateDrawBatchReady(false) && bypass.ready_vertex_output_writes.Scans() == 0,
          "unconsumed W caused shader scanning or fallback");
}

// CodexAstraLocal: Compare the guard to real CPU packing/conversion rather than
// another implementation of last-writer mapping. Vary only one physical W from
// zero to one, then observe which nonpadding OutputVertex semantics change.
void TestSemanticMapping() {
    u32 random = 0x64cf34ab;
    const auto next = [&] { random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                           return random; };
    for (u32 trial = 0; trial < 256; ++trial) {
        RegsInternal regs{};
        const u32 mask = next() & 0xffff;
        regs.vs.output_mask.Assign(mask);
        const u32 count = std::min<u32>(next() % 8, std::popcount(mask));
        regs.rasterizer.vs_output_total.Assign(count);
        for (u32 i = 0; i < count; ++i) {
            auto& map = regs.rasterizer.vs_output_attributes[i];
            map.map_x.Assign(static_cast<Semantic>(next() % 32));
            map.map_y.Assign(static_cast<Semantic>(next() % 32));
            map.map_z.Assign(static_cast<Semantic>(next() % 32));
            map.map_w.Assign(static_cast<Semantic>(next() % 32));
        }
        ShaderUnit unit;
        const auto baseline = CpuOutput(unit, regs);
        std::array<f24, 24> base;
        std::memcpy(base.data(), &baseline, sizeof(baseline));
        u16 expected = 0;
        for (u32 reg = 0; reg < 16; ++reg) {
            unit.output[0][reg].w = f24::One();
            const auto changed = CpuOutput(unit, regs);
            std::array<f24, 24> values;
            std::memcpy(values.data(), &changed, sizeof(changed));
            for (u32 semantic = 0; semantic < 24; ++semantic) {
                if (semantic != 17 && semantic != 21 && base[semantic] != values[semantic])
                    expected |= 1U << reg;
            }
            unit.output[0][reg].w = f24::Zero();
        }
        Check(ConsumedOutputW(regs.vs, regs.rasterizer) == expected,
              "guard disagrees with production semantic overwrite order");
    }

    // CodexAstraLocal: Explicit boundaries supplement the randomized production oracle.
    RegsInternal regs{};
    regs.vs.output_mask.Assign(0xffff8008U);
    regs.rasterizer.vs_output_total.Assign(2);
    for (auto& map : regs.rasterizer.vs_output_attributes)
        map.raw = 0x1f1f1f1f;
    auto& first = regs.rasterizer.vs_output_attributes[0];
    auto& last = regs.rasterizer.vs_output_attributes[1];
    first.map_w.Assign(Semantic::POSITION_X);
    last.map_x.Assign(Semantic::POSITION_X);
    last.map_w.Assign(static_cast<Semantic>(17));
    Check(ConsumedOutputW(regs.vs, regs.rasterizer) == 0,
          "overwritten W or padding became a consumed source");
    last.map_w.Assign(Semantic::POSITION_X);
    Check(ConsumedOutputW(regs.vs, regs.rasterizer) == 0x8000,
          "last component/high physical output mapping changed");
    last.map_w.Assign(static_cast<Semantic>(21));
    Check(ConsumedOutputW(regs.vs, regs.rasterizer) == 0, "second padding slot became consumed");
    regs.rasterizer.vs_output_total.Assign(3);
    Check(ConsumedOutputW(regs.vs, regs.rasterizer) == 0,
          "incomplete transport was assigned a narrow absence verdict");
}

// CodexAstraLocal: Cover common versus MAD layouts, all destination lanes,
// temporary registers, descriptor boundaries and reserved opcodes independently
// of nihstro GetInfo's destination flags, which the production guard itself uses.
void TestInstructionDecoding() {
    ProgramCode program{};
    SwizzleData swizzles{};
    const u32 nop = static_cast<u32>(O::NOP) << 26;
    program.fill(nop);
    const std::array arithmetic{O::ADD, O::DP3, O::DP4, O::DPH, O::DST, O::EX2, O::LG2, O::LIT,
        O::MUL, O::SGE, O::SLT, O::FLR, O::MAX, O::MIN, O::RCP, O::RSQ, O::MOV,
        O::DPHI, O::DSTI, O::SGEI, O::SLTI};
    for (const O opcode : arithmetic) {
        for (u32 destination : {0U, 7U, 15U, 16U, 31U}) {
            for (u32 mask = 0; mask < 16; ++mask) {
                for (u32 descriptor : {0U, 127U}) {
                    nihstro::Instruction instruction{};
                    instruction.opcode = opcode;
                    instruction.common.dest = D{destination};
                    instruction.common.operand_desc_id = descriptor;
                    program[2048] = instruction.hex;
                    swizzles.fill(0);
                    swizzles[descriptor] = mask;
                    const u16 expected = destination < 16 && (mask & 1) ? 1U << destination : 0;
                    Check(PossibleOutputWWrites(program, swizzles) == expected,
                          "common instruction destination/W-mask decoding changed");
                }
            }
        }
    }
    for (const O opcode : {O::MAD, O::MADI}) {
        for (u32 destination = 0; destination < 32; ++destination) {
            for (u32 mask = 0; mask < 16; ++mask) {
                for (u32 descriptor : {0U, 31U}) {
                    nihstro::Instruction instruction{};
                    instruction.opcode = opcode;
                    instruction.mad.dest = D{destination};
                    instruction.mad.operand_desc_id = descriptor;
                    program[2048] = instruction.hex;
                    swizzles.fill(0);
                    swizzles[descriptor] = mask;
                    const u16 expected = destination < 16 && (mask & 1) ? 1U << destination : 0;
                    Check(PossibleOutputWWrites(program, swizzles) == expected,
                          "MAD opcode-overlap/destination/W-mask decoding changed");
                }
            }
        }
    }
    // CodexAstraLocal: Neither condition/address writes nor flow words prove an output write.
    swizzles.fill(15);
    for (u32 opcode : {0x12U, 0x2eU, 0x2fU, 0x20U, 0x21U, 0x22U, 0x23U, 0x24U,
                      0x25U, 0x26U, 0x27U, 0x28U, 0x29U, 0x2aU, 0x2bU, 0x2cU, 0x2dU}) {
        program[2048] = opcode << 26;
        Check(PossibleOutputWWrites(program, swizzles) == 0, "nondestination opcode wrote W");
    }
    for (u32 opcode : {0x10U, 0x11U, 0x14U, 0x15U, 0x16U, 0x17U, 0x1cU, 0x1dU, 0x1eU, 0x1fU}) {
        program[2048] = opcode << 26;
        Check(PossibleOutputWWrites(program, swizzles) == 0xffff,
              "unknown words incorrectly proved an absent W write");
    }
}

// CodexAstraLocal: A possible conditional write is deliberately outside the
// absence proof. Show real per-invocation carry so nobody interprets admission
// as full CPU/GPU equivalence; an unreachable write likewise prevents rejection.
void TestProofLimits() {
    Vulkan::RasterizerVulkan renderer;
    InitFixture(renderer, false);
    auto& setup = renderer.pica.vs_setup;
    Assemble(setup, {{O::MOV, D::MakeOutput(3), "xyz", S::MakeInput(0)}, {O::NOP}, {O::NOP},
                    {O::MOV, D::MakeOutput(3), "w", S::MakeInput(0)}, {O::END}});
    nihstro::Instruction compare{};
    compare.opcode = O::CMP;
    compare.common.src1 = S::MakeInput(0);
    compare.common.src2 = S::MakeTemporary(0);
    compare.common.compare_op.x = nihstro::Instruction::Common::CompareOpType::GreaterThan;
    compare.common.compare_op.y = nihstro::Instruction::Common::CompareOpType::Equal;
    setup.UpdateProgramCode(1, compare.hex);
    nihstro::Instruction branch{};
    branch.opcode = O::IFC;
    branch.flow_control.op = nihstro::Instruction::FlowControlType::Op::JustX;
    branch.flow_control.refx = true;
    branch.flow_control.dest_offset = 4;
    setup.UpdateProgramCode(2, branch.hex);
    Shader::InterpreterEngine engine;
    engine.SetupBatch(setup, 0);
    ShaderUnit unit;
    const std::array expected{0.f, .75f, .75f};
    for (u32 invocation = 0; invocation < 3; ++invocation) {
        unit.input[0].x = f24::FromFloat32(invocation == 1 ? 1.f : -1.f);
        unit.input[0].w = f24::FromFloat32(invocation == 1 ? .75f : .25f);
        engine.Run(setup, unit);
        Check(CpuOutput(unit, renderer.regs).quat.w.ToFloat32() == expected[invocation],
              "production conditional-output carry changed");
    }
    Check(renderer.AccelerateDrawBatchReady(false), "possible conditional write was overclaimed");
    ShaderUnit next_batch;
    next_batch.input[0] = unit.input[0];
    engine.Run(setup, next_batch);
    Check(CpuOutput(next_batch, renderer.regs).quat.w.ToFloat32() == 0.f,
          "new batch unexpectedly inherited prior output values");

    setup.UpdateProgramCode(0, static_cast<u32>(O::END) << 26);
    Check(renderer.AccelerateDrawBatchReady(false), "unreachable possible write was ignored");
    // CodexAstraLocal: A first-word END is insufficient; no entry-point traversal is promised.
    Check(renderer.ready_vertex_output_rejections == 0, "proof-limit fixture was rejected");

    // CodexAstraLocal: Unknown stale words after END suppress the absence proof,
    // even when the executed CPU program still leaves its consumed output W zero.
    Vulkan::RasterizerVulkan unknown_tail;
    InitFixture(unknown_tail, false);
    unknown_tail.pica.vs_setup.UpdateProgramCode(MAX_PROGRAM_CODE_LENGTH - 1, 0x10U << 26);
    Check(unknown_tail.AccelerateDrawBatchReady(false), "unknown tail was treated as no output write");
}

// CodexAstraLocal: Hashes are hints. Force collisions and mutate caller storage,
// including normally unused tails, then exercise exact-hit reuse and FIFO eviction.
void TestMemoization() {
    ProgramCode program{};
    SwizzleData swizzles{};
    program.fill(static_cast<u32>(O::NOP) << 26);
    OutputWriteMemo<2> memo;
    Check(memo.Get(program, swizzles, 7, 9) == 0, "empty memo verdict changed");
    Check(memo.Get(program, swizzles, 7, 9) == 0 && memo.Hits() == 1 && memo.Scans() == 1,
          "exact memo reuse failed");
    nihstro::Instruction mov{};
    mov.opcode = O::MOV;
    mov.common.dest = D::MakeOutput(15);
    mov.common.operand_desc_id = 127;
    program.back() = mov.hex;
    swizzles[127] = 1;
    Check(memo.Get(program, swizzles, 7, 9) == 0x8000 && memo.Scans() == 2,
          "hash collision or in-place program mutation reused stale absence");
    swizzles[127] = 14;
    Check(memo.Get(program, swizzles, 7, 9) == 0 && memo.Scans() == 3 && memo.Size() == 2,
          "swizzle mutation or FIFO bound changed");
    program.fill(static_cast<u32>(O::NOP) << 26);
    swizzles.fill(0);
    Check(memo.Get(program, swizzles, 7, 9) == 0 && memo.Scans() == 4,
          "oldest entry was not evicted");
    for (u32 repeat = 0; repeat < 1000; ++repeat)
        Check(memo.Get(program, swizzles, 7, 9) == 0, "hot memo verdict changed");
    Check(memo.Scans() == 4 && memo.Hits() == 1001, "hot memo repeated the instruction scan");

    // CodexAstraLocal: Verify the production default capacity, not only the small test cache.
    OutputWriteMemo<> bounded;
    for (u32 id = 0; id < 130; ++id) {
        swizzles.back() = id;
        Check(bounded.Get(program, swizzles, id, id) == 0, "unused swizzle changed verdict");
    }
    Check(bounded.Size() == 128 && bounded.Scans() == 130, "production memo storage was unbounded");
}
} // CodexAstraLocal: anonymous test namespace

int main(int argc, char** argv) {
    // CodexAstraLocal: Fail independently at real admission before the larger decoder corpus.
    Check(argc == 2, "expected fixture output directory");
    TestDefaultAndAdmission(argv[1]);
    TestSemanticMapping();
    TestInstructionDecoding();
    TestProofLimits();
    TestMemoization();
    std::puts("PASS: production CPU output lifetime/defaults and optional admission; "
              "256 production mapping differentials; arithmetic/MAD/unknown decoding; "
              "conditional/unreachable proof limits; bounded collision-safe memoization");
}
