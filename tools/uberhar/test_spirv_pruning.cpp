// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Validate actual generated modules and execute actual extracted
// TEV instruction emitters through a finite scalar/vector arithmetic recorder.
// This is not a Vulkan/Adreno pixel or performance claim.
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include "common/logging/log.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/spv_fs_shader_gen.h"

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                      fmt::string_view message, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(message, args));
}
}
using namespace Pica::Shader;
using TevStageConfig = Pica::TexturingRegs::TevStageConfig;
// CodexAstraLocal: Extracted production serialization proves the toggle changes
// warm optional identities, not merely source text or a same-build version tag.
#include "actual_profile_words.inc"

namespace Numeric {
using Pica::TexturingRegs;
struct Id {
    std::array<float, 4> value{};
    u32 count{};
};
struct FragmentModule {
    FSConfig config;
    Pica::TevLiveness::Plan tev_liveness{};
    struct { u32 Get(u32 size) { return size; } } vec_ids;
    u32 f32_id = 1;
    Id rounded_primary_color{}, primary_fragment_color{}, secondary_fragment_color{};
    Id combiner_output{}, combiner_buffer{}, next_combiner_buffer{};
    Id color_results_1{}, color_results_2{}, color_results_3{};
    Id alpha_results_1{}, alpha_results_2{}, alpha_results_3{};
    std::array<Id, 4> sample_tex_unit_func{};
    std::array<Id, 6> constants{};
    u32 source_reads{};
    explicit FragmentModule(FSConfig c) : config{c} {}
    template<class... F> Id ConstF32(F... f) { return {{{static_cast<float>(f)...}}, sizeof...(f)}; }
    Id ConstS32(s32 i) { return ConstF32(i); }
    template<class F> Id Binary(u32 count, Id a, Id b, F operation) {
        if (a.count != count || b.count != count) throw std::runtime_error("invalid emitted operand width");
        Id result{{}, count};
        for (u32 i = 0; i < count; ++i) result.value[i] = operation(a.value[i], b.value[i]);
        return result;
    }
    Id OpFAdd(u32 n, Id a, Id b) { return Binary(n,a,b,[](float x,float y){return x+y;}); }
    Id OpFSub(u32 n, Id a, Id b) { return Binary(n,a,b,[](float x,float y){return x-y;}); }
    Id OpFMul(u32 n, Id a, Id b) { return Binary(n,a,b,[](float x,float y){return x*y;}); }
    Id OpFMin(u32 n, Id a, Id b) { return Binary(n,a,b,[](float x,float y){return std::min(x,y);}); }
    Id OpFClamp(u32 n, Id a, Id low, Id high) {
        return Binary(n, Binary(n,a,low,[](float x,float y){return std::max(x,y);}), high,
                      [](float x,float y){return std::min(x,y);});
    }
    Id OpVectorTimesScalar(u32 n, Id a, Id scalar) {
        if (scalar.count != 1) throw std::runtime_error("invalid scalar operand");
        Id b{{},n}; b.value.fill(scalar.value[0]); return OpFMul(n,a,b);
    }
    Id OpRound(u32 n, Id a) {
        if (a.count != n) throw std::runtime_error("invalid rounded width");
        for (u32 i=0;i<n;++i) a.value[i]=std::round(a.value[i]); return a;
    }
    Id OpFMix(u32 n, Id a, Id b, Id c) {
        Id one{{},n};one.value.fill(1.f);
        return OpFAdd(n,OpFMul(n,a,OpFSub(n,one,c)),OpFMul(n,b,c));
    }
    Id OpFma(u32 n, Id a, Id b, Id c) {
        if (a.count!=n||b.count!=n||c.count!=n) throw std::runtime_error("invalid FMA width");
        Id r{{},n};for(u32 i=0;i<n;++i)r.value[i]=std::fma(a.value[i],b.value[i],c.value[i]);return r;
    }
    Id OpDot(u32 n, Id a, Id b) {
        if(n!=1||a.count!=3||b.count!=3)throw std::runtime_error("invalid dot operands");
        return ConstF32(a.value[0]*b.value[0]+a.value[1]*b.value[1]+a.value[2]*b.value[2]);
    }
    template<class... I> Id OpVectorShuffle(u32 n, Id a, Id b, I... indices) {
        const std::array<u32,sizeof...(I)> selected{static_cast<u32>(indices)...};
        if(n!=selected.size())throw std::runtime_error("invalid shuffle width");
        Id r{{},n};for(u32 i=0;i<n;++i){auto index=selected[i];
            if(index>=a.count+b.count)throw std::runtime_error("invalid shuffle index");
            r.value[i]=index<a.count?a.value[index]:b.value[index-a.count];}return r;
    }
    Id OpCompositeExtract(u32 n, Id a, u32 index) {
        if(n!=1||index>=a.count)throw std::runtime_error("invalid extraction");return ConstF32(a.value[index]);
    }
    template<class... V> Id OpCompositeConstruct(u32 n, V... values) {
        Id r{{},n};u32 index=0;
        for(auto v:std::array<Id,sizeof...(V)>{values...})for(u32 i=0;i<v.count;++i){
            if(index>=n)throw std::runtime_error("invalid construct overflow");r.value[index++]=v.value[i];}
        if(index!=n)throw std::runtime_error("invalid construct width");return r;
    }
    Id GetShaderDataMember(u32, Id member, Id index) {
        if(member.value[0]!=25)throw std::runtime_error("unexpected TEV uniform");
        ++source_reads;return constants.at(static_cast<u32>(index.value[0]));
    }
    Id OpFunctionCall(u32, Id sampled) { ++source_reads;return sampled; }
    Id GetSource(TevStageConfig::Source, s32);
    Id AppendColorModifier(TevStageConfig::ColorModifier,TevStageConfig::Source,s32);
    Id AppendAlphaModifier(TevStageConfig::AlphaModifier,TevStageConfig::Source,s32);
    Id AppendColorCombiner(TevStageConfig::Operation);
    Id AppendAlphaCombiner(TevStageConfig::Operation);
    Id Byteround(Id,u32=1);
    void WriteTevStage(s32);
    void WritePrunedTevStage(s32);
};
#include "actual_tev_methods.inc"
}

FSConfig MakeConfig() {
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    regs.framebuffer.output_merger.alpha_test.func.Assign(Pica::FramebufferRegs::CompareFunc::Always);
    FSConfig cfg{regs};
    for(auto& stage:cfg.texture.tev_stages)stage={0x000f000f,0,0,0};
    return cfg;
}
void SemanticChecks() {
    using Stage=TevStageConfig;
    std::mt19937 random{0x0585F00D};
    constexpr std::array<u32,10> source{0,1,2,3,4,5,6,13,14,15};
    constexpr std::array<u32,10> modifier{0,1,2,3,4,5,8,9,12,13};
    constexpr std::array<u32,10> color_op{0,1,2,3,4,5,6,7,8,9};
    constexpr std::array<u32,8> alpha_op{0,1,2,3,4,5,8,9};
    u64 old_reads=0,new_reads=0;
    for(u32 test=0;test<384;++test) {
        auto cfg=MakeConfig();
        cfg.texture.combiner_buffer_input.Assign(test<256?test:random()&255U);
        for(auto& stage:cfg.texture.tev_stages){
            stage={};
            for(u32 i=0;i<3;++i){stage.sources_raw|=source[random()%10]<<(4*i);
                stage.sources_raw|=source[random()%10]<<(16+4*i);
                stage.modifiers_raw|=modifier[random()%10]<<(4*i);
                stage.modifiers_raw|=(random()%8)<<(12+4*i);}
            stage.ops_raw=color_op[random()%10]|(alpha_op[random()%8]<<16);
            stage.scales_raw=(random()%4)|((random()%4)<<16);
        }
        if(test<64)cfg.texture.tev_stages[0]={0x000f000f,0,0,test&1?0x00030003U:0U};
        if(test>=256&&test<320)cfg.texture.tev_stages[0]={0x000e000e,0,0,0};
        if(test>=320){
            for(auto& stage:cfg.texture.tev_stages)stage={0x000f000f,0,0,0};
            cfg.texture.tev_stages[0]={0x000e000e,0,7,0x00020002}; // Dot3 alpha with RGB scale four
            cfg.texture.tev_stages[5]={0x000f000e,0,0,0}; // RGB replaced, alpha survives
        }
        Numeric::FragmentModule full{cfg},pruned{cfg};
        auto vector=[&](){return Numeric::Id{{float(random()%256)/255,float(random()%256)/255,
            float(random()%256)/255,float(random()%256)/255},4};};
        full.rounded_primary_color=vector();full.primary_fragment_color=vector();full.secondary_fragment_color=vector();
        full.combiner_output=full.combiner_buffer=full.ConstF32(0,0,0,0);full.next_combiner_buffer=vector();
        for(auto& x:full.constants)x=vector();for(auto& x:full.sample_tex_unit_func)x=vector();pruned=full;
        std::array<Stage,6> stages;for(u32 i=0;i<6;++i)stages[i]=cfg.texture.tev_stages[i];
        if(Numeric::IsPassThroughTevStage(stages[0])){
            stages[0].color_source3.Assign(Stage::Source::Previous);
            stages[0].alpha_source3.Assign(Stage::Source::Previous);}
        pruned.tev_liveness=Pica::TevLiveness::Analyze(stages,cfg.texture.combiner_buffer_input&15U,
            cfg.texture.combiner_buffer_input>>4,Pica::TevLiveness::Stage0Policy::GeneratedGlsl);
        for(s32 i=0;i<6;++i){full.WriteTevStage(i);pruned.WritePrunedTevStage(i);}
        if(std::memcmp(full.combiner_output.value.data(),pruned.combiner_output.value.data(),16))
            throw std::runtime_error("actual SPIR-V TEV instruction semantics differ at case "+std::to_string(test));
        old_reads+=full.source_reads;new_reads+=pruned.source_reads;
    }
    if(!(new_reads<old_reads))throw std::runtime_error("dead-work controls had no coverage");
    std::cout<<"PASS 384 actual TEV emitter semantics, all256 buffer masks, stage0/passthrough/Dot3-alpha; reads "
             <<old_reads<<" -> "<<new_reads<<'\n';
}

int main(int argc,char**argv)try{
    if(argc!=2)return 2;
    SemanticChecks();
    Profile off{};off.is_vulkan=true;off.has_logic_op=true;off.has_custom_border_color=true;
    Profile on=off;on.vk_prune_tev=true;
    if(off==on||OptionalProfileWords(off)==OptionalProfileWords(on))throw std::runtime_error("warm identity ignores toggle");
    std::filesystem::create_directories(argv[1]);
    for(u32 test=0;test<12;++test){
        auto config=MakeConfig();
        config.texture.tev_stages[0]={0x000e000e,0,0,0};
        if(test==1){config.lighting.enable.Assign(1);config.lighting.src_num.Assign(1);}
        if(test==2){config.lighting.enable.Assign(1);config.lighting.src_num.Assign(1);
            config.texture.tev_stages[5]={0x00010001,0,0,0};}
        if(test==3)config.texture.tev_stages[0]={0x000f000f,0,0,0};
        if(test==4)config.texture.tev_stages[0]={0x00e000ef,0,0,0};
        if(test>=5&&test<9){config.texture.combiner_buffer_input.Assign((test-4)*0x11);
            config.texture.tev_stages[2]={0x000d000d,0,0,0};}
        if(test==9){config.texture.tev_stages[0]={0x00e000e0,0,7,0x00020002};
            config.texture.tev_stages[5]={0x000f000e,0,0,0};}
        if(test==10){config.texture.texture0_type.Assign(Pica::TexturingRegs::TextureConfig::TextureType::Shadow2D);
            config.texture.tev_stages[0]={0x00030003,0,0,0};config.texture.tev_stages[5]={0x000e000e,0,0,0};}
        if(test==11){config.texture.texture0_type.Assign(Pica::TexturingRegs::TextureConfig::TextureType::Texture2D);
            config.texture.tev_stages[0]={0x00030003,0,0,0};}
        const auto original=config;
        const auto a=Generator::SPIRV::GenerateFragmentShader(config,off);
        const auto b=Generator::SPIRV::GenerateFragmentShader(config,on);
        if(!(config==original))throw std::runtime_error("generator changed source config");
        if(test==1&&!(b.size()<a.size()))throw std::runtime_error("unused lighting not removed");
        for(u32 flag=0;flag<2;++flag){const auto& words=flag?b:a;
            std::ofstream file(std::filesystem::path(argv[1])/(std::to_string(test)+(flag?"-on.spv":"-off.spv")),std::ios::binary);
            file.write(reinterpret_cast<const char*>(words.data()),words.size()*sizeof(u32));}
        std::cout<<"case="<<test<<" off_words="<<a.size()<<" on_words="<<b.size()<<'\n';
    }
    std::cout<<"PASS 24 production direct SPIR-V modules emitted; exact profile/cache toggle differs\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
