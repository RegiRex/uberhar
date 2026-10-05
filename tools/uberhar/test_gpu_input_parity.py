#!/usr/bin/env python3
"""CodexAstraUlt-2: Execute production optional routing and GPU input packing.

Real PICA register types, NativeVertexInputPlan and ShaderUnit supply CPU inputs.
Only the rasterizer/device/buffer shell and unused serialization/logging interfaces
are modeled. This regression proves input transport and fallback ordering, not
GPU shader execution, Dark Moon scene coverage, or device image parity.
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
    std::tuple<u8*,u64,bool> Map(u32 size,u32 alignment) {
        mapped=alignment ? Common::AlignUp(cursor,alignment) : cursor;
        if (mapped+size > bytes.size()) throw std::runtime_error("stream capacity");
        return {bytes.data()+mapped,mapped,false};
    }
    void Commit(u32 size) { cursor=mapped+size; }
};
struct RasterizerVulkan {
    struct { Pica::PipelineRegs pipeline{}; Pica::ShaderRegs vs{}; } regs;
    struct PicaShell {
        Pica::AttributeBuffer input_default_attributes{};
        auto GetReadyGpuVertexAdmission() const {
            return ReadyVertexPolicy::Classify(true,false,true,false,
                Pica::PipelineRegs::TriangleTopology::List,96,false,true);
        }
    } pica;
    struct { struct { VertexLayout vertex_layout; } state; } pipeline_info;
    struct { u32 vs_input_index_min{}, vs_input_index_max{95}, vs_input_size{1536}; } vertex_info;
    struct { u32 GetMinVertexStrideAlignment() const { return 1; } } instance;
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
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", *flags,
                    f"-I{stubs}", "-Isrc", str(cpp),
                    "src/video_core/pica/shader_unit.cpp", "-o", str(args.output)], check=True)
    subprocess.run([str(args.output)], check=True)


if __name__ == "__main__":
    main()
