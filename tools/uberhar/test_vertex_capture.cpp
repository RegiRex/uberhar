// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Drive production Session APIs and independently inspect their
// binary artifacts. Only filesystem/provider and mapped-buffer plumbing is modeled.
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>
#include <json.hpp>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_vertex_capture.h"

namespace Common {
const char g_scm_rev[] = "capture-regression-fixture";
const char g_build_version[] = "capture-test";
}
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*,
                      fmt::string_view, const fmt::format_args&) {}
}

namespace {
using namespace Vulkan;
using namespace Vulkan::VertexCapture;
using Json = nlohmann::json;
constexpr u64 Title = 0x0004000000055f00ULL, Run = 19;
unsigned checks{};
void Check(bool condition, const char* why) {
    ++checks;
    if (!condition) throw std::runtime_error(why);
}
std::filesystem::path Artifact(const std::string& id) {
    return std::filesystem::path(FileUtil::root) / "dump/uberhar_vertex_capture" / (id+".uvc");
}
void WriteConfig(const Json& config) {
    std::ofstream{std::filesystem::path(FileUtil::root)/"config/uberhar_vertex_capture.json"}
        << config.dump();
}
Json Config(std::string id, bool discover = true, u32 width = 4, u32 per_swap = 2) {
    return {{"schema",1}, {"enabled",true}, {"capture_id",id},
        {"mode",discover ? "discover" : "capture"}, {"title_id","0004000000055f00"},
        {"trigger","next_gameplay_transition"}, {"delay_swaps",0},
        {"window_swaps",width}, {"packets_per_swap",per_swap},
        {"selector",{{"attempt_ordinal",0},{"color_addresses",Json::array()}}}};
}

// CodexAstraLocal: Read the protocol independently of its producer's Writer;
// exact lengths and section bounds must hold even for canceled/partial evidence.
struct Result {
    Json manifest;
    std::vector<u8> payload;
    std::size_t manifest_bytes{};
};
Result Read(const std::string& id) {
    std::ifstream input{Artifact(id),std::ios::binary};
    Check(input.good(), "expected bounded artifact absent");
    std::vector<u8> bytes{std::istreambuf_iterator<char>(input),{}};
    Check(bytes.size()>=16 && bytes.size()<=MaxBytes,"artifact length violates cap");
    Check(std::memcmp(bytes.data(),"UBVCAP01",8)==0,"wrong protocol magic");
    auto u32at=[&](u32 at) {
        return u32{bytes[at]} | (u32{bytes[at+1]}<<8) | (u32{bytes[at+2]}<<16) |
            (u32{bytes[at+3]}<<24);
    };
    const auto json_size=u32at(8), payload_size=u32at(12);
    Check(json_size<=MaxManifestBytes && payload_size<=MaxPayloadBytes,
          "header sizes violate independent caps");
    Check(u64{16}+json_size+payload_size==bytes.size(),"artifact length/header disagree");
    Result result{Json::parse(bytes.begin()+16,bytes.begin()+16+json_size),
        {bytes.begin()+16+json_size,bytes.end()},json_size};
    for(const auto& packet:result.manifest.at("packets")) {
        u64 total{};
        for(const auto& section:packet.at("sections")) {
            const u64 offset=section.at("offset"), size=section.at("size");
            Check(offset<=result.payload.size() && size<=result.payload.size()-offset,
                  "section outside actual payload");
            total+=size;
        }
        Check(total<=MaxPacketBytes,"one packet exceeds cap");
    }
    return result;
}
std::span<const u8> Section(const Result& r, u32 index, const char* name) {
    const auto& section=r.manifest.at("packets").at(index).at("sections").at(name);
    return {r.payload.data()+section.at("offset").get<std::size_t>(),
            section.at("size").get<std::size_t>()};
}

// CodexAstraLocal: Real register/hash/layout types with deliberately different
// live PICA intentions and bound UBO contents expose accidental reconstruction.
struct Fixture {
    Pica::RegsInternal regs{};
    Pica::ShaderSetup setup;
    Pica::AttributeBuffer defaults{};
    std::array<Pica::NativeInputAttribute,16> inputs{};
    BindingState bound;
    StreamBuffer uniform;
    std::array<u8,96> vertices;
    std::array<u8,16> fixed;
    Fixture() {
        regs.pipeline.num_vertices=3;
        regs.pipeline.vertex_attributes.attribute_loaders[0].component_count.Assign(1);
        regs.pipeline.vertex_attributes.attribute_loaders[0].byte_count.Assign(16);
        regs.vs.main_offset.Assign(0);
        setup.UpdateProgramCode(0,0);
        setup.UpdateSwizzleData(0,0);
        inputs[0]={0,16,4,Pica::PipelineRegs::VertexAttributeFormat::FLOAT,false};
        for(u32 i=1;i<inputs.size();++i) inputs[i].is_default=true;
        auto& layout=bound.pipeline.state.vertex_layout;
        layout.binding_count=2; layout.attribute_count=16;
        layout.bindings[0].binding.Assign(0);
        layout.bindings[0].byte_count.Assign(16);
        layout.attributes[0].binding.Assign(0);
        layout.attributes[0].location.Assign(0);
        layout.attributes[0].type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
        layout.attributes[0].size.Assign(4);
        // CodexAstraLocal: Match the actual uploader's sixteen locations and
        // shared fixed binding so the independent replay reader accepts this layout.
        layout.bindings[1].binding.Assign(1);
        layout.bindings[1].fixed.Assign(1);
        for(u32 i=1;i<16;++i) {
            layout.attributes[i].binding.Assign(1);
            layout.attributes[i].location.Assign(i);
            layout.attributes[i].type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
            layout.attributes[i].size.Assign(4);
        }
        bound.extra.load_flags.fill(Pica::Shader::Generator::AttribLoadFlags::Float);
        bound.profile.vk_format_traits[15].native_format=
            static_cast<u32>(vk::Format::eR32G32B32A32Sfloat);
        bound.uniform_offsets={512,4096,6144};
        uniform.bytes.fill(0xa5);
        vertices.fill(0xcc); fixed.fill(0x39);
        for(u32 row=0;row<3;++row)
            std::fill_n(vertices.data()+row*32,16,0x10+row);
    }
    Json Configuration(const std::string& id,u32 width=4,u32 per_swap=2) {
        auto c=Config(id,false,width,per_swap);
        c["selector"]["program_hash"]=fmt::format("{:016x}",setup.GetProgramCodeHash());
        c["selector"]["swizzle_hash"]=fmt::format("{:016x}",setup.GetSwizzleDataHash());
        c["selector"]["entry"]=0;
        return c;
    }
    Token Stage(Session& session,u64 tick=10,bool indexed=false,u32 minimum=60000) {
        if(!indexed) regs.pipeline.vertex_offset=minimum;
        session.BeginDraw(regs,setup,indexed);
        session.PreparePayload(regs,setup,defaults,inputs,1,minimum,minimum+2);
        session.CopyVertex(0,0,16,32,3,vertices.data());
        session.CopyFixed(fixed);
        if(indexed) {
            std::array<u8,6> indices{};
            for(u32 i=0;i<3;++i) {
                const u32 value=minimum+i;
                indices[2*i]=value&255; indices[2*i+1]=value>>8;
            }
            session.CopyIndices(2,2,indices);
        }
        auto token=session.Commit(bound,uniform,tick);
        session.EndDraw();
        return token;
    }
};

// CodexAstraLocal: Optional configuration failures must remain local to
// diagnostics, including malformed JSON types, nesting, filesystem exceptions.
void TestConfiguration() {
    Check(!Session::Load(Title,Run,0),"missing file enabled capture");
    auto c=Config("disabled"); c["enabled"]=false; WriteConfig(c);
    Check(!Session::Load(Title,Run,0),"disabled config enabled capture");
    c=Config("wrong-title"); WriteConfig(c);
    Check(!Session::Load(Title+1,Run,0),"different title enabled capture");
    for(const auto& bad:std::array<Json,7>{Json(1.5),Json("1"),Json(true),Json(nullptr),
                                         Json(-1),Json(0),Json(2)}) {
        c=Config("schema"); c["schema"]=bad; WriteConfig(c);
        Check(!Session::Load(Title,Run,0),"invalid schema type/value accepted");
    }
    for(const auto& bad:std::array<Json,4>{Json(nullptr),Json::array(),Json(1),Json("*")}) {
        c=Config("selector"); c["selector"]=bad; WriteConfig(c);
        Check(!Session::Load(Title,Run,0),"invalid selector object accepted");
    }
    for(auto [key,value]:std::array<std::pair<const char*,int>,4>{{
            {"delay_swaps",121},{"window_swaps",0},{"window_swaps",9},{"packets_per_swap",3}}}) {
        c=Config("range"); c[key]=value; WriteConfig(c);
        Check(!Session::Load(Title,Run,0),"out-of-range configuration accepted");
    }
    c=Config("product",true,8,2); WriteConfig(c);
    Check(!Session::Load(Title,Run,0),"packet product cap ignored");
    c=Config("../escape"); WriteConfig(c);
    Check(!Session::Load(Title,Run,0),"unsafe artifact id accepted");
    FileUtil::throw_exists=true;
    Check(!Session::Load(Title,Run,0),"provider exception escaped diagnostics");
    FileUtil::throw_exists=false;
    std::ofstream{std::filesystem::path(FileUtil::root)/"config/uberhar_vertex_capture.json"}
        << std::string(MaxConfigBytes+1,' ');
    Check(!Session::Load(Title,Run,0),"oversized config accepted");
    // CodexAstraLocal: Duplicate keys must fail even when each value is valid;
    // different parsers otherwise disagree about the chosen diagnostic request.
    const auto text=Config("duplicate").dump();
    std::ofstream{std::filesystem::path(FileUtil::root)/"config/uberhar_vertex_capture.json"}
        << "{\"schema\":1," << text.substr(1);
    Check(!Session::Load(Title,Run,0),"duplicate configuration key accepted");
}

// CodexAstraLocal: Mutate all originals after Commit; only actual uploaded
// semantic bytes may survive, while ABI and uploaded-stride padding stay absent.
void TestImmutableAndSubmission() {
    Fixture f;
    WriteConfig(f.Configuration("immutable"));
    auto session=Session::Load(Title,Run,0); Check(bool(session),"valid capture rejected");
    session->NextSwap(Title,Run,3,false,9,9);
    auto token=f.Stage(*session,10,true);
    Check(bool(token),"bounded indexed draw did not stage");
    f.vertices.fill(0xee); f.fixed.fill(0xee); f.uniform.bytes.fill(0xee);
    f.setup.UpdateProgramCode(0,0xffffffffU);
    token.Recorded();
    session->Finish(10,9);
    auto result=Read("immutable");
    const auto& packet=result.manifest["packets"][0];
    Check(packet["recorded"] && packet["accepted"] && !packet["completed"],
          "submission/completion states conflated");
    const auto vertices=Section(result,0,"vertex_0");
    Check(vertices.size()==48,"upload padding serialized");
    for(u32 row=0;row<3;++row)
        Check(std::all_of(vertices.begin()+16*row,vertices.begin()+16*(row+1),
                         [row](u8 b){return b==0x10+row;}),"source mutation affected snapshot");
    Check(Section(result,0,"fixed")[0]==0x39,"fixed input snapshot changed");
    Check(Section(result,0,"program")[0]==0,"program snapshot changed");
    const auto pica=Section(result,0,"vs_pica");
    Check(pica[0]==0xa5 && pica[16]==0xa5,"actual bound UBO was reconstructed");
    Check(std::all_of(pica.begin()+4,pica.begin()+16,[](u8 b){return b==0;}),
          "PICA UBO padding copied");
    const auto extra=Section(result,0,"vs_extra");
    Check(std::all_of(extra.begin()+8,extra.begin()+16,[](u8 b){return b==0;}),
          "extra UBO padding copied");
    Check(packet["draw"]["minimum"]==60000 && packet["draw"]["base_vertex"]==-60000,
          "high-base indices lost rebasing identity");
    const auto writes=FileUtil::writes;
    session->Finish(100,100);
    Check(FileUtil::writes==writes,"second Finish rewrote evidence");
    WriteConfig(f.Configuration("immutable"));
    Check(!Session::Load(Title,Run,0),"existing artifact would be overwritten");

    for(u32 state=0;state<3;++state) {
        Fixture next;
        const auto id="execution-"+std::to_string(state);
        WriteConfig(next.Configuration(id));
        auto owner=Session::Load(Title,Run,0);
        owner->NextSwap(Title,Run,3,false,9,9);
        auto pending=next.Stage(*owner);
        Check(bool(pending),"execution fixture not staged");
        if(state) pending.Recorded();
        owner->Finish(state==2?10:9,100);
        const auto r=Read(id);
        const auto& p=r.manifest["packets"][0];
        Check(p["recorded"]==(state!=0),"canceled draw recorded");
        Check(p["accepted"]==(state==2),"unsubmitted draw accepted");
        Check(p["completed"]==(state==2),"completion bypassed acceptance");
        owner.reset(); pending.Recorded(); // Owner survives until queued token retirement.
    }
}

// CodexAstraLocal: A failed selected attempt keeps its ordinal; the next draw
// cannot silently replace it. Uncapturable inputs and output errors stay bounded.
void TestFailureAndLimits() {
    Fixture f;
    WriteConfig(f.Configuration("failed",1,1));
    auto session=Session::Load(Title,Run,0);
    session->NextSwap(Title,Run,3,false,0,0);
    session->BeginDraw(f.regs,f.setup,false); session->EndDraw();
    Check(!f.Stage(*session),"later draw substituted for failed selected ordinal");
    session->Finish(100,100);
    Check(Read("failed").manifest["packets"].empty(),"failed preflight produced payload");

    for(const auto id:{"sparse","count","ubo"}) {
        Fixture fixture; WriteConfig(fixture.Configuration(id));
        auto owner=Session::Load(Title,Run,0); owner->NextSwap(Title,Run,3,false,0,0);
        if(std::string_view{id}=="ubo") {
            fixture.bound.uniform_offsets[0]=8190;
            Check(!fixture.Stage(*owner),"invalid bound UBO accepted");
        } else {
            if(std::string_view{id}=="count") fixture.regs.pipeline.num_vertices=4097;
            owner->BeginDraw(fixture.regs,fixture.setup,true);
            owner->PreparePayload(fixture.regs,fixture.setup,fixture.defaults,fixture.inputs,1,
                                  0,std::string_view{id}=="sparse"?65535:2);
            Check(!owner->WantsPayload(),"invalid shape left active payload");
            owner->EndDraw();
        }
        owner->Finish(100,100);
        Check(Read(id).manifest["packets"].empty(),"censored draw produced packet");
    }
    WriteConfig(f.Configuration("provider-failure"));
    auto failed=Session::Load(Title,Run,0);
    failed->NextSwap(Title,Run,3,false,0,0); auto t=f.Stage(*failed); t.Recorded();
    FileUtil::fail_directory=true;
    failed->Finish(10,10);
    FileUtil::fail_directory=false;
    Check(!std::filesystem::exists(Artifact("provider-failure")),"failed provider created output");

    // CodexAstraLocal: An identity change closes immediately between swaps;
    // neither later gameplay edges nor watermarks may resume the old session.
    for(bool change_run:{false,true}) {
        const auto id=change_run?"new-run":"new-title";
        WriteConfig(f.Configuration(id));
        auto changed=Session::Load(Title,Run,0);
        changed->NextSwap(Title,Run,3,false,0,0);
        changed->ObserveIdentity(change_run?Title:Title+1,change_run?Run+1:Run);
        Check(!f.Stage(*changed),"identity switch left capture active before next swap");
        changed->NextSwap(Title,Run,0,false,100,100);
        changed->NextSwap(Title,Run,3,false,100,100);
        Check(!f.Stage(*changed),"old session rearmed after identity switch");
        changed->Finish(100,100);
        Check(Read(id).manifest["packets"].empty(),"identity switch fabricated old-run packet");
    }
}

// CodexAstraLocal: Fill the public maximum discovery bank with maximal scalar
// widths. A valid capped capture must still serialize usable prefix evidence.
void TestDiscoveryCapacity() {
    Fixture f; WriteConfig(Config("discovery-max",true,4,2));
    auto owner=Session::Load(Title,Run,0);
    for(u32 interval=0;interval<4;++interval) {
        owner->NextSwap(Title,Run,3,false,std::numeric_limits<u64>::max(),
                        std::numeric_limits<u64>::max());
        for(u32 row=0;row<MaxRows/4+3;++row) {
            f.regs.pipeline.num_vertices=4096-row;
            f.bound.pipeline_key=std::numeric_limits<u64>::max()-row;
            f.bound.shader_ids.fill(std::numeric_limits<u64>::max());
            f.bound.source_hash=std::numeric_limits<u64>::max();
            owner->BeginDraw(f.regs,f.setup,true);
            auto token=owner->Commit(f.bound,f.uniform,std::numeric_limits<u64>::max()-1);
            if(token) token.Recorded();
            owner->EndDraw();
        }
    }
    owner->Finish(std::numeric_limits<u64>::max(),std::numeric_limits<u64>::max());
    const auto result=Read("discovery-max");
    Check(result.manifest["discovery"].size()==MaxRows,"maximum discovery prefix lost");
    Check(result.manifest["summary"]["capped"].get<u64>()>0,"omitted descriptors not counted");
    Check(result.payload.empty(),"discovery captured guest payload");
    std::printf("Discovery maximum manifest: %zu bytes / %zu cap\n",
                result.manifest_bytes,MaxManifestBytes);
}

// CodexAstraLocal: Exercise all eight packet slots with the largest public
// layout/profile metadata, then saturate raw payload allocation independently.
void TestPacketCapacity() {
    Fixture f;
    auto& layout=f.bound.pipeline.state.vertex_layout;
    layout.attribute_count=16;
    layout.binding_count=13;
    for(u32 i=0;i<16;++i) {
        layout.attributes[i].binding.Assign(i%13);
        layout.attributes[i].location.Assign(i);
        layout.attributes[i].type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
        layout.attributes[i].size.Assign(4);
        layout.attributes[i].offset.Assign(255);
        f.inputs[i]={0xfffffff0U,0xffffffffU,4,Pica::PipelineRegs::VertexAttributeFormat::FLOAT,false};
    }
    for(u32 i=0;i<13;++i) {
        layout.bindings[i].binding.Assign(i);
        layout.bindings[i].byte_count.Assign(2047);
    }
    for(auto& trait:f.bound.profile.vk_format_traits) {
        trait.transfer_support=255; trait.blit_support=255; trait.attachment_support=255;
        trait.storage_support=255; trait.needs_conversion=255; trait.needs_emulation=255;
        trait.usage_flags=trait.aspect_flags=trait.native_format=0xffffffffU;
    }
    WriteConfig(f.Configuration("eight-max-metadata"));
    auto owner=Session::Load(Title,Run,0);
    for(u32 interval=0;interval<4;++interval) {
        owner->NextSwap(Title,Run,3,false,100,100);
        for(u32 packet=0;packet<2;++packet) {
            auto token=f.Stage(*owner,10+interval,true);
            Check(bool(token),"valid bounded packet lost under metadata maximum");
            token.Recorded();
        }
    }
    owner->Finish(100,100);
    const auto result=Read("eight-max-metadata");
    Check(result.manifest["packets"].size()==8,"maximum packet bank did not serialize");
    std::printf("Eight-packet maximum manifest: %zu bytes / %zu cap\n",
                result.manifest_bytes,MaxManifestBytes);

    Fixture huge;
    huge.regs.pipeline.num_vertices=4096;
    huge.regs.pipeline.vertex_attributes.attribute_loaders[0].byte_count.Assign(112);
    WriteConfig(huge.Configuration("aggregate-cap"));
    auto capped=Session::Load(Title,Run,0);
    std::vector<u8> raw(4096*112,0x17);
    unsigned captured{};
    for(u32 interval=0;interval<4;++interval) {
        capped->NextSwap(Title,Run,3,false,100,100);
        for(u32 packet=0;packet<2;++packet) {
            capped->BeginDraw(huge.regs,huge.setup,false);
            capped->PreparePayload(huge.regs,huge.setup,huge.defaults,huge.inputs,1,0,4095);
            capped->CopyVertex(0,0,112,112,4096,raw.data());
            capped->CopyFixed(huge.fixed);
            auto token=capped->Commit(huge.bound,huge.uniform,10+interval);
            if(token) { token.Recorded(); ++captured; }
            capped->EndDraw();
        }
    }
    capped->Finish(100,100);
    const auto saturated=Read("aggregate-cap");
    Check(captured>0 && captured<8,"aggregate cap failed to censor excess packet");
    Check(saturated.manifest["packets"].size()==captured,"aggregate prefix was lost");
    Check(saturated.manifest["summary"]["allocated_payload_bytes"].get<u64>()<=MaxPayloadBytes,
          "aggregate allocation exceeded cap");
    Check(saturated.manifest["summary"]["copied_bytes"].get<u64>()<=MaxCopiedBytes,
          "copy lifetime exceeded cap");
    Check(saturated.manifest["summary"]["capped"].get<u64>()>0,
          "aggregate censorship not reported");
}
} // CodexAstraLocal: independent production-session fixtures.

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("fixture directory argument required");
        FileUtil::root=argv[1];
        std::filesystem::create_directories(std::filesystem::path(FileUtil::root)/"config");
        TestConfiguration(); TestImmutableAndSubmission(); TestFailureAndLimits();
        TestDiscoveryCapacity(); TestPacketCapacity();
        std::printf("PASS: production vertex capture session (%u checks)\n",checks);
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
