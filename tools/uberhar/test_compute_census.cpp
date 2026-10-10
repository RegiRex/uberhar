// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Execute real census/report/admission bodies with synthetic
// registers and recording resource/log endpoints; this is not GPU execution.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fmt/format.h>
#include "common/vector_math.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect.h"
#include "video_core/renderer_vulkan/uberhar_compute_census.h"
#include "policy.inc"
#include "video_core/pica/compute_assembly.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/shader_recovery_error.h"

struct LogEntry { Common::Log::Delivery delivery; std::string text; };
std::vector<LogEntry> logs;
int throw_after = -1;
// CodexAstraLocal: A throwing output endpoint verifies that optional reporting
// cannot replay consumed counters or escape back into rendering.
template<class... A> void Log(Common::Log::Delivery delivery, fmt::format_string<A...> f, A&&... a) {
    if (throw_after == 0) throw std::bad_alloc{};
    if (throw_after > 0) --throw_after;
    logs.push_back({delivery, fmt::format(f, std::forward<A>(a)...)});
}
#undef LOG_INFO_WITH_DELIVERY
#undef LOG_INFO
#define LOG_INFO_WITH_DELIVERY(category, delivery, ...) Log(delivery, __VA_ARGS__)
#define LOG_INFO(category, ...) Log(Common::Log::Delivery::Reliable, __VA_ARGS__)
#define MICROPROFILE_SCOPE(name) ((void)0)
unsigned checks{};
void Check(bool yes, const char* reason) {
    ++checks;
    if (!yes) throw std::runtime_error(reason);
}
#undef ASSERT
// CodexAstraLocal: Source-extracted no-target retention assertions must fail the
// fixture visibly; they do not invoke the emulator's process-level abort path.
#define ASSERT(condition) Check(condition, "source retention size assertion")
using Fields = std::map<std::string, std::string>;
Fields Parse(const std::string& s) {
    Fields fields;
    std::istringstream stream{s}; std::string item;
    while (stream >> item) {
        const auto pos = item.find('=');
        if (pos != std::string::npos)
            Check(fields.emplace(item.substr(0, pos), item.substr(pos + 1)).second, "duplicate output field");
    }
    return fields;
}
u64 Number(const Fields& f, const char* key) { return std::stoull(f.at(key)); }

// CodexAstraLocal: Only resource handles and renderer commands are modeled;
// state masks, rectangle geometry, observation, and reporting remain production.
namespace vk { enum class Format { eR8G8B8A8Unorm, Other }; }
namespace VideoCore { enum class PixelFormat { RGBA8, Other }; }
namespace Vulkan {
enum class SurfaceType { Color, Depth };
struct HardwareVertex { Common::Vec4f position, color; };
struct Surface {
    struct { vk::Format native{vk::Format::eR8G8B8A8Unorm}; bool storage_support{true}; } traits;
    int image{7}; int Image() const { return image; }
};
struct Framebuffer {
    // CodexAstraLocal: A null handle exercises the real early Draw exit without
    // constructing a Vulkan framebuffer or changing its census denominator.
    bool handle{true};
    bool Handle() const { return handle; }
    unsigned color_id{1}, color_level{};
    VideoCore::PixelFormat format{VideoCore::PixelFormat::RGBA8};
    VideoCore::PixelFormat Format(SurfaceType) const { return format; }
    std::array<int, 1> Images() const { return {7}; }
};
class ComputeRectRenderer {
public:
    Settings::UberharTestMode mode{Settings::UberharTestMode::Automatic}; bool selected{};
    u64 considered{}, unsupported{}, geometry_rejected{}, format_rejected{}, eligible{},
        native_draws{}, compute_draws{}, compute_pixels{};
    std::array<u64, 11> rejected_state{};
    std::array<u64, 2> measured_draws{};
    std::array<double, 2> measured_ns{};
    unsigned reserves{}, begins{}, ends{};
    bool Choose(const ComputeRectPacket&) { ++eligible; return selected; }
    int ReserveSample(bool, u64) { ++reserves; return 0; }
    void BeginSample(int) { ++begins; }
    void EndSample(int) { ++ends; }
    void Draw(Surface&, const ComputeRectPacket& p) { ++compute_draws; compute_pixels += p.PixelCount(); }
    void ReportCensus(std::chrono::steady_clock::time_point now, bool final = false) noexcept;
    void Report();
    bool AllowsExpandedRectangles() const;
#include "members.inc"
#include "observe.inc"
};
#include "report.inc"
struct Fixture {
    // CodexAstraLocal: Mode is selected once for this test owner, independently
    // of compute-owner availability; ordinary fallback remains the default.
    explicit Fixture(Settings::UberharTestMode mode = Settings::UberharTestMode::Automatic)
        : strict_compute{Settings::RequiresComputeOnly(mode)} { owned.mode = mode; }
    bool strict_compute;
#include "strict_stats.inc"
    StrictComputeStats strict_compute_stats{};
    void ReportStrictCompute() const;
    ComputeRectRenderer owned;
    ComputeRectRenderer* compute_rect{&owned};
    bool accelerate{};
    Pica::RegsInternal regs{};
    std::vector<HardwareVertex> vertex_batch;
    // CodexAstraLocal: The census consumes packet metadata without waiting for
    // hardware output. This endpoint counts accidental payload reads; separate
    // retention/queue gates cover the real 88B transport and asynchronous work.
    struct Packet {
        std::vector<HardwareVertex> hardware;
        mutable unsigned reads{};
        u32 VertexCount() const { return static_cast<u32>(hardware.size()); }
        std::span<const u8> HardwareBytes() const {
            ++reads;
            return {reinterpret_cast<const u8*>(hardware.data()), hardware.size() * sizeof(HardwareVertex)};
        }
    } packet;
    Packet* deferred{};
    bool vertex_capture{}, ready_vertex_attempt{};
    struct { bool ready{}; } cpu_bridge;
    struct { bool enabled{true}; bool HasWorkerThread() const { return enabled; } } scheduler;
    struct { bool enabled{true}; bool CanDeferUpload() const { return enabled; } } stream_buffer;
    using DeferredHardwareWriter = void (*)();
    static void WriteDeferredTriangles() {}
#include "deferred_preflight.inc"
    Framebuffer target;
    // CodexAstraLocal: Record the framebuffer boundary calls and the terminal
    // graphics sentinel. State masks and the entire pre-graphics Draw are real.
    struct FramebufferHelper {
        Vulkan::Framebuffer* target{};
        unsigned* cancellations{};
        struct View { s32 x{}, y{}, width{64}, height{32}; };
        struct Rect { s32 left{}, bottom{}, right{64}, top{32}; };
        Vulkan::Framebuffer* Framebuffer() const { return target; }
        void CancelInvalidation() const { ++*cancellations; }
        View Viewport() const { return {}; }
        Rect DrawRect() const { return {}; }
    };
    struct {
        Surface surface;
        Vulkan::Framebuffer* target{};
        unsigned cancellations{};
        Surface& GetSurface(unsigned) { return surface; }
        FramebufferHelper GetFramebufferSurfaces(bool, bool) { return {target, &cancellations}; }
    } res_cache;
    struct {
        struct { struct { VideoCore::PixelFormat color{}, depth{}; } attachments;
                 struct { bool stencil_test_enable{}; } depth_stencil; } state;
        bool GetFinalColorWriteMask(int) const { return true; }
        bool IsDepthWriteEnabled() const { return false; }
    } pipeline_info;
    int instance{};
    unsigned graphics_fallthrough{}, sync_calls{}, nonempty_calls{};
    void SyncDrawState() { ++sync_calls; }
    struct { unsigned ended{}; void EndRendering() { ++ended; } } renderpass_cache;
    void EmptyBatch() {
#include "empty.inc"
        ++nonempty_calls;
    }
    bool Route() {
        res_cache.target = &target;
#include "admission.inc"
        ++graphics_fallthrough;
        return false;
    }
};
#include "strict_report.inc"

// CodexAstraLocal: Only GPU execution/resources are recording endpoints here.
// The actual DrawComputeBatch body decides ownership, whole-batch commit,
// terminal propagation and counters; the real Vulkan owner check is separate.
struct CaptureEndpoint { unsigned calls{}; bool fail{}; };
struct CapturedInput {};
template<class Flush>
CapturedInput CaptureComputeVertexInput(CaptureEndpoint& memory, int, bool, bool,
    const Pica::ComputeAssemblyState&, Flush&& flush, const Pica::AttributeBuffer*) {
    ++memory.calls;
    if(memory.fail) throw VideoCore::ShaderRecoveryError("capture refusal");
    flush(0x1000,16); return {};
}
struct ComputeVertexProducer {
    struct Batch { unsigned count; Pica::ComputeAssemblyResult assembly; };
    unsigned count{6}, calls{}, marks{}; bool fail{};
    Batch Produce(const CapturedInput&, bool) {
        ++calls;
        if(fail) throw VideoCore::ShaderRecoveryError("producer refusal");
        Batch out{count,{}};out.assembly.state.words[0]=91;out.assembly.written_mask=1;return out;
    }
    void MarkRasterUse(){++marks;}
};
struct StrictFixture {
    bool strict_compute{true},compute_raster{true},accurate_mul{true};
    ComputeVertexProducer producer;
    ComputeVertexProducer* compute_vertices{&producer};
    CaptureEndpoint memory;int pica{};
    std::vector<HardwareVertex> vertex_batch;
    struct {unsigned flushes{};void FlushRegion(PAddr,u32){++flushes;}}res_cache;
    struct {unsigned ended{};void EndRendering(){++ended;}}renderpass_cache;
    struct {struct Profile {unsigned vk_disable_spirv_optimizer{1};};Profile ShaderProfile(){return {};}}pipeline_cache;
#include "strict_stats.inc"
    StrictComputeStats strict_compute_stats;
    unsigned raster_calls{};bool consumer_failure{};
    bool Draw(bool,bool,std::nullptr_t,const ComputeVertexProducer::Batch*) {
        if(consumer_failure)throw VideoCore::ShaderRecoveryError("consumer refusal");
        ++raster_calls;return true;
    }
    bool DrawComputeBatch(bool,const Pica::ComputeAssemblyState&,Pica::ComputeAssemblyResult&,
                          const Pica::AttributeBuffer* = nullptr);
    void ReportStrictCompute() const;
};
#include "strict_batch.inc"
#include "strict_owner_report.inc"
} // namespace Vulkan

// CodexAstraLocal: Build every exact joint reason mask independently from
// production bit positions, retaining the inherited complete geometry path.
Pica::RegsInternal Registers(u32 mask) {
    using FB = Pica::FramebufferRegs; using R = Pica::RasterizerRegs;
    using T = Pica::TexturingRegs::TevStageConfig;
    Pica::RegsInternal r{};
    r.framebuffer.framebuffer.allow_color_write.Assign(!(mask & 2));
    r.framebuffer.output_merger.depth_color_mask = 0xf00;
    r.framebuffer.output_merger.fragment_operation_mode.Assign(
        mask & 1 ? FB::FragmentOperationMode::Shadow : FB::FragmentOperationMode::Default);
    r.framebuffer.output_merger.depth_test_enable.Assign(bool(mask & 4));
    r.framebuffer.output_merger.depth_write_enable.Assign(bool(mask & 8));
    r.framebuffer.output_merger.stencil_test.enable.Assign(bool(mask & 16));
    r.framebuffer.output_merger.alpha_test.enable.Assign(bool(mask & 32));
    r.rasterizer.clip_enable.Assign(bool(mask & 64));
    r.rasterizer.scissor_test.mode.Assign(mask & 128 ? R::ScissorMode::Include : R::ScissorMode::Disabled);
    r.rasterizer.cull_mode.Assign(mask & 256 ? R::CullMode::KeepClockWise : R::CullMode::KeepAll);
    r.texturing.fog_mode.Assign(mask & 512 ? Pica::TexturingRegs::FogMode::Fog : Pica::TexturingRegs::FogMode::None);
    r.framebuffer.output_merger.logic_op.Assign(mask & 1024 ? FB::LogicOp::Xor : FB::LogicOp::Copy);
    T t{}; t.color_source1.Assign(T::Source::PrimaryColor); t.alpha_source1.Assign(T::Source::PrimaryColor);
    r.texturing.tev_stage0 = t;
    t.color_source1.Assign(T::Source::Previous); t.alpha_source1.Assign(T::Source::Previous);
    r.texturing.tev_stage1 = r.texturing.tev_stage2 = r.texturing.tev_stage3 = r.texturing.tev_stage4 = r.texturing.tev_stage5 = t;
    return r;
}
std::vector<Vulkan::HardwareVertex> Vertices(std::size_t count) {
    using V = Vulkan::HardwareVertex;
    const Common::Vec4f c{1, 0, 1, 1};
    std::vector<V> v{{{-1,-1,-.5f,1},c},{{1,-1,-.5f,1},c},{{1,1,-.5f,1},c},
                     {{-1,-1,-.5f,1},c},{{1,1,-.5f,1},c},{{-1,1,-.5f,1},c}};
    v.resize(count); return v;
}

// CodexAstraLocal: Independent exact population sums cover every joint mask,
// deterministic truncation, consume/reset, invalid masks and saturation.
void TestBins() {
    using C = Vulkan::ComputeStateCensus;
    constexpr std::array<u32, 2> allowed{0x180, 0x1a2};
    C census;
    std::array<C::Counts, C::Bins> oracle{};
    C::Counts total{}; std::array<u64, 11> marginal{}; std::array<u64, 2> candidates{};
    std::vector<C::Group> ranked;
    for (u32 mask = 0; mask < C::Bins; ++mask) {
        const u64 six = 1 + mask % 5, other = mask % 3;
        for (u64 n = 0; n < six; ++n) census.Record(mask, 6);
        for (u64 n = 0; n < other; ++n) census.Record(mask, n ? 0 : 12);
        oracle[mask] = {six, other}; total.six += six; total.other += other;
        for (unsigned bit = 0; bit < 11; ++bit) if (mask & (1U << bit)) marginal[bit] += six + other;
        for (unsigned i = 0; i < 2; ++i) if (!(mask & ~allowed[i])) candidates[i] += six;
        ranked.push_back({mask, {six, other}, six + other});
    }
    const auto result = census.Consume(allowed);
    Check(result.total.six == total.six && result.total.other == total.other, "joint populations include mask zero");
    Check(result.admitted.six == oracle[0].six && result.admitted.other == oracle[0].other, "admitted population");
    Check(result.draws == total.six + total.other && result.rejected == result.draws - result.admitted_draws, "mask totals conserve");
    Check(result.marginal == marginal && result.state_only_six == candidates, "full-bank marginals and candidate families");
    Check(result.distinct == C::Bins && !result.overflow && result.groups == 4, "bounded groups");
    std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.draws != b.draws ? a.draws > b.draws : a.mask < b.mask; });
    for (unsigned i = 0; i < 4; ++i) Check(result.top[i].mask == ranked[i].mask && result.top[i].draws == ranked[i].draws, "deterministic ranked population");
    Check(result.ranked_draws + result.unranked_draws == result.draws && result.ranked.six + result.unranked.six == total.six, "rank censorship conserves");
    Check(census.Consume(allowed).draws == 0, "consume resets interval");
    census.Record(C::Bins, 6); census.Record(~u32{}, 3);
    const auto invalid = census.Consume(allowed);
    Check(invalid.draws == 2 && invalid.invalid_draws == 2 && invalid.groups == 0 && invalid.unranked_draws == 2, "invalid masks are explicit");
    u64 sum = std::numeric_limits<u64>::max() - 1; bool overflow{};
    C::Add(sum, 2, overflow);
    Check(overflow && sum == std::numeric_limits<u64>::max(), "addition saturates");
}

void TestAdmission() {
    using namespace Vulkan;
    // CodexAstraLocal: The full extracted admission block must preserve all
    // route counters and count six only from the actual submitted batch length.
    for (u32 mask = 0; mask < 2048; ++mask) for (auto count : {0U, 3U, 6U, 12U}) {
        Fixture f; f.regs = Registers(mask); f.vertex_batch = Vertices(count);
        Check(ComputeRectStateRejections(f.regs) == mask, "fixture independently covers exact state masks");
        Check(!f.Route() && f.owned.native_draws == 1, "ordinary rejected or unselected draw remains native");
        Check(f.owned.considered == 1 && f.owned.unsupported == bool(mask), "callsite state counters");
        const auto seen = f.owned.state_census.Consume({0x180, 0x1a2});
        Check(seen.draws == 1 && seen.total.six == (count == 6) && seen.total.other == (count != 6), "callsite observes actual batch once");
        Check(f.owned.geometry_rejected == (!mask && count != 6) && f.owned.eligible == (!mask && count == 6), "geometry remains independent of six-count evidence");
    }
    for (unsigned failure = 0; failure < 8; ++failure) {
        Fixture f; f.regs = Registers(0); f.vertex_batch = Vertices(6); f.owned.selected = true;
        if (failure == 1) f.target.color_id = 0;
        if (failure == 2) f.target.color_level = 1;
        if (failure == 3) f.target.format = VideoCore::PixelFormat::Other;
        if (failure == 4) f.res_cache.surface.traits.native = vk::Format::Other;
        if (failure == 5) f.res_cache.surface.traits.storage_support = false;
        if (failure == 6) f.res_cache.surface.image = 8;
        if (failure == 7) f.vertex_batch[0].position.w = 0;
        const bool drawn = f.Route();
        Check(drawn == !failure && f.owned.compute_draws == !failure && f.owned.native_draws == bool(failure), "format and geometry gates unchanged");
        Check(f.owned.considered == 1 && f.owned.state_census.Consume({0x180,0x1a2}).admitted_draws == 1, "mask zero does not claim geometry or format");
        Check(f.renderpass_cache.ended == !failure && f.owned.begins == !failure && f.owned.ends == !failure, "compute command boundaries unchanged");
    }
    for (bool absent : {false, true}) {
        Fixture f; f.regs = Registers(0); f.vertex_batch = Vertices(6);
        if (absent) f.compute_rect = nullptr; else f.accelerate = true;
        Check(!f.Route() && f.owned.considered == 0 && f.owned.state_census.Consume({0,0}).draws == 0, "accelerated and absent-renderer exclusion");
    }
}

// CodexAstraLocal: A full raw-mask matrix independently predicts the permanent
// state subset from the synthetic register setup. Ordinary complete vertices
// remain the reference for each deferred population; no payload wait is needed.
void TestDeferredAdmission() {
    using namespace Vulkan;
    for (u32 mask = 0; mask < 2048; ++mask) for (auto count : {0U, 3U, 6U, 12U, 95U, 96U, 255U}) {
        Fixture f; f.regs = Registers(mask); f.packet.hardware = Vertices(count);
        const bool permanent = (mask & ~(2U | 1024U)) != 0;
        const bool expected = count && count % 3 == 0 && permanent;
        Check(bool(f.PrepareDeferredVertices(count)) == expected,
              "deferred preflight requires permanent rejection");
        if (!expected) continue;
        Fixture ordinary; ordinary.regs = Registers(mask); ordinary.vertex_batch = Vertices(count);
        f.deferred = &f.packet;
        Check(!f.Route() && !ordinary.Route(), "deferred and ordinary rejected routes reach graphics");
        Check(f.owned.considered == 1 && f.owned.unsupported == 1 && f.owned.native_draws == 1 &&
              f.owned.compute_draws == 0 && f.owned.geometry_rejected == 0 &&
              f.owned.format_rejected == 0 && f.owned.eligible == 0 && f.packet.reads == 0 &&
              f.vertex_batch.empty(), "deferred rejection needs no geometry payload or wait");
        const auto seen = f.owned.state_census.Consume({0x180, 0x1a2});
        const auto reference = ordinary.owned.state_census.Consume({0x180, 0x1a2});
        Check(seen.draws == 1 && seen.total.six == (count == 6) && seen.total.other == (count != 6),
              "deferred census counts packet vertices exactly once");
        Check(seen.groups == 1 && seen.top[0].mask == mask && seen.rejected == 1 &&
              seen.marginal == reference.marginal && seen.total.six == reference.total.six &&
              seen.total.other == reference.total.other && f.owned.raw_unsupported == 1,
              "deferred census preserves exact raw mask");
    }
    // CodexAstraLocal: The preflight also rejects routes that need live output
    // or cannot safely delay the upload. These capabilities are recording inputs.
    for (unsigned failure = 0; failure < 7; ++failure) {
        Fixture f; f.regs = Registers(4);
        if (failure == 0) f.strict_compute = true;
        if (failure == 1) f.cpu_bridge.ready = true;
        if (failure == 2) f.vertex_capture = true;
        if (failure == 3) f.ready_vertex_attempt = true;
        if (failure == 4) f.vertex_batch = Vertices(3);
        if (failure == 5) f.scheduler.enabled = false;
        if (failure == 6) f.stream_buffer.enabled = false;
        Check(!f.PrepareDeferredVertices(6), "deferred route capabilities remain guarded");
    }
    for (bool no_target : {false, true}) {
        Fixture f; f.regs = Registers(4); f.packet.hardware = Vertices(6); f.deferred = &f.packet;
        if (no_target) f.target.handle = false;
        else f.compute_rect = nullptr;
        Check(f.Route() == no_target && f.owned.considered == 0 &&
              f.owned.state_census.Consume({0x180, 0x1a2}).draws == 0,
              "deferred no-target and absent-renderer remain outside census");
        Check(f.packet.reads == no_target && f.vertex_batch.size() == (no_target ? 6U : 0U),
              "only no-target retention reads deferred payload before graphics");
    }
    // CodexAstraLocal: Retain an actual report-level mixed ordinary/deferred
    // interval so raw/effective conservation is checked beyond helper snapshots.
    Fixture f; f.regs = Registers(4); f.vertex_batch = Vertices(3); f.Route();
    f.vertex_batch.clear(); f.packet.hardware = Vertices(6); f.deferred = &f.packet; f.Route();
    logs.clear(); f.owned.ReportCensus(std::chrono::steady_clock::now());
    const auto report = Parse(logs[0].text);
    Check(Number(report, "considered") == 2 && Number(report, "six") == 1 &&
          Number(report, "other") == 1 && Number(report, "unsupported") == 2 &&
          Number(report, "raw_unsupported") == 2 && report.at("conservation") == "true",
          "ordinary and deferred reports conserve one mixed interval");
}

// CodexAstraLocal: Exercise actual final/interval output including silent partial
// delivery and throwing output; window identity never implies a scene label.
void TestReports() {
    using namespace Vulkan; using namespace std::chrono;
    ComputeRectRenderer r; r.census_start = steady_clock::time_point{};
    const auto note = [&](u32 mask, unsigned count) { ++r.considered; r.unsupported += bool(mask); r.ObserveState(mask,count); };
    logs.clear();
    for (u32 mask = 0; mask < 8; ++mask) for (unsigned n = 0; n <= mask; ++n) note(mask,n & 1 ? 3 : 6);
    r.ReportCensus(steady_clock::time_point{} + seconds{30});
    Check(logs.size() == 5, "summary plus four rows bound");
    auto first = Parse(logs[0].text);
    Check(Number(first,"schema") == 2 && first.at("raw_scope") == "unmodified_state_mask" &&
          !first.contains("admitted_six"), "schema2 labels raw six-vertex evidence");
    Check(Number(first,"window") == 1 && Number(first,"considered") == 36 && first.at("conservation") == "true", "first interval retains preceding draws");
    Check(Number(first,"ranked_draws") + Number(first,"unranked_draws") == 36, "reported censoring totals");
    for (unsigned i = 1; i < 5; ++i) { auto row = Parse(logs[i].text); Check(row.at("window") == first.at("window") && Number(row,"rank") == i && logs[i].delivery == Common::Log::Delivery::Diagnostic, "group interval and optional delivery"); }
    logs.clear(); note(0,6); note(0,6); note(128,3);
    r.ReportCensus(steady_clock::time_point{} + seconds{60});
    auto second = Parse(logs[0].text);
    Check(Number(second,"considered") == 3 && Number(second,"considered_total") == 39 && Number(second,"state_six_scissor_cull") == 2 && second.at("conservation") == "true", "interval reset and complete candidate families");
    // CodexAstraLocal: Losing a summary or a detail row cannot repeat its bank;
    // accumulated lifetime marginals survive either controlled output failure.
    logs.clear(); note(4,6); throw_after = 0; r.ReportCensus(steady_clock::time_point{} + seconds{90}); throw_after = -1;
    Check(logs.empty() && r.census_log_failures == 1, "throwing summary isolated");
    r.ReportCensus(steady_clock::time_point{} + seconds{120});
    auto lost = Parse(logs.back().text);
    Check(Number(lost,"considered") == 0 && Number(lost,"prior_log_failures") == 1 && lost.at("conservation") == "true", "failed output cannot replay consumed bank");
    logs.clear(); note(2,6); note(8,3); throw_after = 2;
    r.ReportCensus(steady_clock::time_point{} + seconds{150}); throw_after = -1;
    Check(logs.size() == 2 && Number(Parse(logs[0].text),"ranked_groups") == 2 && r.census_log_failures == 2, "partial row loss remains distinguishable");
    logs.clear(); note(32,6); const auto before = r.rejected_state[5]; r.Report();
    Check(logs.size() == 4 && logs[0].text.find("state census") != std::string::npos && logs[2].text.find("compute blockers") != std::string::npos && r.rejected_state[5] == before + 1, "final partial flush precedes lifetime marginals");
    Check(logs[0].delivery == Common::Log::Delivery::Reliable && Parse(logs[0].text).at("final") == "true", "final partial is reliable and labelled");
    logs.clear(); r.Report();
    Check(Number(Parse(logs[0].text),"considered") == 0 && r.rejected_state[5] == before + 1, "repeated final cannot double-count");
    logs.clear(); note(2048,6); r.ReportCensus(steady_clock::now());
    const auto invalid = Parse(logs[0].text);
    Check(invalid.at("conservation") == "false" && Number(invalid,"invalid_mask_draws") == 1, "invalid mask forbids conservation claim");
    logs.clear(); r.ReportCensus(steady_clock::time_point{});
    Check(Parse(logs[0].text).at("clock_valid") == "false", "backward diagnostic interval is explicit");
}

// CodexAstraLocal: The same raw1026 bucket can contain an exact replacement,
// a genuine blend, and a state-admitted draw later rejected by format/geometry.
// Count the unchanged diagnostic distribution independently of actual routing.
void TestEffectiveAdmission() {
    using namespace Vulkan; using namespace std::chrono;
    using M = Settings::UberharTestMode; using FB = Pica::FramebufferRegs;
    const auto expanded = [] {
        Fixture f; f.regs = Registers(0); f.vertex_batch = Vertices(6);
        auto& om = f.regs.framebuffer.output_merger;
        om.depth_color_mask = 0x500;
        om.alphablend_enable.Assign(true);
        om.alpha_blending.blend_equation_rgb.Assign(FB::BlendEquation::Add);
        om.alpha_blending.blend_equation_a.Assign(FB::BlendEquation::Add);
        om.alpha_blending.factor_source_rgb.Assign(FB::BlendFactor::SourceAlpha);
        om.alpha_blending.factor_source_a.Assign(FB::BlendFactor::SourceAlpha);
        om.alpha_blending.factor_dest_rgb.Assign(FB::BlendFactor::OneMinusSourceAlpha);
        om.alpha_blending.factor_dest_a.Assign(FB::BlendFactor::OneMinusSourceAlpha);
        return f;
    };
    for (M mode : {M::Custom, M::Native, M::Automatic, M::ComboGeneric}) {
        Fixture f = expanded(); f.compute_rect = &f.owned;
        f.owned.mode = mode; f.strict_compute = Settings::RequiresComputeOnly(mode);
        f.owned.selected = true;
        const bool enabled = mode == M::Automatic || mode == M::ComboGeneric;
        Check(ComputeRectStateRejections(f.regs) == 1026, "expanded fixture retains exact raw mask");
        Check(f.Route() == enabled && f.owned.compute_draws == enabled &&
              f.owned.native_draws == !enabled, "expanded state selects complete draw");
        Check(f.owned.unsupported == !enabled && f.owned.raw_unsupported == 1 &&
              f.owned.considered == 1, "raw rejection stays separate from effective route");
        logs.clear(); f.owned.ReportCensus(steady_clock::now());
        const auto summary = Parse(logs[0].text);
        Check(Number(summary,"state_admitted") == enabled && Number(summary,"unsupported") == !enabled &&
              Number(summary,"raw_state_admitted") == 0 && Number(summary,"raw_unsupported") == 1 &&
              Number(summary,"raw_unsupported_total") == 1 && summary.at("conservation") == "true",
              "raw and effective admission differ");
        Check(Number(summary,"raw_admitted_six") == 0 && Number(summary,"six") == 1 &&
              Number(Parse(logs[1].text),"mask") == 1026, "raw ranked group preserves new supported mask");
    }
    for (unsigned failure = 0; failure < 4; ++failure) {
        Fixture f = expanded(); f.compute_rect = &f.owned; f.owned.selected = true;
        if (failure == 0) for (auto& vertex : f.vertex_batch) vertex.color[3] = .5f;
        if (failure == 1) f.target.color_id = 0;
        if (failure == 2) f.vertex_batch[0].position.w = 2.f;
        if (failure == 3) f.owned.selected = false;
        Check(!f.Route() && f.owned.native_draws == 1 && !f.owned.compute_draws,
              "unsupported incomplete and unselected draw remain native");
        Check(f.owned.raw_unsupported == 1 && f.owned.unsupported == (failure == 0) &&
              f.owned.format_rejected == (failure == 1) && f.owned.geometry_rejected == (failure == 2),
              "effective state format and geometry remain distinct");
        logs.clear(); f.owned.Report();
        const auto summary = Parse(logs[0].text);
        const auto routes = Parse(logs.back().text);
        Check(Number(summary,"raw_unsupported") == 1 && Number(summary,"unsupported") == (failure == 0) &&
              Number(routes,"unsupported_state") == (failure == 0) && summary.at("conservation") == "true",
              "final effective route totals are not raw blockers");
    }
    // CodexAstraLocal: Saturation, regressed totals and impossible effective
    // populations invalidate diagnostics instead of printing wrapped admission.
    for (unsigned failure = 0; failure < 4; ++failure) {
        ComputeRectRenderer r;
        if (failure == 0) { r.raw_unsupported = std::numeric_limits<u64>::max(); r.ObserveState(2,6); ++r.considered; }
        if (failure == 1) r.census_raw_unsupported = 1;
        if (failure == 2) { r.ObserveState(0,6); ++r.considered; ++r.unsupported; }
        if (failure == 3) { r.ObserveState(2,6); ++r.considered; r.unsupported = 2; }
        logs.clear(); r.ReportCensus(steady_clock::now());
        const auto summary = Parse(logs[0].text);
        Check(summary.at("conservation") == "false" && Number(summary,"state_admitted") <= 1,
              "inconsistent effective or raw counters invalidate conservation");
        if (failure == 0) Check(summary.at("overflow") == "true", "raw counter saturation is explicit");
    }
}

// CodexAstraLocal: Preserve the former ordinary fallback cases, while the new
// independent owner must stop on errors, publish state only after its consumer,
// and count valid zero-emission work without pretending it rasterized pixels.
void TestStrictIsolation() {
    using namespace Vulkan;
    using M = Settings::UberharTestMode;
    for (M mode : {M::Custom, M::Native, M::Automatic, M::ComboGeneric}) {
        for (unsigned failure=0;failure<7;++failure) {
            Fixture f{mode};f.regs=Registers(0);f.vertex_batch=Vertices(6);f.owned.selected=true;
            if(failure==0)f.regs=Registers(4);
            if(failure==1)f.target.color_id=0;
            if(failure==2)f.vertex_batch[0].position.w=0;
            if(failure==3)f.owned.selected=false;
            if(failure==4)f.compute_rect=nullptr;
            if(failure==5)f.target.handle=false;
            if(failure==6)f.res_cache.surface.traits.storage_support=false;
            Check(f.Route()==(failure==5)&&f.graphics_fallthrough==(failure!=5),
                  "other modes retain graphics fallback and early no-target behavior");
            Check(!f.res_cache.cancellations&&f.vertex_batch.size()==6&&!f.strict_compute_stats.attempts,
                  "other modes retain batch ownership and no strict counters");
        }
    }
    for(unsigned unavailable=0;unavailable<3;++unavailable) {
        StrictFixture f;
        if(unavailable==0)f.strict_compute=false;
        if(unavailable==1)f.compute_raster=false;
        if(unavailable==2)f.compute_vertices=nullptr;
        Pica::ComputeAssemblyResult completed{};completed.state.words[0]=0xdead;
        Check(!f.DrawComputeBatch(false,{},completed)&&!f.memory.calls&&!f.strict_compute_stats.attempts&&
              completed.state.words[0]==0xdead,"unavailable compute owner cannot consume a draw");
    }
    for(bool immediate:{false,true})for(unsigned population:{0U,6U}) {
        StrictFixture f;f.producer.count=population;
        Pica::AttributeBuffer attributes{};Pica::ComputeAssemblyResult completed{};
        Check(f.DrawComputeBatch(false,{},completed,immediate?&attributes:nullptr),"strict batch succeeds");
        Check(f.raster_calls==unsigned(population!=0)&&f.producer.marks==unsigned(population!=0),
              "strict complete batch records one raster");
        Check(f.memory.calls==1&&f.producer.calls==1&&f.res_cache.flushes==1&&completed.state.words[0]==91,
              "strict original input and final state pass through once");
        const auto& c=f.strict_compute_stats;
        Check(c.attempts==1&&c.computed==1&&c.rasterized==unsigned(population!=0)&&
              c.empty_batches==unsigned(population==0)&&!c.terminal_failures,"strict successful population partition");
        logs.clear();f.ReportStrictCompute();const auto report=Parse(logs.at(0).text);
        Check(Number(report,"schema")==2&&report.at("conservation")=="true"&&
              report.at("vertex_policy")=="original_input_compute"&&report.at("graphics_fallback")=="disabled"&&
              Number(report,"computed")==1&&Number(report,"rasterized")==unsigned(population!=0),
              "strict report binds original input and actual raster scope");
    }
    for(unsigned failure=0;failure<4;++failure) {
        StrictFixture f;
        if(failure==0)f.vertex_batch=Vertices(1);
        if(failure==1)f.memory.fail=true;
        if(failure==2)f.producer.fail=true;
        if(failure==3)f.consumer_failure=true;
        Pica::ComputeAssemblyResult completed{};completed.state.words[0]=0xdead;
        bool threw{};
        try{(void)f.DrawComputeBatch(false,{},completed);}catch(const VideoCore::ShaderRecoveryError&){threw=true;}
        Check(threw&&!f.raster_calls&&!f.producer.marks&&completed.state.words[0]==0xdead,
              "strict failure preserves uncommitted state");
        logs.clear();f.ReportStrictCompute();const auto report=Parse(logs.at(0).text);
        Check(Number(report,"attempts")==1&&Number(report,"terminal_failures")==1&&
              Number(report,"computed")==0&&report.at("conservation")=="true"&&
              report.at("zero_compute_work")=="true","strict failure report conserves actual calls");
    }
    for(bool nonempty:{false,true}) {
        Fixture f{M::Compute};if(nonempty)f.vertex_batch=Vertices(3);
        bool threw{};try{f.EmptyBatch();}catch(const VideoCore::ShaderRecoveryError&){threw=true;}
        Check(threw&&!f.nonempty_calls,"strict CPU-prepared entry is terminal");
    }
    Fixture accelerated{M::Compute};accelerated.accelerate=true;accelerated.vertex_batch=Vertices(6);
    bool threw{};try{(void)accelerated.Route();}catch(const VideoCore::ShaderRecoveryError&){threw=true;}
    Check(threw&&!accelerated.sync_calls&&!accelerated.graphics_fallthrough&&accelerated.vertex_batch.size()==6,
          "strict hardware entry is terminal");
    Fixture missing{M::Compute};missing.target.handle=false;missing.vertex_batch=Vertices(6);
    threw=false;try{(void)missing.Route();}catch(const VideoCore::ShaderRecoveryError&){threw=true;}
    Check(threw&&missing.res_cache.cancellations==1&&missing.strict_compute_stats.no_target==1&&
          !missing.graphics_fallthrough&&missing.vertex_batch.size()==6,"strict missing target is terminal without consuming CPU batch");
}

int main() {
    try { TestBins(); TestAdmission(); TestReports(); TestEffectiveAdmission(); TestStrictIsolation(); TestDeferredAdmission(); std::printf("PASS checks=%u bank_bytes=%zu\n", checks, sizeof(Vulkan::ComputeStateCensus)); }
    catch (const std::exception& error) { std::fprintf(stderr,"FAILED: %s\n",error.what()); return 1; }
}
