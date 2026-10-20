// CodexAstraLocal: Actual leased JIT/input/FIFO/88-byte comparisons for the live
// packet implementation. Only diagnostic and forced scheduling seams are modeled.
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>
#include <json.hpp>
#include "common/logging/log.h"
#include "video_core/pica/uberhar_cpu_draw_queue.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/uberhar_parallel_vertex.h"
#include "video_core/shader/shader_jit.h"
#include "video_core/shader_recovery_error.h"
#include "test_parallel_vertex_observable_cases.h"
#include "actual_writer.h"

// CodexAstraLocal: Standalone execution has no logging service. Error diagnostics
// become explicit fixture failures; ordinary diagnostic output needs no sink.
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format, args));
}
}
using namespace Pica;
using Environment = Common::Uberhar::ParallelFloatEnvironment;
using Json = nlohmann::json;
u64 checks{}, comparisons{};
void Need(bool yes, const char* why) { ++checks; if (!yes) throw std::runtime_error(why); }

// CodexAstraLocal: FP status is a fixture observation, not a shipped packet
// field. Fixed storage records the actual writer's flags before worker restore;
// each normal wave has exactly sixteen independently retained output buffers.
struct FlagRecord { const u8* output{}; Environment::Status status{}; };
std::mutex flags_mutex;
std::array<FlagRecord, 16> flags;
std::size_t flag_count{};
void ObservedWriter(std::span<const OutputVertex> vertices, std::span<u8> output) noexcept {
    VideoCore::ActualWriter(vertices, output);
    const auto status = Environment::Flags();
    std::lock_guard lock{flags_mutex};
    if (flag_count == flags.size()) std::abort();
    flags[flag_count++] = {output.data(), status};
}
Environment::Status ObservedFlags(const u8* output) {
    std::lock_guard lock{flags_mutex};
    for (std::size_t n = 0; n < flag_count; ++n)
        if (flags[n].output == output) return flags[n].status;
    throw std::runtime_error("writer status observation missing");
}

// CodexAstraLocal: A removed terminal publication must fail the actual Wait
// contract without leaving a future destructor blocked. Only this specific
// post-drain wait deadline emits the lost-completion diagnostic and exits;
// generic process timeouts are never accepted by the regression driver.
bool WaitTerminal(const CpuDrawPacket& packet, const char* missing) {
    auto waited = std::async(std::launch::async, [&] {
        try { packet.Wait(); } catch (const VideoCore::ShaderRecoveryError&) { return true; }
        return false;
    });
    ++checks;
    if (waited.wait_for(std::chrono::seconds{10}) != std::future_status::ready) {
        std::cout << Json{{"passed", false}, {"checks", checks}, {"error", missing}}.dump() << std::endl;
        std::_Exit(1);
    }
    return waited.get();
}

// CodexAstraLocal: A blocking entry fixture creates real pending waves, without
// changing the production queue or manufacturing worker activity by polling.
std::mutex gate_mutex;
std::condition_variable gate_cv;
bool gate_open{true};
void Start() { std::unique_lock lock{gate_mutex}; gate_cv.wait(lock, [] { return gate_open; }); }
void Gate(bool open) { { std::lock_guard lock{gate_mutex}; gate_open = open; } gate_cv.notify_all(); }

// CodexAstraLocal: Generated from the real PICA reset branches/reconciliation;
// only owner services unrelated to these operations are replaced by the fixture.
#include "actual_reset_owner.h"

// CodexAstraLocal: The driver injects these calls only into a copied CPP. They
// force the otherwise rare terminal boundaries without replacing JIT execution.
namespace PacketProbe {
std::atomic<bool> fail_packet{}, fail_wave{};
std::mutex mutex;
std::condition_variable cv;
bool entered{}, release{};
void BeforeInvocation() {
    if (fail_packet.exchange(false)) throw std::runtime_error("injected packet failure");
}
void BeforeWave() {
    if (!fail_wave.load()) return;
    std::unique_lock lock{mutex}; entered = true; cv.notify_all();
    cv.wait(lock, [] { return release; });
    throw std::runtime_error("injected coordinator failure");
}
}

Environment Env(unsigned mode) {
    auto result = Environment::Capture();
#if defined(__aarch64__)
    result.control = u64(mode & 3) << 22 | (mode & 4 ? 1ULL << 24 : 0) | (mode & 8 ? 1ULL << 25 : 0);
    result.status = 0;
#else
    const int modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    std::fesetround(modes[mode & 3]); std::feclearexcept(FE_ALL_EXCEPT); result = Environment::Capture();
    result.mxcsr = (result.mxcsr & ~0x8040U) | (mode & 4 ? 0x8040U : 0);
#endif
    return result;
}
constexpr unsigned ModeCount =
#if defined(__aarch64__)
    16;
#else
    8;
#endif

// CodexAstraLocal: These programs deliberately include selected carried values
// and prior condition/address use. Whole draws preserve the serial invocation
// history; no per-vertex independence certificate is used to erase that state.
std::vector<CarryChallenge::Case> Programs() {
    using namespace CarryChallenge;
    std::vector<Case> result;
    Builder regular("input_uniform_math");
    regular.Arithmetic(Op::MUL, Dst::MakeTemporary(0), 15, Src::MakeInput(0), Src::MakeFloat(0));
    regular.Arithmetic(Op::ADD, Dst::MakeOutput(0), 15, Src::MakeTemporary(0), Src::MakeInput(1));
    regular.Arithmetic(Op::MOV, Dst::MakeOutput(1), 15, Src::MakeInput(2));
    auto a = regular.Finish(); a.output_mask = 3; result.push_back(a);
    Builder carry("selected_recurrence");
    carry.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, Src::MakeTemporary(0), Src::MakeInput(0));
    carry.Arithmetic(Op::MUL, Dst::MakeOutput(0), 15, Src::MakeTemporary(0), Src::MakeFloat(1));
    carry.Arithmetic(Op::MOV, Dst::MakeOutput(1), 15, Src::MakeInput(2));
    auto b = carry.Finish(); b.output_mask = 3; result.push_back(b);
    auto sensitive = ObservableSensitive::BuildCases();
    result.insert(result.end(), sensitive.begin(), sensitive.end());
    Need(!AnalyzeParallelVertex(b.code, b.swizzles, b.entry, 0, b.output_mask,
        ParallelVertexContract::SelectedOutputValues).Supported(), "true per-vertex carry refusal");
    // CodexAstraLocal: The selected policy excludes light shaders. Add real
    // arithmetic with a consumed quaternion output, preserving the separate
    // carried position/control witness in output0 for every qualified case.
    for (auto& c : result) {
        Builder work("consumed_work");
        for (u32 n = 0; n < 35; ++n)
            work.Arithmetic(Op::ADD, Dst::MakeTemporary(14), 15,
                n ? Src::MakeTemporary(14) : Src::MakeInput(1), Src::MakeInput(1));
        work.Arithmetic(Op::MOV, Dst::MakeOutput(1), 15, Src::MakeTemporary(14));
        const auto tail = work.Finish();
        Need(tail.swizzles[0] == c.swizzles[0], "consumed-tail descriptor identity");
        u32 end = c.entry;
        while (nihstro::Instruction{c.code[end]}.opcode.Value().EffectiveOpCode() != Op::END) ++end;
        std::copy_n(tail.code.begin(), 37, c.code.begin() + end);
        // This new tail uses the identity descriptor already shared by each case.
        c.output_mask |= 2;
    }
    return result;
}

struct Live {
    CpuDrawCapture source;
    std::vector<u8> bytes;
    std::vector<u16> indices;
    PipelineRegs::TriangleTopology topology;
    Live(const ShaderRunLease& lease, const CarryChallenge::Case& shader, u32 count,
         u32 seed, bool indexed, u32 shape = 0)
        : bytes((count + 136) * 64), indices(count),
          topology{shape % 2 ? PipelineRegs::TriangleTopology::Shader : PipelineRegs::TriangleTopology::List} {
        source.shader_lease = lease; source.shader.main_offset.Assign(shader.entry);
        source.count = count; source.indexed = indexed; source.index_width = 2;
        source.base_vertex = shape % 3 == 0 ? 127 : 0;
        source.base_address = 0x1000;
        source.available_attributes = 4; source.shader.max_input_attribute_index.Assign(3);
        source.shader.input_attribute_to_register_map_low = shape % 3 ? 0x3210 : 0x3200;
        source.shader.output_mask.Assign(shader.output_mask);
        source.rasterizer.vs_output_total.Assign(std::popcount(shader.output_mask));
        for (u32 n = 0; n < 2; ++n) {
            using S = RasterizerRegs::VSOutputAttributes::Semantic;
            auto& map = source.rasterizer.vs_output_attributes[n]; const u32 base = n ? 4 : 0;
            map.map_x.Assign(static_cast<S>(base)); map.map_y.Assign(static_cast<S>(base + 1));
            map.map_z.Assign(static_cast<S>(base + 2)); map.map_w.Assign(static_cast<S>(base + 3));
        }
        for (u32 n = 0; n < 96; ++n) for (u32 k = 0; k < 4; ++k)
            source.uniforms.f[n][k] = f24::FromFloat32(float((n + k + seed) % 5 + 1) * .125f);
        for (u32 n = 0; n < 16; ++n) for (u32 k = 0; k < 4; ++k)
            source.defaults[n][k] = f24::FromFloat32(float(n + k + seed % 7) * .0625f);
        for (u32 n = 0; n < bytes.size() / 4; ++n) {
            const u32 raw = 0x3e800000U + ((n * 13 + seed) % 64) * 0x00020000U;
            std::memcpy(bytes.data() + n * 4, &raw, 4);
        }
        for (u32 a = 0; a < 4; ++a) {
            const auto format = static_cast<PipelineRegs::VertexAttributeFormat>((shape + a) % 4);
            source.attributes[a] = {a * 16, shape == 6 ? 0U : 64U, 1U + (shape + a) % 4,
                                    format, shape % 4 == 1 && a == 3};
        }
        for (u32 n = 0; n < count; ++n)
            indices[n] = static_cast<u16>(127 + (count >= 126 && n % 5 == 4 ? n - 3 : n));
        source.index_bytes = {reinterpret_cast<const u8*>(indices.data()), indices.size() * 2};
        source.bindings[0] = {source.base_address, bytes}; source.binding_count = 1;
    }
    u32 Vertex(u32 n) const { return source.indexed ? indices[n] : source.base_vertex + n; }
    void Mutate() {
        std::fill(bytes.begin(), bytes.end(), 0); std::fill(indices.begin(), indices.end(), 0);
        source.defaults = {}; source.uniforms = {}; source.shader = {}; source.rasterizer = {};
    }
};

struct Expected {
    std::vector<u8> hardware;
    std::array<OutputVertex, 2> pair;
    NativeVertexCounts counts;
    Environment::Status flags;
};
// CodexAstraLocal: The oracle uses original live addresses, the actual bound
// JIT/FIFO, and original scalar primitive submission plus AddTriangle conversion.
Expected Serial(const Live& live) {
    const auto& source = live.source; NativeVertexInputPlan input;
    u32 maximum{}; for (u32 n = 0; n < source.count; ++n) maximum = std::max(maximum, live.Vertex(n));
    Need(input.Prepare(source.shader, source.available_attributes, source.base_address, maximum,
        [&](u32 a) { return source.attributes[a]; },
        [&](PAddr a) { return std::span<const u8>{live.bytes}.subspan(a - source.base_address); }, true)
        == NativeVertexInputPlan::Result::Ready, "serial plan");
    NativeVertexPlan output(source.shader, source.rasterizer); ShaderUnit unit;
    const auto context = source.shader_lease.Bind(source.uniforms); NativeVertexSamples samples;
    VideoCore::TransportSink sink; PrimitiveAssembler assembly{live.topology};
    std::vector<OutputVertex> outputs;
    const auto counts = RunNativeVertexBatch<false>(source.count, source.indexed,
        [&](u32 n) { return live.Vertex(n); },
        [&]<bool>(u32 vertex, u32) { input.Load(unit, source.defaults, vertex); context.Run(unit); return output.Convert(unit); },
        [&](const OutputVertex& v) {
            outputs.push_back(v);
            assembly.SubmitVertex(v, [&](const auto& a, const auto& b, const auto& c) { sink.AddTriangle(a, b, c); });
        }, samples);
    Expected result; result.counts = counts; result.flags = Environment::Flags();
    result.pair = {outputs[source.count - 3], outputs[source.count - 2]};
    const auto* bytes = reinterpret_cast<const u8*>(sink.vertex_batch.data());
    result.hardware.assign(bytes, bytes + sink.vertex_batch.size() * 88); return result;
}
void Equal(const CpuDrawPacket& packet, const Expected& expected) {
    const auto bytes = packet.HardwareBytes();
    Need(bytes.size() == expected.hardware.size() &&
        !std::memcmp(bytes.data(), expected.hardware.data(), bytes.size()), "hardware differs");
    const auto joined = packet.CompletedResult();
    Need(!std::memcmp(joined.last_pair.data(), expected.pair.data(), sizeof(joined.last_pair)), "last pair differs");
    Need(joined.counts.invocations == expected.counts.invocations &&
        joined.counts.hits == expected.counts.hits, "FIFO counts differ");
    Need(ObservedFlags(bytes.data()) == expected.flags, "captured FP flags differ");
    ++comparisons;
}

ShaderRunLease Lease(Shader::JitEngine& engine, ShaderSetup& setup, const CarryChallenge::Case& c) {
    setup.UpdateProgramCode(c.code, 0); setup.UpdateSwizzleData(c.swizzles, 0);
    engine.SetupBatch(setup, c.entry); return engine.LeaseForDraw(setup);
}
void Normal(const std::vector<CarryChallenge::Case>& programs) {
    for (u32 cores : {1U, 2U, 4U, 8U}) for (u32 mode = 0; mode < ModeCount; ++mode) {
        const auto env = Env(mode);
        { std::lock_guard lock{flags_mutex}; flag_count = 0; }
        Gate(cores == 1);
        CpuDrawExecutor queue{cores, 16, 8 * 1024 * 1024, Start};
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release_gate;
        std::vector<std::shared_ptr<CpuDrawPacket>> packets; std::vector<Expected> expected;
        {
            Shader::JitEngine engine; ShaderSetup setup;
            for (u32 n = 0; n < 16; ++n) {
                const u32 sizes[]{96, 99, 126, 192, 255, 768, 4095, 4098};
                const auto& c = programs[n % programs.size()]; auto lease = Lease(engine, setup, c);
                Live live{lease, c, sizes[n % 8], n + 1, bool(n % 2), n % 8};
                env.Apply(); expected.push_back(Serial(live)); env.Apply();
                auto packet = queue.Capture(live.source, ObservedWriter);
                Need(bool(packet), "valid capture refused"); Need(!queue.LastCaptureAtCapacity(), "stale capacity reason");
                Need(queue.Submit(packet), "first publication refused");
                Need(!queue.Submit(packet), "duplicate publication");
                packets.push_back(std::move(packet)); live.Mutate();
            }
        }
        // Engine and all original source buffers are gone before queued work.
        if (cores > 1) Need(queue.GetStatistics().completed == 0, "forced pre-execution lifetime seam missed");
        Gate(true);
        auto renderer = std::async(std::launch::async, [&] {
            for (u32 n = 0; n < packets.size(); ++n) packets[n]->Wait();
        });
        Need(renderer.wait_for(std::chrono::seconds{15}) == std::future_status::ready,
             "renderer wait needs owner drain"); renderer.get();
        for (u32 n = 0; n < packets.size(); ++n) Equal(*packets[n], expected[n]);
        queue.Drain(); const auto stats = queue.GetStatistics();
        Need(stats.completed == 16 && stats.submitted == 16 && stats.failed == 0, "completed population");
        Need(stats.coordinator_packets + stats.auxiliary_packets == 16, "packet role accounting");
        packets.clear(); Need(queue.GetStatistics().resident_packets == 0, "completed reference credit leaked");
    }
}

// CodexAstraLocal: Inspect the fields the real save-state archive consumes,
// including the retained pair even when no triangle is partially assembled.
struct AssemblySnapshot {
    std::vector<u8> bytes;
    template <typename T> AssemblySnapshot& operator&(T& value) {
        const auto* begin = reinterpret_cast<const u8*>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(value));
        return *this;
    }
};
std::vector<u8> SavedAssembly(PrimitiveAssembler& assembler) {
    AssemblySnapshot saved;
    boost::serialization::access::serialize(saved, assembler, 0);
    return saved.bytes;
}

// CodexAstraLocal: Keep A blocked across the actual redundant register writes,
// enqueue independent B, then check ordered bytes/counts and the saved tail.
// A test-only pre-drain hook releases workers on a deliberately broken branch,
// so restoring unconditional drains fails directly instead of hanging a test.
void Resets(const std::vector<CarryChallenge::Case>& programs) {
    Shader::JitEngine engine; ShaderSetup setup;
    const auto lease = Lease(engine, setup, programs[0]);
    for (auto topology : {PipelineRegs::TriangleTopology::List,
                          PipelineRegs::TriangleTopology::Shader}) {
        Gate(false);
        ResetOwner owner;
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release_gate;
        owner.primitive_assembler.Reconfigure(topology);
        auto& state = *owner.deferred_vertices;
        std::array<Expected, 2> expected;
        std::array<std::shared_ptr<CpuDrawPacket>, 2> packets;
        for (u32 n = 0; n < 2; ++n) {
            Live live{lease, programs[0], 126, n + 3, true};
            live.topology = topology;
            expected[n] = Serial(live);
            packets[n] = state.executor.Capture(live.source, VideoCore::ActualWriter);
            Need(packets[n] && state.executor.Submit(packets[n]), "reset packet publication");
            state.pending.push_back(packets[n]);
            if (n == 0) {
                owner.Topology(topology);
                owner.Restart();
                Need(state.synchronous_boundaries == 0, "redundant reset drained packets");
                Need(state.executor.GetStatistics().completed == 0, "reset worker gate escaped");
            }
            live.Mutate();
        }
        owner.ReconcileDeferredVertices();
        Need(state.deferred_topology_resets == 1 && state.deferred_primitive_resets == 1,
             "reset evidence counters");
        for (u32 n = 0; n < 2; ++n) {
            const auto bytes = packets[n]->HardwareBytes();
            Need(bytes.size() == expected[n].hardware.size() &&
                 !std::memcmp(bytes.data(), expected[n].hardware.data(), bytes.size()),
                 "reset ordered hardware differs");
            ++comparisons;
        }
        PrimitiveAssembler serial{topology};
        for (const auto& result : expected) {
            // The original scalar serial oracle supplied this exact final pair.
            serial.AdoptCompletedTriangleTail(result.pair);
            serial.Reset();
        }
        Need(SavedAssembly(owner.primitive_assembler) == SavedAssembly(serial),
             "reset saved pair differs");
        Need(state.completed == 2 && state.inputs == 252 &&
             state.invocations == expected[0].counts.invocations + expected[1].counts.invocations &&
             state.hits == expected[0].counts.hits + expected[1].counts.hits,
             "reset owner counts differ");
        owner.ReconcileDeferredVertices();
        Need(state.completed == 2 && state.synchronous_boundaries == 1, "reset duplicate prefix");

        // CodexAstraLocal: Changed topology must adopt before Reconfigure, and
        // strip/fan, partial triangles and pending winding cannot use the bypass.
        auto ticket = state.executor.Capture(Live{lease, programs[0], 126, 7, true}.source,
                                             VideoCore::ActualWriter);
        Need(ticket && state.executor.Submit(ticket), "topology boundary packet");
        state.pending.push_back(ticket);
        const auto next = topology == PipelineRegs::TriangleTopology::List
                              ? PipelineRegs::TriangleTopology::Shader
                              : PipelineRegs::TriangleTopology::List;
        owner.Topology(next);
        Need(state.synchronous_boundaries == 2 && state.pending.empty(), "changed topology did not drain");
        PrimitiveAssembler partial{topology};
        partial.SubmitVertex(expected[0].pair[0], [](const auto&, const auto&, const auto&) {});
        Need(!partial.CanResetDeferredTriangles(topology), "partial reset admitted");
        partial.Reset(); partial.SetWinding();
        Need(!partial.CanResetDeferredTriangles(topology), "winding reset admitted");
        for (auto strip : {PipelineRegs::TriangleTopology::Strip, PipelineRegs::TriangleTopology::Fan})
            Need(!PrimitiveAssembler{strip}.CanResetDeferredTriangles(strip), "strip/fan reset admitted");
    }
    // Packet failure must remain terminal after a redundant reset; retrying a
    // real boundary must neither recount a successful prefix nor leave borrowers.
    Gate(false); ResetOwner owner;
    struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release_gate;
    auto& state = *owner.deferred_vertices;
    Live live{lease, programs[0], 126, 9, true};
    auto ticket = state.executor.Capture(live.source, VideoCore::ActualWriter);
    Need(ticket && state.executor.Submit(ticket), "failed reset packet");
    state.pending.push_back(ticket);
    PacketProbe::fail_packet = true;
    owner.Restart();
    for (int retry = 0; retry < 2; ++retry) {
        bool failed{};
        try { owner.ReconcileDeferredVertices(); }
        catch (const VideoCore::ShaderRecoveryError&) { failed = true; }
        Need(failed && state.completed == 0 && state.reconciled == 0, "reset failure lost terminal state");
    }
    Need(state.executor.GetStatistics().completed == 1, "reset failure borrower not drained");
}

// CodexAstraLocal: Credit includes unpublished and renderer-held packets;
// malformed/effect refusals must not be mistaken for a need to drain commands.
void Boundaries(const std::vector<CarryChallenge::Case>& programs) {
    Shader::JitEngine engine; ShaderSetup setup; auto lease = Lease(engine, setup, programs[0]);
    Live live{lease, programs[0], 126, 1, true}; CpuDrawExecutor queue{4, 2, 1 << 20};
    auto a = queue.Capture(live.source, VideoCore::ActualWriter);
    auto b = queue.Capture(live.source, VideoCore::ActualWriter);
    Need(a && b, "capacity fixtures");
    Need(!queue.Capture(live.source, VideoCore::ActualWriter) && queue.LastCaptureAtCapacity(), "held credit not bounded");
    auto bad = live.source; bad.count = 1;
    Need(!queue.Capture(bad, VideoCore::ActualWriter) && !queue.LastCaptureAtCapacity(), "refusal reason conflated");
    bool unpublished{}; try { a->Wait(); } catch (const VideoCore::ShaderRecoveryError&) { unpublished = true; }
    Need(unpublished, "unpublished wait did not refuse");
    Need(queue.Submit(a) && queue.Submit(b), "bounded publish"); queue.Drain();
    Need(!queue.Capture(live.source, VideoCore::ActualWriter) && queue.LastCaptureAtCapacity(), "completed retained credit escaped");
    a.reset(); auto c = queue.Capture(live.source, VideoCore::ActualWriter); Need(bool(c), "released credit not reusable");
    c.reset(); b.reset(); Need(queue.GetStatistics().resident_packets == 0, "canceled capture retained credit");
    CpuDrawExecutor other{1}; a = queue.Capture(live.source, VideoCore::ActualWriter);
    Need(!other.Submit(a), "foreign executor accepted ticket"); a.reset();
    bad = live.source; bad.index_bytes = bad.index_bytes.first(1);
    Need(!queue.Capture(bad, VideoCore::ActualWriter) && !queue.LastCaptureAtCapacity(), "index bounds refused as capacity");
    bad = live.source; bad.bindings[0].bytes = bad.bindings[0].bytes.first(4);
    Need(!queue.Capture(bad, VideoCore::ActualWriter), "short input accepted");
    CpuDrawExecutor tiny{2, 8, 16};
    Need(!tiny.Capture(live.source, VideoCore::ActualWriter) && tiny.LastCaptureAtCapacity(), "oversized footprint accepted");
    auto all = CarryChallenge::BuildCases();
    for (const auto& caze : all) if (caze.name == "loop_refused" || caze.name == "recursive_call_refused") {
        Live refused{Lease(engine, setup, caze), caze, 96, 2, false};
        Need(!queue.Capture(refused.source, VideoCore::ActualWriter) && !queue.LastCaptureAtCapacity(), "effect graph refusal lost");
    }
    // CodexAstraLocal: A carried read must not short-circuit later effect proof;
    // changing only Boolean control must invalidate the retained proof decision.
    {
        using namespace CarryChallenge;
        Builder builder("carry_before_conditional_emit");
        builder.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, Src::MakeTemporary(0), Src::MakeInput(0));
        builder.Flow(Op::IFU, 4, 1); builder.Flow(Op::EMIT); builder.Flow(Op::NOP); builder.Output();
        for (u32 n = 0; n < 35; ++n)
            builder.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15, Src::MakeTemporary(0), Src::MakeInput(0));
        builder.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeTemporary(0));
        auto effect = builder.Finish();
        Live probe{Lease(engine, setup, effect), effect, 96, 1, false};
        auto accepted = queue.Capture(probe.source, VideoCore::ActualWriter);
        Need(bool(accepted), "untaken external effect refused"); accepted.reset();
        probe.source.uniforms.b[0] = true;
        Need(!queue.Capture(probe.source, VideoCore::ActualWriter), "late external effect escaped proof");
        probe.source.uniforms.b[0] = false;
        Need(bool(queue.Capture(probe.source, VideoCore::ActualWriter)), "Boolean proof reuse changed safe branch");
    }
    // Final ticket survives executor destruction; coordinator must drain first.
    std::shared_ptr<CpuDrawPacket> retained;
    { CpuDrawExecutor short_lived{4}; retained = short_lived.Capture(live.source, VideoCore::ActualWriter);
      Need(short_lived.Submit(retained), "destruction fixture submission"); }
    Need(!WaitTerminal(*retained, "executor destructor lost pending work"), "destructor ticket failed");
}

// CodexAstraLocal: Force both an active wave and later pending wave through
// coordinator failure. The actual post-drain Wait has a fixture deadline, so a
// missing publication produces its own diagnostic, not a generic process timeout.
void Failures(const std::vector<CarryChallenge::Case>& programs) {
    Shader::JitEngine engine; ShaderSetup setup;
    Live live{Lease(engine, setup, programs[0]), programs[0], 126, 5, true};
    {
        CpuDrawExecutor queue{4};
        PacketProbe::fail_packet = true;
        auto ticket = queue.Capture(live.source, VideoCore::ActualWriter);
        Need(queue.Submit(ticket), "failure packet publish"); queue.Drain();
        const bool typed = WaitTerminal(*ticket, "packet failure lost completion");
        Need(typed && queue.GetStatistics().failed == 1, "packet failure not terminal typed");
    }
    Gate(false); CpuDrawExecutor queue{4, 8, 1 << 20, Start};
    struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release_gate;
    std::array<std::shared_ptr<CpuDrawPacket>, 4> packets;
    for (u32 n = 0; n < 2; ++n) {
        packets[n] = queue.Capture(live.source, VideoCore::ActualWriter);
        Need(queue.Submit(packets[n]), "active failure fixture");
    }
    PacketProbe::fail_wave = true; Gate(true);
    {
        std::unique_lock lock{PacketProbe::mutex};
        Need(PacketProbe::cv.wait_for(lock, std::chrono::seconds{10}, [] { return PacketProbe::entered; }),
             "active wave fault seam did not execute");
    }
    for (u32 n = 2; n < 4; ++n) {
        packets[n] = queue.Capture(live.source, VideoCore::ActualWriter);
        Need(queue.Submit(packets[n]), "pending failure fixture");
    }
    { std::lock_guard lock{PacketProbe::mutex}; PacketProbe::release = true; }
    PacketProbe::cv.notify_all(); bool terminal{};
    try { queue.Drain(); } catch (const VideoCore::ShaderRecoveryError&) { terminal = true; }
    Need(terminal, "coordinator failure not typed");
    for (u32 n = 0; n < 4; ++n) {
        const bool typed = WaitTerminal(*packets[n], n < 2 ? "active wave lost completion" : "pending wave lost completion");
        Need(typed, "canceled ticket did not fail");
    }
    Need(queue.GetStatistics().completed == 4 && queue.GetStatistics().failed == 4,
         "terminal population accounting");
    PacketProbe::fail_wave = false;
}

// CodexAstraLocal: Work guards are separate from correctness/effect guards.
// Pin both threshold edges, FIFO eviction semantics and sparse/copy inflation.
void WorkFloor(const std::vector<CarryChallenge::Case>& programs) {
    using namespace CarryChallenge;
    Shader::JitEngine engine; ShaderSetup setup; CpuDrawExecutor queue{4};
    for (u32 arithmetic : {34U,35U}) {
        Builder b("floor");
        for (u32 n=0;n<arithmetic;++n)
            b.Arithmetic(Op::ADD,Dst::MakeTemporary(0),15,
                n ? Src::MakeTemporary(0) : Src::MakeInput(0),Src::MakeInput(0));
        b.Arithmetic(Op::MOV,Dst::MakeOutput(0),15,Src::MakeTemporary(0));
        // CodexAstraLocal: The rejected 34-instruction case must independently
        // pay the work threshold, so removing the arithmetic floor is detectable.
        const auto c=b.Finish(); Live live{Lease(engine,setup,c),c,arithmetic==34?102U:96U,1,false,3};
        const auto packet=queue.Capture(live.source,VideoCore::ActualWriter);
        Need(bool(packet)==(arithmetic==35),"arithmetic floor boundary");
    }
    Live live{Lease(engine,setup,programs[0]),programs[0],192,1,true,3};
    for(u32 domain:{63U,64U,65U,96U}) {
        for(u32 n=0;n<live.indices.size();++n)live.indices[n]=n%domain;
        const auto packet=queue.Capture(live.source,VideoCore::ActualWriter);
        Need(bool(packet)==(domain>64),"FIFO miss floor boundary");
    }
    auto source=live.source; source.indexed=false; source.base_vertex=0;
    source.count=93;
    // CodexAstraLocal: Below96 standalone work must stay synchronous even for
    // expensive shaders; the removed expansion cannot be enabled by any policy.
    Need(!queue.Capture(source,VideoCore::ActualWriter),"conservative seed admitted93");
    source.count=96;
    Need(bool(queue.Capture(source,VideoCore::ActualWriter)),"miss floor rejected96");
    source.available_attributes=9; source.shader.max_input_attribute_index.Assign(8);
    for(u32 n=0;n<9;++n)source.attributes[n]={n*8,70,n==8?3U:4U,PipelineRegs::VertexAttributeFormat::SHORT,false};
    Need(bool(queue.Capture(source,VideoCore::ActualWriter)),"raw copy floor rejected70");
    source.available_attributes=10; source.shader.max_input_attribute_index.Assign(9);
    for(u32 n=0;n<9;++n)source.attributes[n].stride=71;
    source.attributes[9]={70,71,1,PipelineRegs::VertexAttributeFormat::UBYTE,false};
    Need(!queue.Capture(source,VideoCore::ActualWriter)&&!queue.LastCaptureAtCapacity(),"raw copy floor accepted71");
    // CodexAstraLocal: Conservative default adds the93miss standalone refusal;
    // that draw remains executable through the synchronous path.
    Need(queue.GetStatistics().work_refused==5,"work refusal accounting");

    // A long branch cannot inflate a guaranteed work floor; a Boolean snapshot
    // fixes the choice, while per-invocation condition branches retain minimum0.
    for (const auto op : {Op::IFC,Op::IFU}) {
        Builder b("short_path"); b.Flow(op,36,1);
        for(u32 n=0;n<35;++n)b.Arithmetic(Op::ADD,Dst::MakeTemporary(0),15,Src::MakeInput(0),Src::MakeInput(1));
        b.Output(); const auto c=b.Finish();
        for(u16 booleans:{0U,1U}) {
            ParallelVertexWork work{999,true};
            const auto plain=AnalyzeParallelVertex(c.code,c.swizzles,c.entry,booleans,c.output_mask);
            const auto actual=AnalyzeParallelVertex(c.code,c.swizzles,c.entry,booleans,c.output_mask,
                ParallelVertexContract::FullArithmeticReads,&work);
            Need(plain.status==actual.status&&plain.pc==actual.pc&&plain.nodes==actual.nodes&&
                plain.reg==actual.reg&&plain.lanes==actual.lanes,"default analysis changed");
            Need(work.valid&&work.minimum_arithmetic==(op==Op::IFU&&booleans?35U:0U),"shortest arithmetic path");
        }
    }
    Builder call("repeated_call");call.Flow(Op::CALL,4,2);call.Flow(Op::CALL,4,2);call.Flow(Op::END);call.Flow(Op::NOP);
    for(u32 n=0;n<2;++n)call.Arithmetic(Op::ADD,Dst::MakeTemporary(0),15,Src::MakeTemporary(0),Src::MakeInput(0));
    const auto repeated=call.Finish();ParallelVertexWork work;
    AnalyzeParallelVertex(repeated.code,repeated.swizzles,0,0,0,ParallelVertexContract::FullArithmeticReads,&work);
    Need(work.valid&&work.minimum_arithmetic==4,"repeated call arithmetic floor");
    auto late=repeated;late.code[0]=nihstro::Instruction{static_cast<u32>(Op::EMIT)<<26}.hex;
    AnalyzeParallelVertex(late.code,late.swizzles,0,0,0,ParallelVertexContract::FullArithmeticReads,&work);
    Need(!work.valid&&work.minimum_arithmetic==0,"refusal reused stale work");
}

// CodexAstraLocal: Real immutable JIT packets exercise pending-only grouping.
// The fixture pauses coordinator entry, not production admission, so the exact
// seeded group and the loss of that opportunity are deterministic and bounded.
CarryChallenge::Case WorkProgram(u32 arithmetic) {
    using namespace CarryChallenge;
    Builder b("group_work");
    for (u32 n = 0; n < arithmetic; ++n)
        b.Arithmetic(Op::ADD, Dst::MakeTemporary(0), 15,
                     n ? Src::MakeTemporary(0) : Src::MakeInput(0), Src::MakeInput(0));
    b.Arithmetic(Op::MOV, Dst::MakeOutput(0), 15, Src::MakeTemporary(0));
    return b.Finish();
}
void Groups(const std::vector<CarryChallenge::Case>& programs) {
    Shader::JitEngine engine; ShaderSetup setup;
    const auto heavy = WorkProgram(140), floor = WorkProgram(35);
    const auto heavy_lease = Lease(engine, setup, heavy);
    const auto floor_lease = Lease(engine, setup, floor);
    const auto carry_lease = Lease(engine, setup, programs[1]);
    const auto environment = Env(0);
    {
        Gate(false); CpuDrawExecutor queue{4, 8, 8 * 1024 * 1024, Start};
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release;
        // CodexAstraLocal: Assert actual admission/publication behavior instead
        // of the obsolete production advisory mirror; all functional guards remain.
        Need(!queue.AllowsShaderWork(floor_lease, 0, 93), "unseeded small admission");
        auto stats = queue.GetStatistics();
        Need(stats.captures == 1 && stats.refused == 1 && stats.early_refused == 1,
             "early refusal counted twice");
        Need(queue.AllowsShaderWork(heavy_lease, 0, 255), "old seed early refusal");
        Need(queue.GetStatistics().captures == 1, "successful policy counted capture");
        { std::lock_guard lock{flags_mutex}; flag_count = 0; }
        std::vector<std::shared_ptr<CpuDrawPacket>> packets;
        std::vector<Expected> expected;
        for (u32 n = 0; n < 4; ++n) {
            const u32 sizes[]{255, 12, 18, 90};
            Live live{n ? carry_lease : heavy_lease, n ? programs[1] : heavy,
                      sizes[n], n + 1, false};
            environment.Apply(); expected.push_back(Serial(live)); environment.Apply();
            Need(queue.AllowsShaderWork(live.source.shader_lease, 0, sizes[n]),
                 "seeded small early refusal");
            auto packet = queue.Capture(live.source, ObservedWriter);
            Need(bool(packet) && queue.Submit(packet), "seeded small publication refused");
            packets.push_back(packet); live.Mutate();
        }
        CpuDrawPacket::Completion result;
        Need(!packets[0]->TryCompletedResult(result), "pending result reported ready");
        Live wide{carry_lease, programs[1], 18, 7, false};
        wide.source.base_vertex = 0; wide.source.available_attributes = 10;
        wide.source.shader.max_input_attribute_index.Assign(9);
        for (u32 n = 0; n < 9; ++n)
            wide.source.attributes[n] = {n * 8, 71, n == 8 ? 3U : 4U,
                                         PipelineRegs::VertexAttributeFormat::SHORT, false};
        wide.source.attributes[9] = {70, 71, 1, PipelineRegs::VertexAttributeFormat::UBYTE, false};
        Need(!queue.Capture(wide.source, VideoCore::ActualWriter), "group copy bound escaped");
        Need(queue.GetStatistics().copy_refused == 1, "group copy refusal accounting");
        Gate(true); queue.Drain();
        for (u32 n = 0; n < packets.size(); ++n) Equal(*packets[n], expected[n]);
        stats = queue.GetStatistics();
        Need(stats.seed_submitted == 1 && stats.group_candidates == 3 && stats.group_submitted == 3 &&
             stats.group_lost == 0 && stats.grouped_waves == 1 && stats.maximum_wave == 4,
             "group admission accounting");
        Need(stats.work_grains == 2 && stats.coordinator_packets + stats.auxiliary_packets == 4,
             "weighted grain accounting");
        packets.clear(); Need(queue.GetStatistics().resident_packets == 0, "group credit leaked");
    }
    {
        Gate(false); CpuDrawExecutor queue{4, 8, 8 * 1024 * 1024, Start};
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release;
        Live seed{floor_lease, floor, 96, 1, false};
        auto packet = queue.Capture(seed.source, VideoCore::ActualWriter);
        Need(packet && queue.Submit(packet), "exact baseline seed publication");
        Need(!queue.AllowsShaderWork(floor_lease, 0, 3), "aggregate work charge omitted");
        Live indexed{floor_lease, floor, 192, 1, true};
        for (u32 n = 0; n < indexed.indices.size(); ++n) indexed.indices[n] = n % 3;
        Need(queue.AllowsShaderWork(floor_lease, 0, 192), "upper-bound policy required raw indices");
        Need(!queue.Capture(indexed.source, VideoCore::ActualWriter), "group used inputs instead of misses");
        Gate(true); queue.Drain();
    }
    {
        Gate(false); CpuDrawExecutor queue{4, 8, 8 * 1024 * 1024, Start};
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release;
        Live seed{heavy_lease, heavy, 255, 1, false};
        auto packet = queue.Capture(seed.source, VideoCore::ActualWriter);
        Need(packet && queue.Submit(packet), "race seed publication");
        Live small{carry_lease, programs[1], 18, 9, false};
        auto canceled = queue.Capture(small.source, VideoCore::ActualWriter);
        Need(bool(canceled), "race small capture");
        Gate(true); queue.Drain();
        Need(!queue.Submit(canceled), "lost seed published small packet");
        CpuDrawPacket::Completion result;
        Need(!canceled->TryCompletedResult(result), "unpublished lost group reported ready");
        Need(queue.GetStatistics().group_lost == 1 && queue.GetStatistics().group_submitted == 0,
             "lost group accounting");
        canceled.reset(); packet.reset();
        Need(queue.GetStatistics().resident_packets == 0, "lost group retained credit");
    }
    {
        // CodexAstraLocal: Exhaust capacity reservations without executing any
        // helper. No lane means synchronous publication, with no queue deadlock.
        auto& budget = Common::Uberhar::AuxiliaryBudget::Global();
        std::vector<Common::Uberhar::AuxiliaryBudget::ProgressLane> lanes;
        for (;;) { auto lane = budget.ReserveProgress(); if (!lane) break; lanes.push_back(std::move(lane)); }
        Gate(false); CpuDrawExecutor queue{4, 8, 8 * 1024 * 1024, Start};
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release;
        Live live{heavy_lease, heavy, 96, 4, false};
        environment.Apply(); const auto expected = Serial(live); environment.Apply();
        auto packet = queue.Capture(live.source, VideoCore::ActualWriter);
        Need(packet && queue.Submit(packet), "no-lane synchronous publication");
        CpuDrawPacket::Completion result;
        Need(packet->TryCompletedResult(result), "no-lane publication failed synchronous progress");
        const auto bytes = packet->HardwareBytes();
        Need(bytes.size() == expected.hardware.size() &&
             !std::memcmp(bytes.data(), expected.hardware.data(), bytes.size()), "no-lane hardware differs");
        ++comparisons;
        Need(queue.GetStatistics().peak_threads == 1, "no-lane async work");
    }
    // CodexAstraLocal: Retain lazy startup and exact proof reuse for worthwhile
    // standalone work. Cached negative policy creates no reservation; a same-
    // key accepted proof is reused through owned capture before lazy startup.
    {
        const auto reservations = Common::Uberhar::AuxiliaryBudget::Global().GetStatistics().reservations;
        CpuDrawExecutor queue{4};
        Need(!queue.CachedShaderWork(floor_lease,0,3).has_value(),"unknown cached policy invented");
        Need(!queue.AllowsShaderWork(floor_lease,0,3),"tiny floor policy admitted");
        const auto built=queue.GetStatistics().effect_builds;
        Need(queue.CachedShaderWork(floor_lease,0,3)==std::optional<bool>{false},"negative policy not cached");
        Need(queue.GetStatistics().effect_builds==built && queue.GetStatistics().start_requests==0 &&
             Common::Uberhar::AuxiliaryBudget::Global().GetStatistics().reservations==reservations,
             "negative policy reserved progress");
        Need(!queue.CachedShaderWork(floor_lease,1,3).has_value(),"cached Boolean identity aliased");
        Live live{heavy_lease,heavy,96,1,false};
        const auto expected=Serial(live);
        Need(queue.AllowsShaderWork(heavy_lease,0,96),"qualified standalone seed rejected");
        auto packet=queue.Capture(live.source,VideoCore::ActualWriter);
        auto before=queue.GetStatistics();
        Need(packet && before.effect_reuses==1 && before.original_seeded==0 && before.start_requests==0,
             "accepted proof or lazy capture contract");
        Need(queue.Submit(packet),"standalone seed publication"); queue.Drain();
        const auto bytes=packet->HardwareBytes();
        Need(bytes.size()==expected.hardware.size() &&
             !std::memcmp(bytes.data(),expected.hardware.data(),bytes.size()),"standalone seed hardware differs");
        ++comparisons;
        Need(queue.GetStatistics().start_requests==1 && queue.GetStatistics().original_seeded==1,
             "lazy progress startup count");
    }
    // CodexAstraLocal: The removed expansion means an exact heavy48 draw must
    // not start/reserve anything. A Boolean-mismatched accepted token falls back
    // to the actual policy, never borrowing the old proof or skipping rendering.
    {
        CpuDrawExecutor queue{4};
        Live small{heavy_lease,heavy,48,1,false};
        Need(!queue.AllowsShaderWork(heavy_lease,0,48) &&
             !queue.Capture(small.source,VideoCore::ActualWriter), "standalone miss floor ignored");
        Need(queue.GetStatistics().start_requests==0,"small standalone work started coordinator");
        Live original{heavy_lease,heavy,96,1,false};
        Need(queue.AllowsShaderWork(heavy_lease,0,96),"original policy rejected");
        original.source.uniforms.b[0] = true;
        auto packet=queue.Capture(original.source,VideoCore::ActualWriter);
        Need(packet && queue.GetStatistics().token_mismatches==1 &&
             queue.GetStatistics().effect_reuses==0,"stale Boolean token reused");
        Need(queue.Submit(packet),"original seed not published"); queue.Drain();
        const auto expected=Serial(original); const auto bytes=packet->HardwareBytes();
        Need(bytes.size()==expected.hardware.size() &&
             !std::memcmp(bytes.data(),expected.hardware.data(),bytes.size()),"token fallback output differs");
        ++comparisons;
        const auto stats=queue.GetStatistics();
        Need(stats.original_seeded==1 && stats.group_submitted==0,
             "seed classes not exclusive");
    }
    // CodexAstraLocal: Diagnostics independently prices actual cached lookup
    // and eligibility. OFF keeps admission/owner counters but reads no clocks.
    for(bool diagnostics : {false,true}) {
        CpuDrawExecutor queue{4,64,8*1024*1024,nullptr,{.sampled_costs=diagnostics}};
        Need(!queue.AllowsShaderWork(floor_lease,0,3),"diagnostic gate admitted tiny work");
        for(unsigned i=0;i<2048;++i)
            Need(queue.CachedShaderWork(floor_lease,0,3)==std::optional<bool>{false},
                 "diagnostic control changed eligibility");
        const auto stats=queue.GetStatistics();
        Need(stats.cached_checks==2048 && stats.start_requests==0 &&
             stats.lookup_samples==(diagnostics?2U:0U) &&
             stats.eligibility_samples==(diagnostics?2U:0U),"feature cost sample routing");
    }
    // CodexAstraLocal: Aggregate work alone cannot admit an unpartitionable
    // cheap tail. Exercise both the member bound and per-member dispatch charge.
    for (u32 member_size : {12U,30U}) {
        Gate(false); CpuDrawExecutor queue{4,8,8*1024*1024,Start};
        struct ReleaseGate { ~ReleaseGate() { Gate(true); } } release;
        Live seed{floor_lease,floor,96,1,false};
        auto first=queue.Capture(seed.source,VideoCore::ActualWriter);
        Need(first&&queue.Submit(first),"partition seed publication");
        std::vector<std::shared_ptr<CpuDrawPacket>> packets{first};
        for (unsigned i=0;i<3;++i) {
            Live member{floor_lease,floor,member_size,i+1,false};
            auto packet=queue.Capture(member.source,VideoCore::ActualWriter);
            Need(packet&&queue.Submit(packet),"valid four-member partition refused"); packets.push_back(packet);
        }
        Live tail{floor_lease,floor,member_size,7,false};
        Need(!queue.Capture(tail.source,VideoCore::ActualWriter),
             member_size==12 ? "oversized grain admitted" : "member charge omitted");
        Gate(true); queue.Drain();
        Need(queue.GetStatistics().work_grains==1,"partition dispatch disagreed with admission");
    }
}

// CodexAstraLocal: Read the actual PICA ordered-prefix implementation using real
// packets in deliberately different readiness orders. Unpublished later work
// represents an unavailable result; the reader must not drain or skip it.
void ReadyPrefix(const std::vector<CarryChallenge::Case>& programs) {
    Shader::JitEngine engine; ShaderSetup setup;
    auto lease = Lease(engine, setup, programs[0]);
    Gate(true); ResetOwner owner;
    auto& state = *owner.deferred_vertices;
    Live a{lease, programs[0], 96, 1, false}, b{lease, programs[0], 96, 2, false};
    const auto expected_a = Serial(a), expected_b = Serial(b);
    auto first = state.executor.Capture(a.source, VideoCore::ActualWriter);
    auto second = state.executor.Capture(b.source, VideoCore::ActualWriter);
    Need(first && second && state.executor.Submit(second), "prefix readiness setup");
    second->Wait(); state.pending = {first, second};
    owner.ReconcileDeferredVertices(false);
    Need(state.completed == 0 && state.pending.size() == 2 && state.synchronous_boundaries == 0,
         "nonready predecessor skipped");
    Need(state.executor.Submit(first), "prefix first publication"); first->Wait();
    owner.ReconcileDeferredVertices(false);
    PrimitiveAssembler expected;
    expected.AdoptCompletedTriangleTail(expected_b.pair);
    Need(state.completed == 2 && state.ready_prefix_packets == 2 && state.pending.empty() &&
         SavedAssembly(owner.primitive_assembler) == SavedAssembly(expected), "ready prefix order differs");
    auto third = state.executor.Capture(a.source, VideoCore::ActualWriter);
    auto fourth = state.executor.Capture(b.source, VideoCore::ActualWriter);
    Need(third && fourth && state.executor.Submit(third), "prefix partial setup"); third->Wait();
    state.pending = {third, fourth};
    owner.ReconcileDeferredVertices(false);
    expected.AdoptCompletedTriangleTail(expected_a.pair);
    Need(state.completed == 3 && state.pending.size() == 1 && state.pending[0] == fourth &&
         state.synchronous_boundaries == 0 && SavedAssembly(owner.primitive_assembler) == SavedAssembly(expected),
         "ready prefix waited for later work");
    Need(state.executor.Submit(fourth), "prefix final publication"); fourth->Wait();
    owner.ReconcileDeferredVertices(false);
    expected.AdoptCompletedTriangleTail(expected_b.pair);
    const auto before_error = SavedAssembly(owner.primitive_assembler);
    auto failed = state.executor.Capture(a.source, VideoCore::ActualWriter);
    auto later = state.executor.Capture(b.source, VideoCore::ActualWriter);
    PacketProbe::fail_packet = true;
    Need(failed && later && state.executor.Submit(failed), "prefix error publication");
    Need(WaitTerminal(*failed, "prefix error incomplete"), "prefix error not typed");
    Need(state.executor.Submit(later), "prefix later publication"); later->Wait();
    state.pending = {failed, later};
    for (u32 retry = 0; retry < 2; ++retry) {
        bool terminal{};
        try { owner.ReconcileDeferredVertices(false); }
        catch (const VideoCore::ShaderRecoveryError&) { terminal = true; }
        Need(terminal && state.completed == 4 && state.pending.size() == 2 && state.reconciled == 0 &&
             SavedAssembly(owner.primitive_assembler) == before_error, "ready error skipped or replayed");
    }
    state.executor.Drain();
}

int main(int argc, char** argv) try {
    const auto original = Environment::Capture(); const auto programs = Programs();
    const std::string selected = argc > 1 ? argv[1] : "all";
    if (selected == "all" || selected == "normal") Normal(programs);
    if (selected == "all" || selected == "bounds") Boundaries(programs);
    if (selected == "all" || selected == "failures") Failures(programs);
    if (selected == "all" || selected == "work" || selected == "features") WorkFloor(programs);
    if (selected == "all" || selected == "resets" || selected == "groups" || selected == "features") Resets(programs);
    if (selected == "all" || selected == "groups" || selected == "features") { Groups(programs); ReadyPrefix(programs); }
    original.Apply();
    std::cout << Json{{"passed", true}, {"checks", checks}, {"comparisons", comparisons},
        {"programs", programs.size()}, {"fp_modes", ModeCount},
        {"scope", "actual async packet, JIT/input/FIFO/88-byte writer; live Vulkan ordering separately required"}}.dump() << '\n';
    return 0;
} catch (const std::exception& e) {
    Gate(true); std::cout << Json{{"passed", false}, {"checks", checks}, {"error", e.what()}}.dump() << '\n'; return 1;
}
