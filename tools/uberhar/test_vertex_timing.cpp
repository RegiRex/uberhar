// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Exercise the actual finite owner, serializer and full-draw
// runner. Clock/provider injection cannot stand in for Thor timing validation.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <json.hpp>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_vertex_timing_batch.h"

namespace Common { const char g_scm_rev[] = "vertex-timing-regression"; }
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*,
                      fmt::string_view, const fmt::format_args&) {}
}
namespace {
using namespace Pica;
using namespace Pica::VertexTiming;
using Json = nlohmann::json;
constexpr u64 Title = 0x0004000000055f00ULL;
unsigned checks{};
std::filesystem::path fixture_root;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) throw std::runtime_error(why);
}

// CodexAstraLocal: Deterministic clocks expose count, failure and ordering bugs;
// simulated work advances separately from the timer's deliberately nonzero cost.
struct FakeClock {
    u64 wall{1'000'000'000}, cpu{123456}, wall_calls{}, cpu_calls{}, fail_wall{}, fail_cpu{};
    bool reverse_wall{}, reverse_cpu{};
    Clock Source() {
        return {this, [](void* p, u64& value) noexcept {
            auto& s = *static_cast<FakeClock*>(p);
            ++s.wall_calls;
            if (s.fail_wall == s.wall_calls) return false;
            s.wall += 7; s.cpu += 2;
            value = s.reverse_wall ? 10'000'000'000ULL - s.wall_calls : s.wall;
            return true;
        }, [](void* p, u64& value) noexcept {
            auto& s = *static_cast<FakeClock*>(p);
            ++s.cpu_calls;
            if (s.fail_cpu == s.cpu_calls) return false;
            s.wall += 11; s.cpu += 5;
            value = s.reverse_cpu ? 1 : s.cpu;
            return true;
        }, [](void*) noexcept { return u64{4242}; }, 1, 1};
    }
    void Work(u64 ns) { wall += ns; cpu += ns; }
};

Config Make(Mode mode = Mode::Detailed) {
    Config cfg;
    cfg.mode = mode; cfg.id = "test"; cfg.title = Title;
    cfg.max_chunks = 64; cfg.max_vertices = 4096; cfg.chunk_vertices = 64;
    return cfg;
}
Json Request(std::string id = "request") {
    return {{"schema", 1}, {"enabled", true}, {"mode", "detailed"},
        {"diagnostic_id", id}, {"title_id", "0004000000055f00"},
        {"trigger", "next_gameplay_transition"}, {"delay_ms", 0},
        {"duration_ms", 10000}, {"period_ms", 100}, {"chunk_vertices", 64},
        {"max_chunks", 64}, {"max_vertices", 4096}, {"seed", 17}};
}
void Arm(Session& session, FakeClock& clock) {
    Common::UberharActivity::Reset(7);
    session.Poll(clock.wall, Common::UberharActivity::Capture());
    clock.Work(1000);
    Common::UberharActivity::SetManualPhase(3);
    session.Poll(clock.wall, Common::UberharActivity::Capture());
}
Json Serialized(Session& session) {
    std::array<char, MaxOutputBytes> output;
    const auto length = session.Serialize(output);
    Check(length.has_value(), "Production serializer exceeded the fixed bound");
    return Json::parse(output.data(), output.data() + *length);
}
void Save(Session& session, const char* name) {
    session.Close();
    std::array<char,MaxOutputBytes> bytes;
    const auto size=session.Serialize(bytes);
    Check(size.has_value(),"Artifact fixture serialization failed");
    std::ofstream{fixture_root/(std::string(name)+".json"),std::ios::binary}
        .write(bytes.data(),*size);
}

void Parser() {
    Check(ParseConfig(Request().dump(), Title).has_value(), "Valid config rejected");
    for (const auto& [key, value] : std::vector<std::pair<std::string, Json>>{
        {"schema",1.0}, {"schema",true}, {"schema",2}, {"enabled",1},
        {"enabled",false}, {"mode","off"}, {"mode","mistyped"},
        {"diagnostic_id","../escape"}, {"diagnostic_id",std::string(49,'x')},
        {"title_id","0004000000055f01"}, {"title_id","55f00"},
        {"trigger","automatic"}, {"duration_ms",10001}, {"duration_ms",0},
        {"delay_ms",180001}, {"period_ms",49}, {"period_ms",1001},
        {"max_chunks",65}, {"max_vertices",4097}, {"chunk_vertices",65},
        {"seed",-1}, {"seed",1.5}, {"typo",true}, {"seed",Json::object()}}) {
        auto config = Request(); config[key] = value;
        Check(!ParseConfig(config.dump(), Title), "Malformed config accepted");
    }
    std::string duplicate = Request().dump();
    duplicate.insert(1, "\"enabled\":false,");
    Check(!ParseConfig(duplicate, Title), "Duplicate enable replaced an earlier value");
    Check(!ParseConfig(std::string(4097, ' '), Title), "Oversized config accepted");
    Check(!ParseConfig("{\"enabled\":true,\"seed\":[[[[[[[[[[[[0]]]]]]]]]]]]}", Title),
          "Deep config accepted");
    Check(!ParseConfig(Request().dump(), 0), "Unknown title accepted");
}

// CodexAstraLocal: Actual partial output writes and temporary/condition carry
// make an accidental prefix/chunk/suffix shader reset observably wrong.
struct Workload {
    ShaderUnit unit;
    u32 loaded{};
    u64 invocations{};
    FakeClock* clock{};
    bool Load(u32 vertex, u32) {
        loaded = vertex;
        unit.input[0].x = f24::FromFloat32(static_cast<float>(vertex % 31));
        if (clock) clock->Work(17);
        return vertex % 7 != 0;
    }
    void Run() {
        ++invocations;
        unit.temporary[0].x = unit.temporary[0].x + f24::FromFloat32(1.0f);
        unit.conditional_code[0] = !unit.conditional_code[0];
        unit.output_bank = (invocations & 1) != 0;
        const u32 component = static_cast<u32>((invocations + loaded) % 4);
        unit.output[unit.output_bank][0][component] = unit.temporary[0].x + unit.input[0].x;
        unit.output[unit.output_bank][1].x = f24::FromFloat32(unit.conditional_code[0] ? 0.25f : 0.75f);
        if (clock) clock->Work(59);
    }
    OutputVertex Convert(const NativeVertexPlan& plan) {
        if (clock) clock->Work(19);
        return plan.Convert(unit);
    }
};

bool Equal(const std::vector<OutputVertex>& a, const std::vector<OutputVertex>& b) {
    return a.size() == b.size() && (a.empty() ||
        std::memcmp(a.data(), b.data(), a.size() * sizeof(OutputVertex)) == 0);
}

void RunCases() {
    using Topology = PipelineRegs::TriangleTopology;
    std::array<bool, 65> start_seen{};
    bool saved_boundary{}, saved_detailed{};
    for (const auto mode : {Mode::Boundary, Mode::Detailed}) {
        for (const bool indexed : {false, true}) {
            for (const auto topology : {Topology::List, Topology::Strip, Topology::Fan, Topology::Shader}) {
                for (u32 pattern = 0; pattern < 3; ++pattern) {
                    PrimitiveAssembler expected_assembler{topology}, timed_assembler{topology};
                    for (const u32 count : {0U,1U,2U,3U,63U,64U,65U,128U,129U,4096U}) {
                        ShaderRegs shader{}; shader.output_mask.Assign(3);
                        RasterizerRegs raster{}; raster.vs_output_total.Assign(2);
                        using Semantic = RasterizerRegs::VSOutputAttributes::Semantic;
                        for (u32 attr = 0; attr < 2; ++attr) {
                            auto& m = raster.vs_output_attributes[attr];
                            m.map_x.Assign(static_cast<Semantic>(attr * 4));
                            m.map_y.Assign(static_cast<Semantic>(attr * 4 + 1));
                            m.map_z.Assign(static_cast<Semantic>(attr * 4 + 2));
                            m.map_w.Assign(static_cast<Semantic>(attr * 4 + 3));
                        }
                        NativeVertexPlan plan{shader, raster};
                        auto at = [&](u32 i) {
                            if (!indexed) return i + 65536;
                            if (pattern == 0) return i % 13;
                            if (pattern == 1) return (i * 129) & 65535;
                            return i % 5 == 0 ? 65535U : (i / 3) % 67;
                        };
                        FakeClock clock;
                        const bool inject = mode == Mode::Detailed && indexed &&
                            topology == Topology::Strip && pattern == 2 && count == 4096;
                        if(inject)clock.fail_wall=55;
                        auto cfg = Make(mode); cfg.seed = checks;
                        Session session{cfg, clock.Source(), "synthetic-carry"};
                        Arm(session, clock);
                        const auto selected = session.Select({11,22,33,count,1,
                            static_cast<u32>(topology),indexed,true});
                        Check(selected.has_value() == (count != 0), "Empty/nonempty admission differs");
                        if (!selected) continue;
                        if (selected->first < start_seen.size()) start_seen[selected->first] = true;
                        Workload expected, actual; actual.clock = &clock;
                        std::vector<OutputVertex> a, b, triangles_a, triangles_b;
                        PrimitiveAssembler::TriangleHandler sink_a = [&](const auto& x,const auto& y,const auto& z) {
                            triangles_a.insert(triangles_a.end(), {x,y,z});
                        };
                        PrimitiveAssembler::TriangleHandler sink_b = [&](const auto& x,const auto& y,const auto& z) {
                            triangles_b.insert(triangles_b.end(), {x,y,z});
                        };
                        if (topology == Topology::Shader) {
                            expected_assembler.SetWinding(); timed_assembler.SetWinding();
                        }
                        auto shade = [&]<bool>(u32 v,u32 i) {
                            expected.Load(v,i); expected.Run(); return expected.Convert(plan);
                        };
                        NativeVertexSamples samples;
                        const auto old = RunNativeVertexBatch<false>(count,indexed,at,shade,
                            [&](const auto& v) { a.push_back(v); expected_assembler.SubmitVertex(v,sink_a); }, samples);
                        const auto timed = RunTimedNativeVertexBatch(count,indexed,at,
                            [&](u32 v,u32 i) { return actual.Load(v,i); }, [&] { actual.Run(); },
                            [&] { return actual.Convert(plan); }, [&](const auto& v) {
                                b.push_back(v); timed_assembler.SubmitVertex(v,sink_b); clock.Work(23);
                            }, session,*selected);
                        Check(old.hits == timed.hits && old.invocations == timed.invocations &&
                              actual.invocations == expected.invocations, "FIFO changed shader invocation population");
                        Check(Equal(a,b), "Timed full draw changed output/carry/defaults");
                        Check(Equal(triangles_a,triangles_b), "Timed full draw changed tails/winding/triangle order");
                        Check(expected_assembler.IsEmpty() == timed_assembler.IsEmpty() &&
                              expected_assembler.HasPendingWinding() == timed_assembler.HasPendingWinding(),
                              "Assembler carry diverged after a chunk");
                        const auto& row = session.Records()[0];
                        Check(row.completed && row.clocks_valid == !inject && row.phase_stable && row.begun,
                              "Complete chunk was not valid");
                        Check(row.observed == selected->count && row.hits + row.misses == row.observed &&
                              row.fused_misses + row.legacy_misses == row.misses,
                              "Observed operation population is not exact");
                        Check(row.stage_calls == std::array<u64,6>{row.observed,row.misses,row.misses,
                              row.misses,row.observed,row.observed}, "Stage operation counts differ from actual work");
                        Check(clock.cpu_calls == (inject?17U:18U), "CPU clocks escaped calibration/chunk endpoints");
                        Check(clock.wall_calls == (inject?55U:52 + (mode == Mode::Detailed ?
                            row.observed * 3 + row.misses * 3 : 0)), "Wall clocks escaped observed interval");
                        Check((mode == Mode::Boundary) == std::all_of(row.stage_ns.begin(),row.stage_ns.end(),
                            [](u64 n){return n == 0;}), "Boundary mode measured interior stages");
                        const auto report = Serialized(session);
                        Check(report["records"][0]["observed_inputs"] == selected->count,
                              "Serialized count differs from actual producer");
                        bool& saved=mode==Mode::Boundary?saved_boundary:saved_detailed;
                        if(!saved && count==4096 && indexed) {
                            Save(session,mode==Mode::Boundary?"boundary":"detailed");saved=true;
                        }
                        if(inject)Save(session,"stateful-clock-failed");
                    }
                }
            }
        }
    }
    Check(start_seen[0] && start_seen[1], "Boundary starts were not exercised");
}

void EmitWork(Session& session, FakeClock& clock, u32 count = 64) {
    const auto range = session.Select({std::numeric_limits<u64>::max(),
        std::numeric_limits<u64>::max(),4095,count,16,3,true,true});
    Check(range.has_value(), "Fixture selection failed");
    session.Begin(Common::UberharActivity::Capture());
    for (u32 i = 0; i < range->count; ++i) {
        for (u32 s = 0; s < StageCount; ++s) {
            clock.Work(20); session.Mark(static_cast<Stage>(s));
        }
        session.InputRoute(true); session.Input(false);
    }
    session.End(Common::UberharActivity::Capture());
}

void Policy() {
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"};
        Common::UberharActivity::Reset(7); Common::UberharActivity::SetManualPhase(3);
        s.Poll(c.wall,Common::UberharActivity::Capture());
        Check(!s.Select({1,2,3,64}), "Startup gameplay was treated as an arm edge");
        c.wall += ArmTimeoutNs;
        s.Poll(c.wall,Common::UberharActivity::Capture());
        Check(s.GetSummary().stop == Stop::ArmTimeout && c.wall_calls == 0 && c.cpu_calls == 0,
              "Unarmed diagnostic did not stop without clocks");
    }
    {
        FakeClock c; auto cfg=Make(); cfg.delay_ms=500; Session s{cfg,c.Source(),"fixture"}; Arm(s,c);
        Check(!s.Select({1,2,3,64}), "Delay ignored");
        c.wall += 500'000'000; s.Poll(c.wall,Common::UberharActivity::Capture());
        Check(!s.Select({1,2,3,4097}) && s.GetSummary().oversized_batches == 1,
              "Oversized diagnostic draw did not retain ordinary path");
        EmitWork(s,c);
        Check(!s.Select({1,2,3,64}), "Same timestamp caused catch-up sampling");
        s.PolicyChanged(); const auto reads=c.wall_calls;
        s.Poll(c.wall,Common::UberharActivity::Capture());
        Check(!s.Select({1,2,3,64}) && c.wall_calls == reads, "Terminal policy read clocks");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        const auto r=s.Select({1,2,3,64}); Check(r.has_value(),"Late prefix select");
        c.wall=s.GetSummary().window_end_ns;
        Check(!s.Begin(Common::UberharActivity::Capture()) && s.GetSummary().stop==Stop::Window,
              "Late prefix began after the window");
        Check(s.GetSummary().records==1 && s.GetSummary().reserved_vertices==64,
              "Late prefix refunded observation budget");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        const auto r=s.Select({1,2,3,64}); Check(r.has_value(),"Overhang select");
        Check(s.Begin(Common::UberharActivity::Capture()),"Overhang begin");
        c.wall=s.GetSummary().window_end_ns+1;
        for(u32 i=0;i<64;++i) { s.Input(false); s.InputRoute(true); }
        s.End(Common::UberharActivity::Capture());
        Check(s.Records()[0].overhang && s.Records()[0].completed,"Overhang suppressed actual work");
    }
    {
        FakeClock c; auto cfg=Make(); cfg.max_vertices=65; Session s{cfg,c.Source(),"fixture"}; Arm(s,c);
        EmitWork(s,c); c.wall += 100'000'000; s.Poll(c.wall,Common::UberharActivity::Capture());
        EmitWork(s,c);
        Check(s.Records()[1].range.count == 1 && s.GetSummary().stop == Stop::Budget,
              "Final input cap was exceeded/refilled");
        const auto wall=c.wall_calls, cpu=c.cpu_calls;
        s.Poll(c.wall,Common::UberharActivity::Capture()); s.Select({1,2,3,64});
        Check(c.wall_calls==wall && c.cpu_calls==cpu,"Budget termination still reads clocks");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        std::thread other([&] { s.Poll(c.wall,Common::UberharActivity::Capture()); }); other.join();
        Check(s.GetSummary().stop==Stop::Identity && c.cpu_calls==0,"Foreign owner CPU admitted");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        Common::UberharActivity::Reset(8); s.Poll(c.wall,Common::UberharActivity::Capture());
        Check(s.GetSummary().stop==Stop::Identity,"Run transition reused collector");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        Common::UberharActivity::SetManualPhase(2); s.Poll(c.wall,Common::UberharActivity::Capture());
        Check(s.GetSummary().stop==Stop::Phase,"Phase change was relabeled gameplay");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        auto range=s.Select({1,2,3,64,1,0,false,true}); Check(range.has_value(),"Phase-back select");
        Check(s.Begin(Common::UberharActivity::Capture()),"Phase-back begin");
        for(u32 i=0;i<64;++i) {
            for(u32 stage=0;stage<StageCount;++stage) s.Mark(static_cast<Stage>(stage));
            s.Input(false);s.InputRoute(true);
        }
        Common::UberharActivity::SetManualPhase(2);Common::UberharActivity::SetManualPhase(3);
        s.End(Common::UberharActivity::Capture());
        Check(!s.Records()[0].phase_stable && s.GetSummary().stop==Stop::Phase,
              "Changed-and-restored phase lost its generation");
        Save(s,"phase-changed");
    }
    {
        FakeClock c; c.wall=std::numeric_limits<u64>::max()-2'000; Session s{Make(),c.Source(),"fixture"};
        Arm(s,c); Check(s.GetSummary().stop==Stop::Clock,"Window deadline overflow accepted");
    }
    {
        FakeClock c; Session s{Make(),c.Source(),"fixture"}; Arm(s,c);
        s.Poll(c.wall-1,Common::UberharActivity::Capture());
        Check(s.GetSummary().stop==Stop::Identity,"Backwards poll accepted");
    }
    for(auto config : {Make(Mode::Off),Make(static_cast<Mode>(100)),Make()}) {
        if(config.mode!=Mode::Off)config.max_chunks=65;
        FakeClock c;Session s{config,c.Source(),"fixture"};Arm(s,c);
        Check(s.GetSummary().stop==Stop::Policy && c.cpu_calls==0 && c.wall_calls==0,
              "Public constructor escaped hard/off limits");
        if(config.mode!=Mode::Boundary&&config.mode!=Mode::Detailed) {
            std::array<char,128> output;Check(!s.Serialize(output),"Invalid enum reached serializer table");
        }
    }
}

void Failures() {
    // CodexAstraLocal: Fail before calibration, at chunk start, inside detail
    // and at the final CPU endpoint. Every shader and submit still executes.
    for (u64 fail_cpu : {1ULL,17ULL,18ULL}) {
        FakeClock c; c.fail_cpu=fail_cpu; Session s{Make(),c.Source(),"failure"}; Arm(s,c);
        const auto range=s.Select({1,2,3,64,1,0,false,true});
        u32 loads{}, runs{}, submits{};
        const auto counts=RunTimedNativeVertexBatch(64,false,[](u32 i){return i;},
            [&](u32,u32){++loads;return true;},[&]{++runs;},[]{return OutputVertex{};},
            [&](const auto&){++submits;},s,*range);
        Check(loads==64 && runs==64 && submits==64 && counts.invocations==64,
              "CPU clock failure changed rendering");
        Check(s.GetSummary().clock_failures==1 && s.GetSummary().stop==Stop::Clock &&
              !s.Records()[0].clocks_valid, "CPU failure was valid evidence");
        const auto total=c.cpu_calls+c.wall_calls;
        s.Poll(c.wall,Common::UberharActivity::Capture()); s.Select({1,2,3,64});
        Check(total==c.cpu_calls+c.wall_calls,"Failure kept timestamping");
        if(fail_cpu==18)Save(s,"clock-failed");
    }
    for (u64 fail_wall : {1ULL,49ULL,54ULL,436ULL}) {
        FakeClock c; c.fail_wall=fail_wall; Session s{Make(),c.Source(),"failure"}; Arm(s,c);
        auto range=s.Select({1,2,3,64}); u32 submits{};
        RunTimedNativeVertexBatch(64,false,[](u32 i){return i;},[](u32,u32){return false;},[]{},
            []{return OutputVertex{};},[&](const auto&){++submits;},s,*range);
        Check(submits==64,"Wall failure dropped a vertex");
        Check(s.GetSummary().stop==Stop::Clock && !s.Records()[0].clocks_valid,
              "Wall failure did not disable evidence");
    }
    {
        FakeClock c;c.reverse_wall=true;Session s{Make(),c.Source(),"reverse-wall"};Arm(s,c);
        s.Select({1,2,3,64});Check(!s.Begin(Common::UberharActivity::Capture()),"Backwards wall accepted");
        Check(s.GetSummary().stop==Stop::Clock,"Backwards wall did not close collector");
    }
    {
        FakeClock c;Session s{Make(),c.Source(),"reverse-cpu"};Arm(s,c);
        s.Select({1,2,3,1});Check(s.Begin(Common::UberharActivity::Capture()),"Reverse CPU begin");
        s.Input(false);s.InputRoute(true);c.reverse_cpu=true;s.End(Common::UberharActivity::Capture());
        Check(s.GetSummary().stop==Stop::Clock&&!s.Records()[0].clocks_valid,"Backwards CPU accepted");
    }
}

void SerializationAndIo(const std::filesystem::path& root) {
    FakeClock c; c.wall=9'000'000'000'000'000'000ULL; c.cpu=8'000'000'000'000'000'000ULL;
    auto cfg=Make(); cfg.id=std::string(48,'z'); Session s{cfg,c.Source(),"cpu_jit_arm64"}; Arm(s,c);
    for(u32 row=0;row<64;++row) {
        EmitWork(s,c,4096);
        c.wall+=100'000'000; s.Poll(c.wall,Common::UberharActivity::Capture());
    }
    const auto report=Serialized(s);
    Check(report["records"].size()==64 && report["summary"]["reserved_vertices"]==4096 &&
          report["summary"]["selected_draw_inputs"]==262144,
          "Maximum production record budget failed");
    std::array<char,MaxOutputBytes> bytes;
    const auto size=s.Serialize(bytes);
    std::ofstream{root/"maximum.json",std::ios::binary}.write(bytes.data(),*size);
    std::printf("Maximum 64-row production artifact: %zu bytes\n",*size);
    Check(c.cpu_calls==144 && c.wall_calls==24880,"Lifetime diagnostic clock cap mismatch");
    std::array<char,128> short_output;
    Check(!s.Serialize(short_output),"Serializer exceeded short caller buffer");

    FileUtil::root=root.string(); std::filesystem::create_directories(root/"config");
    const auto config_path=root/"config/uberhar_vertex_timing.json";
    { std::ofstream file{config_path}; file << Request("io-once").dump(); }
    {
        auto loaded=Session::Load(Title,"host-test");
        Check(loaded!=nullptr,"Production config load failed");
    }
    const auto artifact=root/"dump/uberhar_vertex_timing/io-once.json";
    Check(std::filesystem::exists(artifact),"Normal owner teardown did not retain artifact");
    const auto before=std::filesystem::file_size(artifact);
    std::filesystem::copy_file(artifact,root/"empty.json");
    Check(!Session::Load(Title,"host-test"),"Existing artifact ID admitted");
    Check(std::filesystem::file_size(artifact)==before,"Existing evidence overwritten");
    { auto off=Request("off"); off["enabled"]=false; std::ofstream file{config_path};file<<off.dump(); }
    const auto writes=FileUtil::writes;
    Check(!Session::Load(Title,"host-test") && FileUtil::writes==writes,"Off path retained artifact");
    // CodexAstraLocal: Provider exceptions, short writes and late competing files
    // must not throw through owner destruction, retry, or replace old evidence.
    for(const auto id : {"directory-fails","short-write","flush-fails","late-file"}) {
        {std::ofstream file{config_path};file<<Request(id).dump();}
        auto loaded=Session::Load(Title,"host-test");Check(loaded!=nullptr,"IO failure fixture load");
        const auto output=root/"dump/uberhar_vertex_timing"/(std::string(id)+".json");
        FileUtil::fail_directory=std::string_view(id)=="directory-fails";
        FileUtil::short_write=std::string_view(id)=="short-write";
        FileUtil::fail_flush=std::string_view(id)=="flush-fails";
        if(std::string_view(id)=="late-file") {std::ofstream file{output};file<<"preserve-exactly";}
        const auto calls=FileUtil::writes;
        loaded.reset();
        Check(FileUtil::writes-calls<=1,"Failed export retried");
        if(std::string_view(id)=="late-file") {
            std::ifstream file{output};std::string contents;file>>contents;
            Check(contents=="preserve-exactly"&&FileUtil::writes==calls,"Late evidence overwritten");
        }
        if(FileUtil::fail_directory)Check(!std::filesystem::exists(output),"Directory failure made fake report");
        FileUtil::fail_directory=FileUtil::short_write=FileUtil::fail_flush=false;
    }
}
} // namespace

int main(int argc,char**argv) {
    if(argc!=2) throw std::runtime_error("fresh fixture directory required");
    const std::filesystem::path path{argv[1]}; std::filesystem::create_directories(path);
    fixture_root=path;
    Parser(); Policy(); RunCases(); Failures(); SerializationAndIo(path);
    std::printf("PASS: %u production timing/parser/lifetime/FIFO/carry/assembly checks\n",checks);
}
