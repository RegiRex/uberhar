// CodexAstraLocal: Check exact cached preparation and scanline coverage against
// independent scalar edges; these are correctness counts, not performance data.
#include <array>
#include <bit>
#include <cstring>
#include "video_core/pica/uberhar_vertex_input.h" // CodexAstraLocal: Actual scalar-read provenance.
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include "video_core/renderer_software/sw_raster_work.h"
#include "video_core/renderer_software/sw_tev_preparation.h"
#include "video_core/renderer_software/sw_source_provenance.h" // CodexAstraLocal: Exact bounded source evidence.
using namespace SwRenderer;
static u64 checks{};
static void Need(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
static std::vector<u32> Signature(const PreparedTevConfig& p) {
    std::vector<u32> r{p.demand.textures,p.demand.primary_color,p.demand.lighting,p.constant_result};
    for (u32 i=0;i<4;++i) r.push_back(p.initial_buffer[i]);
    for (const auto& s:p.stages) {
        for (auto v:s.color_sources) r.push_back(static_cast<u32>(v));
        for (auto v:s.alpha_sources) r.push_back(static_cast<u32>(v));
        for (auto v:s.color_modifiers) r.push_back(static_cast<u32>(v));
        for (auto v:s.alpha_modifiers) r.push_back(static_cast<u32>(v));
        for (u32 i=0;i<4;++i) r.push_back(s.constant[i]);
        r.insert(r.end(),{static_cast<u32>(s.color_op),static_cast<u32>(s.alpha_op),s.color_count,
            s.alpha_count,s.color_multiplier,s.alpha_multiplier,s.update_color,s.update_alpha});
        r.push_back(s.live.rgb); r.push_back(s.live.alpha);
    }
    return r;
}
int main() try {
    Pica::TexturingRegs texturing{}; Pica::LightingRegs lighting{};
    PreparedTevCache cache;
    auto compare=[&] { Need(Signature(cache.Get(texturing,lighting)) ==
        Signature(PreparedTevConfig{texturing,lighting}), "cached preparation differs"); };
    compare();const auto first=cache.Generation();compare();
    Need(cache.Generation()==first && cache.Hits()==1,"unchanged preparation rebuilt");
    for(auto* stage:{&texturing.tev_stage0,&texturing.tev_stage1,&texturing.tev_stage2,
                     &texturing.tev_stage3,&texturing.tev_stage4,&texturing.tev_stage5}) {
        for(auto* word:{&stage->sources_raw,&stage->modifiers_raw,&stage->ops_raw,
                        &stage->const_color,&stage->scales_raw}) {
            const auto before=cache.Generation(); *word ^= 1;compare();
            Need(cache.Generation()==before+1,"TEV word mutation reused stale plan");
        }
    }
    auto mutate=[&](auto action) {const auto before=cache.Generation();action();compare();
        Need(cache.Generation()==before+1,"demand or buffer mutation reused stale plan");};
    mutate([&]{texturing.tev_combiner_buffer_input.update_mask_rgb.Assign(1);});
    mutate([&]{texturing.tev_combiner_buffer_input.update_mask_a.Assign(1);});
    mutate([&]{texturing.tev_combiner_buffer_color.raw=0x87654321;});
    mutate([&]{lighting.disable.Assign(1);});
    mutate([&]{lighting.config0.enable_shadow.Assign(1);});
    mutate([&]{lighting.config0.shadow_selector.Assign(2);});
    mutate([&]{lighting.config0.bump_mode.Assign(Pica::LightingRegs::LightingBumpMode::NormalMap);});
    mutate([&]{lighting.config0.bump_selector.Assign(1);});
    const auto builds=cache.Generation();texturing.texture0.address.Assign(0x12345);compare();
    Need(cache.Generation()==builds,"guest mapping state entered immutable plan");

    // CodexAstraLocal: A/B policy cannot retain a stale key when the cache
    // helper is reused outside the renderer's immutable lifetime policy.
    texturing.tev_stage5.const_color ^= 0xff00;
    Need(Signature(cache.Get(texturing,lighting,false)) == Signature(PreparedTevConfig{texturing,lighting}),"disabled cache output");
    texturing.tev_stage5.const_color ^= 0xff00;compare();
    const auto before_off=cache.Generation();cache.Get(texturing,lighting,false,false);
    Need(cache.Generation()==before_off,"disabled diagnostic counted build");

    // CodexAstraLocal: Traced readers consume exactly the ordinary typed
    // source once, retain its bytes after RAM changes, and preserve register
    // aliases/default provenance. No post-fault memory reread is an oracle.
    for(bool alias:{false,true})for(u32 format=0;format<4;++format)for(u32 elements=1;elements<=4;++elements) {
        using F=Pica::PipelineRegs::VertexAttributeFormat;
        std::array<u8,64> bytes;for(u32 i=0;i<bytes.size();++i)bytes[i]=u8(i*31+7);
        const auto original=bytes;Pica::ShaderRegs shader{};shader.max_input_attribute_index.Assign(1);
        shader.input_attribute_to_register_map_low=alias?0:0x10;
        std::array<Pica::NativeInputAttribute,2> descriptions{{{0,16,elements,static_cast<F>(format),false},
                                                            {0,0,4,F::FLOAT,true}}};
        Pica::NativeVertexInputPlan plan;
        Need(plan.Prepare(shader,2,0x123400,1,[&](u32 i){return descriptions[i];},
            [&](PAddr){return std::span<const u8>{bytes};},true)==Pica::NativeVertexInputPlan::Result::Ready,"traced plan refused");
        Pica::ShaderUnit ordinary,observed;Pica::AttributeBuffer defaults{};
        defaults[1][0]=Pica::f24::FromFloat32(13.0f);
        plan.Load(ordinary,defaults,1);Pica::NativeInputReadTrace trace{};
        plan.LoadObserved(observed,defaults,1,0x123400,descriptions,trace);
        Need(std::memcmp(ordinary.input.data(),observed.input.data(),sizeof(ordinary.input))==0,"trace changed converted inputs");
        const u32 size=elements*Pica::PipelineRegs::GetFormatBytes(static_cast<F>(format));
        bytes.fill(0);
        Need(trace.count==2 && trace.guest_bytes==size,"trace counts");
        Need(trace.attributes[0].address==0x123410 && trace.attributes[0].format==format &&
             trace.attributes[0].size==size && trace.attributes[0].elements==elements,"trace source identity");
        Need(std::memcmp(trace.attributes[0].bytes.data(),original.data()+16,size)==0,"read-time bytes lost");
        Need(trace.attributes[1].is_default && trace.attributes[1].address==0 && trace.attributes[1].size==16,
             "default claimed guest RAM provenance");
        Need(std::memcmp(trace.attributes[1].bytes.data(),&defaults[1],16)==0,"default register bytes lost");
    }

    // CodexAstraLocal: Exercise both safe ranges and extreme legacy overflow
    // refusal, each orientation, horizontal edges and the two top-left biases.
    std::mt19937 random{0x49};u64 empty_saved{}, overflow_rows{};
    for(unsigned test=0;test<12000;++test) {
        RasterPoints p{};const u32 range=test%4==0?65536:4096;
        for(auto& point:p)for(auto& v:point)v=random()%range;
        std::array<s32,3> bias{};for(auto& v:bias)v=-(random()%2);
        const u32 x=(random()%64)*16+8,y=(random()%64)*16+8,n=1+random()%64;
        const auto previous_overflow=overflow_rows;
        const auto span=CoveredRow(p,bias,x,y,n,&overflow_rows);
        bool safe=true;std::vector<bool> expected(n,true);
        for(u32 i=0;i<n;++i) for(u32 edge=0;edge<3;++edge) {
            const auto a=p[(edge+1)%3],b=p[(edge+2)%3];
            const s64 left=(s64(b[0])-a[0])*(s64(y)-a[1]);
            const s64 right=(s64(b[1])-a[1])*(s64(x)+i*16-a[0]);
            const s64 weight=left-right,value=weight+bias[edge];
            for(auto v:{left,right,weight,value}) safe &= v>=INT32_MIN && v<=INT32_MAX;
            expected[i]=expected[i]&&value>=0;
        }
        Need(span.first<=span.end&&span.end<=n,"span bounds");
        for(u32 i=0;i<n;++i) {
            const bool visited=i>=span.first&&i<span.end;
            Need(!expected[i]||visited,"covered pixel omitted");
            if(safe) Need(visited==expected[i],"safe span not exact");
        }
        Need(overflow_rows-previous_overflow==!safe,"overflow refusal count differs");
        if(!safe)Need(span.first==0&&span.end==n,"overflow changed legacy traversal");
        // CodexAstraLocal: Exact edge stepping must match independent wide
        // products at every admitted pixel, including top-left boundary bias.
        Need(!span.exact_edges || safe, "unsafe row certified for stepping");
        if (span.exact_edges) {
            RasterEdgeSteps steps{p, x + span.first * 16, y};
            for (u32 pixel = span.first; pixel < span.end; ++pixel, steps.Advance()) {
                for (u32 edge = 0; edge < 3; ++edge) {
                    const auto a=p[(edge+1)%3],b=p[(edge+2)%3];
                    const s64 expected_weight = (s64(b[0])-a[0])*(s64(y)-a[1]) -
                        (s64(b[1])-a[1])*(s64(x)+pixel*16-a[0]);
                    Need(steps.value[edge] == expected_weight, "incremented edge differs");
                    Need(steps.value[edge] >= INT32_MIN && steps.value[edge] <= INT32_MAX,
                         "certified edge leaves original range");
                }
            }
        }
        empty_saved+=n-(span.end-span.first);
    }
    Need(empty_saved>0,"empty coverage not pruned");
    Need(overflow_rows>0,"overflow diagnostic never exercised");
    for(u32 rows:{1U,4U,8U,32U,64U,256U})for(u32 candidates:{0U,1U,511U,1023U,1024U,4096U,65536U}) {
        const auto work=PlanRasterRows(rows,candidates);
        Need(work.grain>=4&&work.grain<=32,"row grain bound");
        Need(!work.parallel||(candidates>=1024&&rows/work.grain>=2),"underqualified dispatch");
    }
    Need(PlanRasterRows(64,4096).parallel,"qualified row work never dispatches");
    // CodexAstraLocal: NaN payloads and full position identity must survive
    // trace ownership, duplicate ambiguity and bounded-history eviction.
    SoftwareSourceHistory history;
    Pica::OutputVertex source{};
    source.pos.w = Pica::f24::One();
    source.tc0.y = std::bit_cast<Pica::f24>(0xfffffffeU);
    Pica::NativeInputReadTrace read{};
    read.count = 1; read.guest_bytes = 8;
    read.attributes[0].address = 0x20f0f85c;
    read.attributes[0].size = 8; read.attributes[0].elements = 2;
    read.attributes[0].format = 3;
    const std::array<u8,8> raw{0x30,0,0,0,0xfe,0xff,0xff,0xff};
    std::copy(raw.begin(), raw.end(), read.attributes[0].bytes.begin());
    history.Add(source, &read, 7, 1, 9);
    read.attributes[0].bytes.fill(0);
    auto match = history.Find(source);
    Need(match.count == 1 && match.latest->event == 1 && match.latest->actual_reads,
         "source values did not match their actual read");
    Need(std::equal(raw.begin(), raw.end(), match.latest->input.attributes[0].bytes.begin()),
         "source read retained mutable caller storage");
    auto different = source;
    different.pos.x = Pica::f24::One();
    Need(history.Find(different).count == 0, "matching UVs substituted different position");
    different = source;
    different.tc0.y = std::bit_cast<Pica::f24>(0x7fc00000U);
    Need(history.Find(different).count == 0, "NaN payload difference ignored");
    history.Add(source, nullptr, 8, 2, 10);
    match = history.Find(source);
    Need(match.count == 2 && match.latest->event == 2 && !match.latest->actual_reads,
         "duplicate output claimed unique actual-read identity");
    for (u32 i = 0; i < SoftwareSourceHistory::Capacity; ++i) {
        different.pos.x = Pica::f24::FromFloat32(float(i + 1));
        history.Add(different, nullptr, 9, i, i);
    }
    Need(history.Find(source).count == 0 && history.Events() == 18, "history eviction failed");
    std::cout<<"software work PASS checks="<<checks<<" skipped_candidates="<<empty_saved<<'\n';
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
