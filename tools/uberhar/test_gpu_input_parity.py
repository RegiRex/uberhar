#!/usr/bin/env python3
"""CodexAstraUlt-2: Execute production optional routing and GPU input packing.

Real PICA register types, NativeVertexInputPlan and ShaderUnit supply CPU inputs.
Only the rasterizer/device/buffer shell and unused serialization/logging interfaces
are modeled. This regression proves input transport and fallback ordering, not
GPU shader execution, Dark Moon scene coverage, or device image parity.
CodexAstraUlt: Also reproduce default/stream conflicts, incomplete copied vertex
tails and aliased input-register ordering before testing the early CPU fallback.
"""
import argparse
import os
from pathlib import Path
import subprocess


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("src/video_core/renderer_vulkan/vk_rasterizer.cpp"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/gpu-input-parity"))
    args = parser.parse_args()
    source = args.source.read_text()
    functions = "\n".join(extract_function(source, signature) for signature in (
        "void RasterizerVulkan::SetupVertexArray()",
        "void RasterizerVulkan::SetupFixedAttribs()",
        "bool RasterizerVulkan::AccelerateDrawBatchReady(bool is_indexed)"))
    header = r'''

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
#include "common/scope_exit.h"
#include <tuple>
#include <vector>
#include "common/alignment.h"
#include "common/logging/log.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h"
// CodexAstraLocal: The extracted admission now also contains the production output guard.
#include "video_core/renderer_vulkan/uberhar_gpu_output_policy.h"

namespace Common::Log { void Stop() {} }

// CodexAstraUlt-2: Model device, cache and buffer plumbing; extract production routing/packing.
namespace Vulkan {
struct Assigned {
    u32 value{};
    template<typename T> void Assign(T v) { value=static_cast<u32>(v); }
    operator u32() const { return value; }
};
struct VertexAttribute { Assigned binding, location, offset, type, size; };
struct VertexBinding { Assigned binding, fixed, byte_count; };
struct VertexLayout {
    u32 binding_count{}, attribute_count{};
    std::array<VertexAttribute,16> attributes;
    std::array<VertexBinding,16> bindings;
};
struct MemoryRef {
    u8* ptr{}; size_t size{};
    u8* GetPtr() const { return ptr; }
    size_t GetSize() const { return size; }
};
struct TestMemory {
    static constexpr PAddr Base=0x1000;
    std::array<u8,16384> bytes{};
    u32 reads{};
    MemoryRef GetPhysicalRef(PAddr address) {
        ++reads;
        if (address < Base || address >= Base+bytes.size()) return {};
        return {bytes.data()+address-Base, bytes.size()-(address-Base)};
    }
};
struct TestStream {
    std::array<u8,32768> bytes{};
    u32 cursor{}, mapped{};
    // CodexAstraUlt: Observe even an uncommitted speculative upload reservation.
    u32 maps{};
    std::tuple<u8*,u64,bool> Map(u32 size,u32 alignment) {
        ++maps;
        mapped=alignment ? Common::AlignUp(cursor,alignment) : cursor;
        if (mapped+size > bytes.size()) throw std::runtime_error("stream capacity");
        return {bytes.data()+mapped,mapped,false};
    }
    void Commit(u32 size) { cursor=mapped+size; }
};
struct RasterizerVulkan {
    struct {
        Pica::PipelineRegs pipeline{};
        Pica::ShaderRegs vs{};
        // CodexAstraLocal: Existing input fixtures consume no output semantics.
        Pica::RasterizerRegs rasterizer{};
        // CodexAstraUlt: Existing transport fixtures are unlit; new cases vary this explicitly.
        struct { bool disable{true}; } lighting;
    } regs;
    struct PicaShell {
        // CodexAstraUlt: Observe that Custom bypasses optional admission before capability checks.
        bool automatic{true};
        Pica::AttributeBuffer input_default_attributes{};
        // CodexAstraLocal: Real shader storage keeps the new guard link-compatible.
        Pica::ShaderSetup vs_setup;
        auto GetReadyGpuVertexAdmission() const {
            return ReadyVertexPolicy::Classify(automatic,false,true,false,
                Pica::PipelineRegs::TriangleTopology::List,96,false,true);
        }
    } pica;
    struct { struct { VertexLayout vertex_layout; } state; } pipeline_info;
    struct { u32 vs_input_index_min{}, vs_input_index_max{95}, vs_input_size{1536}; } vertex_info;
    struct {
        u32 GetMinVertexStrideAlignment() const { return 1; }
        // CodexAstraUlt: Model available correction paths without a physical device.
        bool geometry{}, barycentric{};
        bool UseGeometryShaders() const { return geometry; }
        bool IsFragmentShaderBarycentricSupported() const { return barycentric; }
    } instance;
    struct { void FlushRegion(PAddr,u32) {} } res_cache;
    TestMemory memory;
    TestStream stream_buffer;
    std::array<bool,16> enable_attributes{};
    std::array<u32,16> binding_offsets{};
    u32 fixed_attribute_max_bytes{}, fixed_attribute_over_legacy{};
    struct {
        u32 preflights{};
        u64 GetProgramID() const { return 0; }
        template<typename Regs> bool ReadyGpuFragmentPreflight(const Regs&, bool) {
            ++preflights;
            return true;
        }
    } pipeline_cache;
    bool user_config{}, ready_vertex_attempt{};
    void* ready_vertex_pipeline{};
    std::vector<u32> vertex_batch;
    u64 ready_vertex_zero_stride_rejections{};
    // CodexAstraUlt: Match the production quaternion fallback's bounded counter.
    u64 ready_vertex_quaternion_rejections{};
    // CodexAstraLocal: Match recovered output-guard state without changing input cases.
    ReadyVertexPolicy::OutputWriteMemo<> ready_vertex_output_writes;
    u64 ready_vertex_output_checks{}, ready_vertex_output_w_checks{},
        ready_vertex_output_rejections{};
    // CodexAstraUlt: Match the production per-reason accounting without device plumbing.
    std::array<u64, static_cast<std::size_t>(ReadyVertexPolicy::InputLayoutIssue::Count)>
        ready_vertex_layout_rejections{};
    u32 accelerated{};
    bool AccelerateDrawBatchReady(bool indexed);
    bool AccelerateDrawBatch(bool) {
        ++accelerated;
        SetupVertexArray();
        return true;
    }
    void SetupVertexArray();
    void SetupFixedAttribs();
};
'''
    tests = r'''

} // CodexAstraUlt-2: namespace Vulkan

void Check(bool result,const char* why) {
    if (!result) throw std::runtime_error(why);
}

// CodexAstraUlt: These fixtures use real CPU input conversion and the extracted
// GPU uploader. Read staged float attributes with Vulkan's missing-component
// defaults; no GPU execution or title-specific coverage is implied.
using Float4 = std::array<float,4>;
void InitInputFixture(Vulkan::RasterizerVulkan& renderer,u32 count) {
    using Format=Pica::PipelineRegs::VertexAttributeFormat;
    auto& attrs=renderer.regs.pipeline.vertex_attributes;
    attrs.base_address.Assign(Vulkan::TestMemory::Base/16);
    attrs.max_attribute_index.Assign(count-1);
    attrs.format0.Assign(Format::FLOAT); attrs.size0.Assign(3);
    attrs.format1.Assign(Format::FLOAT); attrs.size1.Assign(3);
    renderer.regs.vs.max_input_attribute_index.Assign(count-1);
    renderer.regs.vs.input_attribute_to_register_map_low=0x10;
    renderer.regs.pipeline.num_vertices=96;
    renderer.vertex_info.vs_input_size=16*96*count;
    auto& loader=attrs.attribute_loaders[0];
    loader.component_count.Assign(1); loader.comp0.Assign(0); loader.byte_count.Assign(16);
}
Float4 ReadCpu(Vulkan::RasterizerVulkan& renderer,
               std::span<const Pica::NativeInputAttribute> descriptions,u32 reg,u32 vertex) {
    Pica::NativeVertexInputPlan plan;
    Check(plan.Prepare(renderer.regs.vs,descriptions.size(),Vulkan::TestMemory::Base,95,
        [&](u32 attribute){return descriptions[attribute];},
        [&](PAddr address)->std::span<const u8>{
            auto ref=renderer.memory.GetPhysicalRef(address);
            return {ref.GetPtr(),ref.GetSize()};
        })==Pica::NativeVertexInputPlan::Result::Ready,"CPU rejected parity fixture");
    Pica::ShaderUnit cpu;
    plan.Load(cpu,renderer.pica.input_default_attributes,vertex);
    const auto& result=cpu.input[reg];
    return {result.x.ToFloat32(),result.y.ToFloat32(),result.z.ToFloat32(),result.w.ToFloat32()};
}
Float4 ReadGpu(Vulkan::RasterizerVulkan& renderer,u32 reg,u32 vertex) {
    const auto& layout=renderer.pipeline_info.state.vertex_layout;
    const auto& attribute=layout.attributes[reg];
    const auto& binding=layout.bindings[attribute.binding];
    Check(attribute.type==static_cast<u32>(Pica::PipelineRegs::VertexAttributeFormat::FLOAT),
          "Fixture reader only models float attributes");
    const u32 address=renderer.binding_offsets[attribute.binding]+attribute.offset+
        (binding.fixed ? 0 : vertex*static_cast<u32>(binding.byte_count));
    Float4 result{0,0,0,1};
    std::memcpy(result.data(),renderer.stream_buffer.bytes.data()+address,attribute.size*sizeof(float));
    return result;
}
void CheckRejected(Vulkan::RasterizerVulkan& renderer,Vulkan::ReadyVertexPolicy::InputLayoutIssue issue) {
    renderer.memory.reads=0;
    const u32 cursor=renderer.stream_buffer.cursor, maps=renderer.stream_buffer.maps;
    const u32 records=TestLog::info_lines;
    for (u32 retry=0;retry<10;retry++) {
        Check(!renderer.AccelerateDrawBatchReady(retry%2),"Unsafe input reached optional GPU route");
        Check(!renderer.ready_vertex_attempt && !renderer.ready_vertex_pipeline,
              "Input fallback latched speculative state");
    }
    Check(renderer.memory.reads==0 && renderer.pipeline_cache.preflights==0 &&
          renderer.accelerated==0 && renderer.stream_buffer.cursor==cursor &&
          renderer.stream_buffer.maps==maps,"Input fallback performed speculative GPU work");
    Check(renderer.ready_vertex_layout_rejections[static_cast<std::size_t>(issue)]==10 &&
          renderer.ready_vertex_zero_stride_rejections==0 && TestLog::info_lines-records==4,
          "Input reason accounting or four-record limit changed");
}
void CheckOrdinary(Vulkan::RasterizerVulkan& renderer,
                   std::span<const Pica::NativeInputAttribute> descriptions) {
    const auto counters=renderer.ready_vertex_layout_rejections;
    const u32 records=TestLog::info_lines;
    Check(renderer.AccelerateDrawBatchReady(false),"Ordinary input layout lost GPU eligibility");
    Check(renderer.accelerated==1 && renderer.pipeline_cache.preflights==1 &&
          !renderer.ready_vertex_attempt && !renderer.ready_vertex_pipeline,
          "Ordinary input changed acceleration or speculative cleanup");
    Check(renderer.ready_vertex_layout_rejections==counters && TestLog::info_lines==records,
          "Ordinary input changed fallback accounting");
    for (u32 attr=0;attr<=renderer.regs.vs.max_input_attribute_index;attr++) {
        const auto reg=renderer.regs.vs.GetRegisterForAttribute(attr);
        for (u32 vertex=0;vertex<96;vertex++)
            Check(ReadCpu(renderer,descriptions,reg,vertex)==ReadGpu(renderer,reg,vertex),
                  "Ordinary CPU/GPU input parity changed");
    }
}

// CodexAstraUlt: Each unsafe fixture first proves the existing uploader diverges,
// then checks production optional routing rejects it before work. Its repaired
// ordinary counterpart must still accelerate with all 96 CPU/GPU inputs equal.
void TestAdditionalInputLayouts() {
    using Format=Pica::PipelineRegs::VertexAttributeFormat;
    using Issue=Vulkan::ReadyVertexPolicy::InputLayoutIssue;
    {
        Vulkan::RasterizerVulkan renderer;
        InitInputFixture(renderer,1);
        auto& attrs=renderer.regs.pipeline.vertex_attributes;
        attrs.attribute_mask.Assign(1);
        renderer.pica.input_default_attributes[0].x=Pica::f24::FromFloat32(9);
        const float source=2;
        std::memcpy(renderer.memory.bytes.data(),&source,sizeof(source));
        std::array descriptions{Pica::NativeInputAttribute{0,16,4,Format::FLOAT,true}};
        const auto cpu=ReadCpu(renderer,descriptions,0,0);
        renderer.SetupVertexArray();
        Check(cpu[0]==9 && ReadGpu(renderer,0,0)[0]==2,
              "Default/stream conflict no longer reproduces; revisit fallback");
        CheckRejected(renderer,Issue::DefaultAttribute);
        attrs.attribute_mask.Assign(0); descriptions[0].is_default=false;
        CheckOrdinary(renderer,descriptions);
    }
    {
        Vulkan::RasterizerVulkan renderer;
        InitInputFixture(renderer,1);
        auto& attrs=renderer.regs.pipeline.vertex_attributes;
        attrs.attribute_loaders[0].byte_count.Assign(12);
        renderer.vertex_info.vs_input_size=12*96;
        const Float4 source{2,3,4,5};
        std::memcpy(renderer.memory.bytes.data()+95*12,source.data(),sizeof(source));
        std::array descriptions{Pica::NativeInputAttribute{0,12,4,Format::FLOAT,false}};
        const auto cpu=ReadCpu(renderer,descriptions,0,95);
        renderer.SetupVertexArray();
        Check(cpu==source && ReadGpu(renderer,0,95)==Float4{2,3,4,0},
              "Short-stride copied-tail conflict no longer reproduces; revisit fallback");
        CheckRejected(renderer,Issue::ShortStride);
        attrs.size0.Assign(2); descriptions[0].elements=3;
        CheckOrdinary(renderer,descriptions);
    }
    {
        Vulkan::RasterizerVulkan renderer;
        InitInputFixture(renderer,2);
        auto& attrs=renderer.regs.pipeline.vertex_attributes;
        attrs.attribute_loaders[0].comp0.Assign(1);
        auto& second=attrs.attribute_loaders[1];
        second.component_count.Assign(1); second.comp0.Assign(0); second.byte_count.Assign(16);
        second.data_offset.Assign(8192);
        renderer.regs.vs.input_attribute_to_register_map_low=0;
        const float one=1, two=2;
        std::memcpy(renderer.memory.bytes.data(),&one,sizeof(one));
        std::memcpy(renderer.memory.bytes.data()+8192,&two,sizeof(two));
        const std::array descriptions{Pica::NativeInputAttribute{8192,16,4,Format::FLOAT,false},
                                      Pica::NativeInputAttribute{0,16,4,Format::FLOAT,false}};
        const auto cpu=ReadCpu(renderer,descriptions,0,0);
        renderer.SetupVertexArray();
        Check(cpu[0]==1 && ReadGpu(renderer,0,0)[0]==2,
              "Reversed-loader register conflict no longer reproduces; revisit fallback");
        CheckRejected(renderer,Issue::RegisterAlias);
        renderer.regs.vs.input_attribute_to_register_map_low=0x10;
        CheckOrdinary(renderer,descriptions);
    }
    {
        Vulkan::RasterizerVulkan renderer;
        InitInputFixture(renderer,2);
        renderer.regs.vs.max_input_attribute_index.Assign(0);
        renderer.regs.vs.input_attribute_to_register_map_low=0;
        auto& second=renderer.regs.pipeline.vertex_attributes.attribute_loaders[1];
        second.component_count.Assign(1); second.comp0.Assign(1); second.byte_count.Assign(16);
        second.data_offset.Assign(8192);
        const float one=1, two=2;
        std::memcpy(renderer.memory.bytes.data(),&one,sizeof(one));
        std::memcpy(renderer.memory.bytes.data()+8192,&two,sizeof(two));
        const std::array descriptions{Pica::NativeInputAttribute{0,16,4,Format::FLOAT,false},
                                      Pica::NativeInputAttribute{8192,16,4,Format::FLOAT,false}};
        const auto cpu=ReadCpu(renderer,descriptions,0,0);
        renderer.SetupVertexArray();
        Check(cpu[0]==1 && ReadGpu(renderer,0,0)[0]==2,
              "Unrequested-loader register conflict no longer reproduces; revisit fallback");
        CheckRejected(renderer,Issue::RegisterAlias);
        renderer.regs.vs.input_attribute_to_register_map_low=0x10;
        CheckOrdinary(renderer,descriptions);
    }
    {
        // CodexAstraUlt: Leading alignment padding and unused trailing padding
        // are safe when all decoded attribute bytes fit the copied stride.
        Vulkan::RasterizerVulkan renderer;
        InitInputFixture(renderer,1);
        auto& loader=renderer.regs.pipeline.vertex_attributes.attribute_loaders[0];
        loader.comp0.Assign(12); loader.comp1.Assign(0); loader.comp2.Assign(15);
        loader.component_count.Assign(3); loader.byte_count.Assign(20);
        renderer.vertex_info.vs_input_size=20*96;
        const Float4 source{2,3,4,5};
        std::memcpy(renderer.memory.bytes.data()+95*20+4,source.data(),sizeof(source));
        const std::array descriptions{Pica::NativeInputAttribute{4,20,4,Format::FLOAT,false}};
        CheckOrdinary(renderer,descriptions);
    }
}
// CodexAstraUlt: Exercise the actual optional admission body across the five useful
// lighting/correction combinations. Rejected draws cannot read indices, warm fragments
// or reserve uploads; Custom remains outside this optional route entirely.
void TestQuaternionAdmission() {
    struct Case { bool lighting, geometry, barycentric, expected; };
    constexpr std::array cases{
        Case{false,false,false,true}, Case{true,false,false,false},
        Case{true,true,false,true}, Case{true,false,true,true}, Case{true,true,true,true}};
    for (const auto& c : cases) {
        Check(Vulkan::ReadyVertexPolicy::CanPreserveQuaternionInterpolation(
                  c.lighting,c.geometry,c.barycentric)==c.expected,"Quaternion policy changed");
        for (bool optional : {false,true}) {
            for (bool indexed : {false,true}) {
                Vulkan::RasterizerVulkan renderer;
                InitInputFixture(renderer,1);
                renderer.pica.automatic=optional;
                renderer.regs.lighting.disable=!c.lighting;
                renderer.instance.geometry=c.geometry;
                renderer.instance.barycentric=c.barycentric;
                const auto records=TestLog::info_lines;
                if (optional && c.expected) {
                    Check(renderer.AccelerateDrawBatchReady(indexed),
                          "Available correction or unlit draw lost admission");
                    Check(renderer.accelerated==1 && renderer.pipeline_cache.preflights==1 &&
                          renderer.ready_vertex_quaternion_rejections==0 &&
                          TestLog::info_lines==records,"Ordinary route changed fallback accounting");
                } else {
                    for (unsigned retry=0;retry<10;++retry)
                        Check(!renderer.AccelerateDrawBatchReady(indexed),
                              "Unsafe quaternion interpolation reached optional GPU route");
                    Check(renderer.memory.reads==0 && renderer.stream_buffer.maps==0 &&
                          renderer.pipeline_cache.preflights==0 && renderer.accelerated==0 &&
                          !renderer.ready_vertex_attempt && !renderer.ready_vertex_pipeline,
                          "Quaternion rejection performed speculative work");
                    Check(renderer.ready_vertex_quaternion_rejections==(optional ? 10U : 0U) &&
                          TestLog::info_lines-records==(optional ? 4U : 0U),
                          "Quaternion first-four limit or Custom admission changed");
                }
            }
        }
    }
    std::puts("PASS: five quaternion capability cases; indexed/nonindexed admission; "
              "early full CPU fallback, four-record limit and Custom exclusion");
}
int main() {
    using Format=Pica::PipelineRegs::VertexAttributeFormat;
    using namespace Vulkan::ReadyVertexPolicy;
    Vulkan::RasterizerVulkan renderer;
    auto& attrs=renderer.regs.pipeline.vertex_attributes;
    attrs.base_address.Assign(Vulkan::TestMemory::Base/16);
    attrs.max_attribute_index.Assign(1);
    attrs.format0.Assign(Format::FLOAT); attrs.size0.Assign(3);
    attrs.format1.Assign(Format::FLOAT); attrs.size1.Assign(3);
    attrs.attribute_mask.Assign(0);
    auto& position=attrs.attribute_loaders[0];
    position.component_count.Assign(1); position.comp0.Assign(0); position.byte_count.Assign(16);
    auto& color=attrs.attribute_loaders[1];
    color.component_count.Assign(1); color.comp0.Assign(1); color.byte_count.Assign(0);
    color.data_offset.Assign(8192);
    renderer.regs.pipeline.num_vertices=96;
    renderer.regs.vs.max_input_attribute_index.Assign(1);
    renderer.regs.vs.input_attribute_to_register_map_low=0x10;
    const std::array<float,4> expected_color{1.0f,0.25f,0.5f,1.0f};
    for (u32 i=0;i<96;i++) {
        const std::array<float,4> pos{static_cast<float>(i),0.0f,0.0f,1.0f};
        std::memcpy(renderer.memory.bytes.data()+i*16,pos.data(),sizeof(pos));
        std::memcpy(renderer.memory.bytes.data()+8192+i*16,expected_color.data(),sizeof(expected_color));
    }
    Pica::NativeVertexInputPlan plan;
    const auto result=plan.Prepare(renderer.regs.vs,2,Vulkan::TestMemory::Base,95,
        [&](u32 attr)->Pica::NativeInputAttribute {
            return attr==0 ? Pica::NativeInputAttribute{0,16,4,Format::FLOAT,false}
                           : Pica::NativeInputAttribute{8192,0,4,Format::FLOAT,false};
        },[&](PAddr address)->std::span<const u8> {
            auto ref=renderer.memory.GetPhysicalRef(address);
            return {ref.GetPtr(),ref.GetSize()};
        });
    Check(result==Pica::NativeVertexInputPlan::Result::Ready,"CPU rejected zero-stride fixture");
    Check(IsEligible(renderer.pica.GetReadyGpuVertexAdmission()),"Fixture topology is not eligible");

    // CodexAstraUlt-2: Every rejected optional attempt must precede reads and GPU setup.
    renderer.memory.reads=0;
    for (u32 retry=0;retry<10;retry++) {
        Check(!renderer.AccelerateDrawBatchReady(retry%2),"Zero-stride draw reached GPU promotion");
        Check(!renderer.ready_vertex_attempt && !renderer.ready_vertex_pipeline,
              "Rejected input latched speculative state");
    }
    Check(renderer.memory.reads==0 && renderer.pipeline_cache.preflights==0 &&
          renderer.accelerated==0 && renderer.stream_buffer.cursor==0,
          "Zero-stride fallback performed speculative GPU work");
    Check(renderer.ready_vertex_zero_stride_rejections==10 && TestLog::info_lines==4,
          "Fallback count or four-record diagnostic bound changed");

    // CodexAstraUlt-2: Recovery retains live zero-stride values and the complete vertex order.
    Pica::ShaderUnit cpu;
    u32 recovered=0;
    for (u32 vertex=0;vertex<96;vertex++) {
        plan.Load(cpu,renderer.pica.input_default_attributes,vertex);
        Check(cpu.input[0].x.ToFloat32()==float(vertex),"CPU recovery changed vertex order");
        for (u32 i=0;i<4;i++)
            Check(cpu.input[1][i].ToFloat32()==expected_color[i],"CPU lost zero-stride color");
        ++recovered;
    }
    const float live_red=0.75f;
    std::memcpy(renderer.memory.bytes.data()+8192,&live_red,sizeof(live_red));
    plan.Load(cpu,renderer.pica.input_default_attributes,95);
    Check(cpu.input[1].x.ToFloat32()==live_red,"CPU recovery cached zero-stride source bytes");

    // CodexAstraUlt-2: Reproduce the inherited mismatch, independently of the new guard.
    renderer.SetupVertexArray();
    const auto& layout=renderer.pipeline_info.state.vertex_layout;
    const auto& attr=layout.attributes[1];
    const u32 address=renderer.binding_offsets[attr.binding]+attr.offset;
    std::array<float,4> gpu{};
    std::memcpy(gpu.data(),renderer.stream_buffer.bytes.data()+address,sizeof(gpu));
    Check(gpu==std::array<float,4>{0,0,0,1},"Known inherited fallback packing changed; revisit guard");

    // CodexAstraUlt-2: Normal nonzero streams still reach the original accelerator.
    color.byte_count.Assign(16);
    Check(renderer.AccelerateDrawBatchReady(false),"Normal vertex layout was rejected");
    Check(renderer.accelerated==1 && renderer.pipeline_cache.preflights==1,
          "Normal layout did not preserve accelerated routing");
    Check(!renderer.ready_vertex_attempt && !renderer.ready_vertex_pipeline,
          "Successful attempt leaked speculative state");
    Check(renderer.ready_vertex_zero_stride_rejections==10 && TestLog::info_lines==4,
          "Normal layout changed rejection accounting");

    // CodexAstraUlt-2: Cover every loader slot, inactive descriptors, and legal field limits.
    u32 cases=0;
    for (u32 slot=0;slot<12;slot++) {
        for (u32 components : {0U,1U,12U,15U}) {
            for (u32 stride : {0U,1U,16U,255U}) {
                Pica::PipelineRegs pipeline{};
                auto& loader=pipeline.vertex_attributes.attribute_loaders[slot];
                loader.component_count.Assign(components);
                loader.byte_count.Assign(stride);
                Check(HasActiveZeroStrideLoader(pipeline)==(components!=0 && stride==0),
                      "Zero-stride policy mishandled loader slot or inactive descriptor");
                ++cases;
            }
        }
    }
    std::printf("PASS: %u input-layout cases; %u CPU vertices retained; "
                "zero-stride GPU mismatch quarantined before uploads; four diagnostic records\n",
                cases,recovered);
    // CodexAstraUlt: Keep historical zero-stride assertions independent of the
    // additional fallback records and report only completed differential checks.
    TestAdditionalInputLayouts();
    std::printf("PASS: four additional CPU/GPU divergences quarantined before GPU work; "
                "three reasons with four-record limits; five ordinary layouts retain parity\n");
    TestQuaternionAdmission();
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cpp = args.output.with_suffix(".cpp")
    cpp.write_text(header + functions + tests)
    # CodexAstraUlt-2: These unused interfaces avoid unrelated SDK/dependency setup;
    # CodexAstraUlt-2: actual input conversion, register fields and routing remain production code.
    stubs = args.output.parent / "gpu-input-parity-stubs"
    definitions = {
        "boost/serialization/access.hpp":
            "#pragma once\nnamespace boost::serialization { class access {}; }\n",
        "boost/serialization/base_object.hpp":
            "#pragma once\nnamespace boost::serialization { template<typename Base,typename Derived> "
            "Base& base_object(Derived& d) { return static_cast<Base&>(d); } }\n",
        # CodexAstraLocal: ShaderSetup's packed-uniform type includes this unused interface.
        "boost/serialization/binary_object.hpp":
            "#pragma once\n#include <boost/serialization/access.hpp>\n",
        "common/logging/log.h":
            "#pragma once\n#include <stdexcept>\n"
            "namespace TestLog { inline unsigned info_lines{}; }\n"
            "#define LOG_INFO(...) (++TestLog::info_lines)\n"
            '#define LOG_ERROR(...) throw std::runtime_error("unexpected error log")\n'
            '#define LOG_CRITICAL(...) throw std::runtime_error("unexpected critical log")\n',
    }
    for name, text in definitions.items():
        path = stubs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    # CodexAstraLocal: Link real shader storage/hashing for the newly extracted output guard.
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", *flags,
                    f"-I{stubs}", "-Isrc", "-Iexternals/nihstro/include",
                    "-Iexternals/xxHash", "-DXXH_INLINE_ALL", str(cpp),
                    "src/video_core/pica/shader_unit.cpp",
                    "src/video_core/pica/shader_setup.cpp", "-o", str(args.output)], check=True)
    subprocess.run([str(args.output)], check=True)


if __name__ == "__main__":
    main()
