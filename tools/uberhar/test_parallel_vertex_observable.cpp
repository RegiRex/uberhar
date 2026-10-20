// CodexAstraLocal: Actual-A64 selected-output proof for the distinct
// observable contract. The same unchanged JIT artifact executes serially and
// through the real pool. Unused state/FPSR differences are reported, not hidden
// or interpreted as guest compatibility before the separate consumer proof.
#include <array>
#include <barrier>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <json.hpp>
#include "common/uberhar_parallel_work.h"
#include "video_core/shader/shader_jit_a64_compiler.h"
#include "test_parallel_vertex_observable_cases.h"
#include "video_core/pica/uberhar_parallel_vertex.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/pica/uberhar_vertex_parallel_batch.h"

using Json = nlohmann::json;
using namespace Pica;
using Jit = Shader::JitShader;
using Env = Common::Uberhar::ParallelFloatEnvironment;
u64 checks{}, calls{}, batches{}, worker_items{}, status_different{}, state_different{};
u64 composed_draws{}, composed_invocations{}, composed_worker_invocations{}, composed_hits{};
std::string current_case;
// CodexAstraLocal: Select the caller-qualified contract explicitly; the original
// certificate remains the API default for all unqualified callers.
ParallelVertexCertificate AnalyzeObservableParallelVertex(const ProgramCode& code,
        const SwizzleData& swizzles, u32 entry, u16 bools, u32 mask) {
    return AnalyzeParallelVertex(code, swizzles, entry, bools, mask,
                                 ParallelVertexContract::SelectedOutputValues);
}

// CodexAstraLocal: Export only the finite synthetic cases compiled into this
// fixture. Python uses the exact same data for positive and sensitive controls.
Json SyntheticCases() {
    auto all = ObservableChallenge::BuildCases();
    for (auto value : ObservableSensitive::BuildCases()) all.push_back(std::move(value));
    Json cases = Json::array();
    for (const auto& value : all) {
        cases.push_back({{"name", value.name}, {"program", value.code},
            {"swizzle", value.swizzles}, {"entry", value.entry}, {"bools", value.bools},
            {"output_mask", value.output_mask}, {"safe_to_execute", value.safe_to_execute},
            {"expect_accept", value.expect == CarryChallenge::Expect::Accept}});
    }
    return cases;
}
void Check(bool condition, const char* why) {
    ++checks;
    if (!condition) throw std::runtime_error(why);
}
void Set(f24& x, u32 bits) { std::memcpy(&x,&bits,4); }
bool SameSelected(const ShaderUnit& left,const ShaderUnit& right,u32 mask) {
    if (left.output_bank!=right.output_bank) return false;
    for(u32 reg=0;reg<16;++reg) if(mask&(1U<<reg))
        if(std::memcmp(&left.output[left.output_bank][reg],&right.output[right.output_bank][reg],16))
            return false;
    return true;
}

// CodexAstraLocal: Compose actual live input conversion, A64 execution, the
// inherited FIFO, chunked worker runner, output conversion and persistent
// assembler. Only the pinned memory provider is a bounded synthetic array.
void Compose() {
    using namespace CarryChallenge;
    Builder builder("composed_status_only_carry");
    builder.Arithmetic(Op::MOV,Dst::MakeTemporary(0),7,Src::MakeInput(0));
    builder.Arithmetic(Op::MUL,Dst::MakeTemporary(0),8,Src::MakeTemporary(0),Src::MakeInput(1));
    builder.Arithmetic(Op::DP3,Dst::MakeOutput(0),15,Src::MakeTemporary(0),Src::MakeInput(2));
    builder.Arithmetic(Op::MOV,Dst::MakeOutput(1),15,Src::MakeInput(0));
    const auto fixture=builder.Finish();
    Check(AnalyzeObservableParallelVertex(fixture.code,fixture.swizzles,0,0,3).Supported(),"composed observable admission");
    Check(!AnalyzeParallelVertex(fixture.code,fixture.swizzles,0,0,3).Supported(),"old contract still refuses carry");
    Jit jit;jit.Compile(&fixture.code,&fixture.swizzles);ShaderSetup setup;
    std::memset(&setup.uniforms,0,sizeof(setup.uniforms));
    ShaderRegs shader{};shader.max_input_attribute_index.Assign(2);
    shader.input_attribute_to_register_map_low=0x210;shader.output_mask.Assign(3);
    RasterizerRegs rasterizer{};rasterizer.vs_output_total.Assign(2);
    for(u32 attribute=0;attribute<2;++attribute) {
        auto& map=rasterizer.vs_output_attributes[attribute];
        const u32 first=attribute==0?0:8;
        using Semantic=RasterizerRegs::VSOutputAttributes::Semantic;
        map.map_x.Assign(static_cast<Semantic>(first));map.map_y.Assign(static_cast<Semantic>(first+1));
        map.map_z.Assign(static_cast<Semantic>(first+2));map.map_w.Assign(static_cast<Semantic>(first+3));
    }
    NativeVertexPlan output{shader,rasterizer};Check(output.Supported(),"composed output plan");
    constexpr u32 VertexCount=521;
    std::vector<u32> storage(VertexCount*12);AttributeBuffer defaults{};
    NativeVertexInputPlan input;
    Check(input.Prepare(shader,3,0,VertexCount-1,[](u32 attribute){
        return NativeInputAttribute{attribute*16,48,4,PipelineRegs::VertexAttributeFormat::FLOAT,false};
    },[&](PAddr address){const auto bytes=std::as_bytes(std::span{storage});
        return std::span<const u8>{reinterpret_cast<const u8*>(bytes.data())+address,bytes.size()-address};
    },true)==NativeVertexInputPlan::Result::Ready,"composed input plan");
    using Triangle=std::array<OutputVertex,3>;
    const auto same=[](const auto& a,const auto& b){return a.size()==b.size()&&(a.empty()||
        std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0);};
    for(u32 cores:{1U,2U,4U}) {
        NativeParallelBatch batch{cores};
        Check(!batch.Prepare(0),"empty scratch request rejected");
        for(auto topology:{PipelineRegs::TriangleTopology::List,PipelineRegs::TriangleTopology::Strip,
                           PipelineRegs::TriangleTopology::Fan,PipelineRegs::TriangleTopology::Shader}) {
            PrimitiveAssembler serial_assembler{topology},parallel_assembler{topology};
            for(bool indexed:{false,true}) for(u32 count:{1U,255U,256U,4095U,4096U,4097U,8193U}) {
                ++composed_draws;
                for(u32 n=0;n<storage.size();++n) storage[n]=0x3e800000U+((n*13+composed_draws)%64)*0x00040000U;
                const auto vertex_at=[&](u32 i){return indexed?(i%193<96?i%37:(i*71)%VertexCount):i%VertexCount;};
                ShaderUnit initial;CarryChallenge::Seed(initial,composed_draws%16,composed_draws&1);
                ShaderUnit serial_unit=initial;NativeVertexSamples samples;
                std::vector<OutputVertex> serial,parallel;std::vector<Triangle> serial_triangles,parallel_triangles;
                if(topology==PipelineRegs::TriangleTopology::Shader&&composed_draws%3==0) {
                    serial_assembler.SetWinding();parallel_assembler.SetWinding();
                }
                const auto context=jit.BindForDraw(setup,0);
                const auto expected=RunNativeVertexBatch<false>(count,indexed,vertex_at,[&]<bool>(u32 vertex,u32){
                    input.Load(serial_unit,defaults,vertex);context.Run(serial_unit);return output.Convert(serial_unit);
                },[&](const OutputVertex& v){serial.push_back(v);serial_assembler.SubmitVertex(v,[&](auto a,auto b,auto c){serial_triangles.push_back({a,b,c});});},samples);
                Check(batch.Prepare(count),"composed scratch preparation");
                const auto actual=batch.Run(count,indexed,vertex_at,[&](auto invocations,auto results){
                    ShaderUnit unit=initial;
                    for(u32 n=0;n<invocations.size();++n) {
                        input.Load(unit,defaults,invocations[n].vertex);context.Run(unit);results[n]=output.Convert(unit);
                    }
#if defined(COMPOSE_WRONG_ORDER)
                    // CodexAstraLocal: Intentional ordered-payload defect, never production code.
                    if(results.size()>1) std::swap(results[0],results[1]);
#endif
                },[&](const OutputVertex& v){parallel.push_back(v);parallel_assembler.SubmitVertex(v,[&](auto a,auto b,auto c){parallel_triangles.push_back({a,b,c});});});
                Check(same(serial,parallel),"composed output bytes");Check(same(serial_triangles,parallel_triangles),"composed ordered triangle bytes");
                Check(expected.invocations==actual.invocations&&expected.hits==actual.hits,"composed FIFO counts");
                Check(batch.LastWork().owner_invocations+batch.LastWork().worker_invocations==actual.invocations,"composed work counts");
                Check(serial_assembler.IsEmpty()==parallel_assembler.IsEmpty()&&
                      serial_assembler.HasPendingWinding()==parallel_assembler.HasPendingWinding(),"composed assembler tail");
                composed_invocations+=actual.invocations;composed_hits+=actual.hits;
                composed_worker_invocations+=batch.LastWork().worker_invocations;
            }
        }
    }
    Check(composed_worker_invocations>0,"composed actual worker participation");
}

int main(int argc,char** argv) try {
    Check(argc==2||argc==3,"arguments");
    if (argc == 2 && std::string_view{argv[1]} == "--export") {
        std::cout << SyntheticCases().dump() << '\n';
        return 0;
    }
    const bool mutant=argc==3;
    std::ifstream input(argv[1]);Json cases;input>>cases;
    const auto original=Env::Capture();
    Common::Uberhar::ParallelWork pool(4);
    unsigned accepted{},rejected{};
    for(const auto& item:cases) {
        current_case=item.at("name").get<std::string>();
        const auto code=item.at("program").get<ProgramCode>();
        const auto swizzle=item.at("swizzle").get<SwizzleData>();
        const u32 entry=item.at("entry"),mask=item.at("output_mask");
        const u16 bools=item.at("bools");
        const auto certificate=AnalyzeObservableParallelVertex(code,swizzle,entry,bools,mask);
        if (!mutant) {
            // CodexAstraLocal: Explicit full-arithmetic-read selection must remain exactly
            // the implicit default; invalid contract values always refuse.
            const auto normal = AnalyzeParallelVertex(code, swizzle, entry, bools, mask);
            const auto full = AnalyzeParallelVertex(code, swizzle, entry, bools, mask,
                                                   ParallelVertexContract::FullArithmeticReads);
            Check(normal.status == full.status && normal.pc == full.pc && normal.reg == full.reg &&
                  normal.lanes == full.lanes && normal.nodes == full.nodes, "default contract changed");
            Check(AnalyzeParallelVertex(code, swizzle, entry, bools, mask,
                    static_cast<ParallelVertexContract>(255)).status == ParallelVertexStatus::InvalidContract,
                  "invalid contract admitted");
        }
        if(!mutant) Check(certificate.Supported()==item.at("expect_accept").get<bool>(),"admission expectation");
        else Check(certificate.Supported(),"mutant failed to admit target");
        if(!certificate.Supported()) {
            ++rejected;
            std::cout<<Json{{"case",current_case},{"accepted",false},{"status",u32(certificate.status)}}.dump()<<'\n';
            continue;
        }
        ++accepted;
        Check(item.at("safe_to_execute").get<bool>(),"unsafe fixture must remain rejected");
        Jit shader;shader.Compile(&code,&swizzle);
        ShaderSetup setup;std::memset(&setup.uniforms,0,sizeof(setup.uniforms));
        for(unsigned reg=0;reg<96;++reg) for(unsigned lane=0;lane<4;++lane)
            Set(setup.uniforms.f[reg][lane],0x3e800000U+((reg*3+lane)%16)*0x00080000U);
        for(unsigned n=0;n<16;++n) setup.uniforms.b[n]=bool(bools&(1U<<n));
        for(unsigned fp=0;fp<16;++fp) for(unsigned seed=0;seed<16;++seed)
            for(unsigned grain: {1U,7U}) {
            const u64 control=u64(fp&3)<<22 | (fp&4?1ULL<<24:0) | (fp&8?1ULL<<25:0);
            const u64 sticky=seed&1?1ULL<<27:0;
            const Env environment{control,sticky};
            Check((control&0x9f00U)==0,"fixture traps must be disabled");
            ShaderUnit initial;CarryChallenge::Seed(initial,seed,seed&1);
            ShaderUnit serial=initial;
            constexpr unsigned Count=32;
            std::array<ShaderUnit,Count> expected,actual;
            environment.Apply();
            for(unsigned n=0;n<Count;++n) {
                CarryChallenge::SetInput(serial,n+seed,n>=16);
                shader.Run(setup,serial,entry);++calls;expected[n]=serial;
            }
            const auto serial_env=Env::Capture();
            environment.Apply();
            std::barrier started(4);
            const auto context=shader.BindForDraw(setup,entry);
            const auto work=pool.Run(Count,grain,[&](u32 first,u32 last) {
                if(first<4*grain) started.arrive_and_wait();
                ShaderUnit state=initial;
                for(u32 n=first;n<last;++n) {
                    CarryChallenge::SetInput(state,n+seed,n>=16);
                    context.Run(state);actual[n]=state;
                }
            });
            worker_items+=work.worker_items;calls+=Count;
            Check(work.working_threads==4,"actual parallel participants");
            for(unsigned n=0;n<Count;++n) Check(SameSelected(expected[n],actual[n],mask),"selected output mismatch");
            const auto parallel_env=Env::Capture();
            Check(parallel_env.control==serial_env.control,"FPCR mismatch");
            status_different+=parallel_env.status!=serial_env.status;
            state_different+=CarryChallenge::Capture(expected.back())!=CarryChallenge::Capture(actual.back());
            ++batches;
        }
        std::cout<<Json{{"case",current_case},{"accepted",true},{"passed",true}}.dump()<<'\n';
    }
    original.Apply();
    if(!mutant) Compose();
    std::cout<<Json{{"passed",true},{"accepted",accepted},{"rejected",rejected},{"batches",batches},
        {"shader_calls",calls},{"worker_items",worker_items},{"assertions",checks},
        {"status_different_batches",status_different},{"unused_final_state_different_batches",state_different},
        {"composed_draws",composed_draws},{"composed_invocations",composed_invocations},
        {"composed_worker_invocations",composed_worker_invocations},{"composed_hits",composed_hits}}.dump()<<'\n';
    return 0;
} catch(const std::exception& error) {
    std::cout<<Json{{"passed",false},{"case",current_case},{"error",error.what()},{"assertions",checks}}.dump()<<'\n';
    return 1;
}
