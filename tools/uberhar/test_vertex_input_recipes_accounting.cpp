// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Exercise the actual integrated plan through public Prepare/
// Load APIs; source mutations must fail before any production promotion.
#include <cstdio>
#include <stdexcept>
#include <numeric>
#include <string>
#include "video_core/pica/uberhar_vertex_output.h"
#include "common/logging/log.h"
#include "video_core/pica/uberhar_vertex_input.h"
// CodexAstraLocal: Suppress ordinary production logs in this host fixture, but
// turn error-level messages into test failures so a hidden fallback cannot pass.
namespace Common::Log {
void Stop(){}
void FmtLogMessageImpl(Class,Level level,const char*,unsigned,const char*,fmt::string_view format,const fmt::format_args& args){
    if(level>=Level::Error)throw std::runtime_error(fmt::vformat(format,args));
}
}
using namespace Pica;
u32 checks{};
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
// CodexAstraLocal: Exact extracted production post-FIFO and report bodies use
// deterministic endpoint data; full FIFO/interpreter/JIT behavior has a separate gate.
struct Accounting {
    u64 native_input_fused_vertices{}, native_input_legacy_vertices{};
    std::array<u64,5> native_input_recipe_invocations{};
    static_assert(sizeof(native_input_recipe_invocations)==40);
    void Apply(const NativeVertexInputPlan& input_plan, NativeVertexCounts counts,
               u64 escaped_input_vertices) {
#include "account.inc"
    }
    std::string Report(const char* kind) {
        std::array<u64,5> native_input_results{};
        u64 native_input_maps{};
        std::string text;
#undef LOG_INFO_WITH_DELIVERY
#define LOG_INFO_WITH_DELIVERY(category, delivery, ...) text=fmt::format(__VA_ARGS__)
#include "report.inc"
        return text;
    }
    u64 Sum() const {return std::accumulate(native_input_recipe_invocations.begin(),native_input_recipe_invocations.end(),u64{});}
};
// CodexAstraLocal: Distinct fixed input formats select all four real tags; a
// default attribute reaches generic slot zero without inventing a test-only recipe.
std::array<u8,4096> shape_memory{};
void Shape(NativeVertexInputPlan& plan,u32 slot,bool allow=true,bool short_map=false) {
    using F=PipelineRegs::VertexAttributeFormat;
    const std::array<std::array<std::pair<F,u32>,11>,5> shapes{{
        {{{F::FLOAT,3},{F::BYTE,3},{F::SHORT,2},{F::SHORT,2},{F::UBYTE,4}}},
        {{{F::FLOAT,3},{F::BYTE,3},{F::SHORT,2},{F::SHORT,2},{F::UBYTE,4}}},
        {{{F::SHORT,3},{F::BYTE,4},{F::SHORT,2},{F::UBYTE,2},{F::SHORT,3},
          {F::SHORT,4},{F::SHORT,4},{F::SHORT,4},{F::SHORT,4},{F::SHORT,4},{F::SHORT,4}}},
        {{{F::FLOAT,3},{F::FLOAT,2}}},
        {{{F::FLOAT,3},{F::BYTE,3},{F::SHORT,2},{F::UBYTE,4}}}}};
    constexpr std::array<u32,5> counts{5,5,11,2,4};
    ShaderRegs regs{};regs.max_input_attribute_index.Assign(counts[slot]-1);
    const auto status=plan.Prepare(regs,counts[slot],0,3,[&](u32 i){
        return NativeInputAttribute{i*16,192,shapes[slot][i].second,shapes[slot][i].first,slot==0&&i==0};
    },[&](PAddr address)->std::span<const u8>{
        if(short_map)return{};
        return std::span<const u8>{shape_memory}.subspan(address);
    },allow);
    Check((status==NativeVertexInputPlan::Result::Ready)==!short_map,"shape prepare result");
    Check(plan.RecipeSlot()==(allow&&!short_map?slot:0),"slot tracks actual successful preparation");
}
void AccountingChecks() {
    NativeVertexInputPlan plan;Accounting counter;
    for(u32 slot=0;slot<5;++slot){Shape(plan,slot);counter.Apply(plan,{.invocations=100,.hits=80},7);
        Check(counter.native_input_recipe_invocations[slot]==93,"actual prepared invocations only");
        Check(counter.Sum()==counter.native_input_fused_vertices,"recipe sums equal fused vertices");}
    Check(counter.Sum()==465&&counter.native_input_legacy_vertices==35,"aggregate partition");
    const auto before=counter;
    counter.Apply(plan,{.invocations=0,.hits=0},0);
    Check(counter.Sum()==before.Sum(),"zero draw adds no recognition coverage");
    counter.Apply(plan,{.invocations=10,.hits=90},10);
    Check(counter.Sum()==before.Sum()&&counter.native_input_legacy_vertices==45,"all escapes are legacy only");
    Shape(plan,1,true,true);counter.Apply(plan,{.invocations=4,.hits=2},0);
    Check(counter.Sum()==465&&counter.native_input_legacy_vertices==49,"unready legacy is outside recipe denominator");
    Shape(plan,1);counter.Apply(plan,{.invocations=17,.hits=3},2);
    Check(counter.native_input_recipe_invocations[1]==108&&counter.Sum()==480&&counter.native_input_legacy_vertices==51,"retry final prepared transport");
    Shape(plan,3);Shape(plan,0);Shape(plan,4);Shape(plan,2,false);
    counter.Apply(plan,{.invocations=5,.hits=15},0);
    Check(counter.native_input_recipe_invocations[0]==98&&counter.Sum()==485,"disabled recipe counts generic transport");
    counter.native_input_recipe_invocations={101,211,307,401,503};counter.native_input_fused_vertices=1523;
    for(const char* kind:{"progress","totals"}){const auto report=counter.Report(kind);
        Check(report.find(std::string("Uberhar vertex input ")+kind+": schema=2 ")!=std::string::npos,"report schema/kind");
        Check(report.find("fused_vertices=1523 ")!=std::string::npos,"report fused consumer");
        for(u32 slot=0;slot<5;++slot)
            Check(report.find("recipe"+std::to_string(slot)+"_vertices="+std::to_string(counter.native_input_recipe_invocations[slot])+" ")!=std::string::npos,"all five report consumers");}
}

int main()try{
    using F=PipelineRegs::VertexAttributeFormat;
    ShaderRegs regs{};regs.max_input_attribute_index.Assign(4);regs.input_attribute_to_register_map_low=0x43210;
    std::array<NativeInputAttribute,5> desc{{{0,64,3,F::FLOAT,false},{16,64,3,F::BYTE,false},
        {32,64,2,F::SHORT,false},{48,64,2,F::SHORT,false},{56,64,4,F::UBYTE,false}}};
    std::array<u8,128> memory{};std::size_t length=memory.size();
    const std::array<float,3> f0{1,2,3},f1{4,5,6};std::memcpy(memory.data(),f0.data(),12);std::memcpy(memory.data()+64,f1.data(),12);
    for(u32 v=0;v<2;++v)for(u32 c=0;c<4;++c)memory[v*64+56+c]=10+c+v;
    AttributeBuffer defaults{};defaults[0]={f24::FromFloat32(9),f24::FromFloat32(8),f24::FromFloat32(7),f24::FromFloat32(6)};
    NativeVertexInputPlan plan;ShaderUnit unit;
    const auto prepare=[&](bool enabled){return plan.Prepare(regs,5,0,1,[&](u32 a){return desc[a];},
        [&](PAddr address)->std::span<const u8>{if(address>length)return{};return{memory.data()+address,length-address};},enabled);};
    const auto ready=[&](bool enabled){Check(prepare(enabled)==NativeVertexInputPlan::Result::Ready,"expected ready");Check(plan.CanLoad(1)&&!plan.CanLoad(2),"wrong bounds");};
    ready(true);Check(plan.RecipeSlot()==1,"initial recipe tag");plan.Load(unit,defaults,0);Check(unit.input[0].x.ToFloat32()==1&&unit.input[0].w.ToFloat32()==1,"conversion/W fill");
    plan.Load(unit,defaults,1);Check(unit.input[0].x.ToFloat32()==4,"live vertex offset");
    float updated=17;std::memcpy(memory.data()+64,&updated,4);plan.Load(unit,defaults,1);Check(unit.input[0].x.ToFloat32()==17,"stale payload");
    desc[0].is_default=true;ready(true);Check(plan.RecipeSlot()==0,"default invalidates tag");plan.Load(unit,defaults,1);Check(unit.input[0].x.ToFloat32()==9&&unit.input[0].w.ToFloat32()==6,"default shape falsely selected");
    defaults[0].x=f24::FromFloat32(23);plan.Load(unit,defaults,1);Check(unit.input[0].x.ToFloat32()==23,"stale default");
    desc[0].is_default=false;ready(true);desc[0].is_default=true;ready(false);Check(plan.RecipeSlot()==0,"disabled invalidates tag");
    plan.Load(unit,defaults,1);Check(unit.input[0].x.ToFloat32()==23,"disabled prepare retained stale recipe");
    desc[0].is_default=false;ready(true);length=0;Check(prepare(true)==NativeVertexInputPlan::Result::ShortMapping&&!plan.CanLoad(0),"failed map authorizes load");Check(plan.RecipeSlot()==0,"failed map clears tag");
    length=memory.size();desc[0].is_default=true;ready(false);plan.Load(unit,defaults,0);Check(unit.input[0].x.ToFloat32()==23,"failed-map stale recipe survived next preparation");
    desc[0].is_default=false;regs.input_attribute_to_register_map_low=0;ready(true);plan.Load(unit,defaults,0);
    Check(unit.input[0].x.ToFloat32()==10&&unit.input[0].w.ToFloat32()==13,"ascending alias writes");
    auto copy=plan;auto moved=std::move(copy);moved.Load(unit,defaults,1);Check(unit.input[0].x.ToFloat32()==11,"copy/move points to wrong ops");
    AccountingChecks();
    std::printf("PASS %u public-plan and exact accounting/report checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
