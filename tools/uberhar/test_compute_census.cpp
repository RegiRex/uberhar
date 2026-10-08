// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Execute real census/report/admission bodies with synthetic
// registers and recording resource/log endpoints; this is not GPU execution.
#include <algorithm>
#include <chrono>
#include <cstdio>
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
unsigned checks{};
void Check(bool yes, const char* reason) {
    ++checks;
    if (!yes) throw std::runtime_error(reason);
}
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
enum class SurfaceType { Color };
struct HardwareVertex { Common::Vec4f position, color; };
struct Surface {
    struct { vk::Format native{vk::Format::eR8G8B8A8Unorm}; bool storage_support{true}; } traits;
    int image{7}; int Image() const { return image; }
};
struct Framebuffer {
    unsigned color_id{1}, color_level{};
    VideoCore::PixelFormat format{VideoCore::PixelFormat::RGBA8};
    VideoCore::PixelFormat Format(SurfaceType) const { return format; }
    std::array<int, 1> Images() const { return {7}; }
};
class ComputeRectRenderer {
public:
    Settings::UberharTestMode mode{Settings::UberharTestMode::Compute}; bool selected{};
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
    ComputeRectRenderer owned;
    ComputeRectRenderer* compute_rect{&owned};
    bool accelerate{};
    Pica::RegsInternal regs{};
    std::vector<HardwareVertex> vertex_batch;
    Framebuffer target;
    struct { Surface surface; Surface& GetSurface(unsigned) { return surface; } } res_cache;
    struct {
        struct View { s32 x{}, y{}, width{64}, height{32}; };
        struct Rect { s32 left{}, bottom{}, right{64}, top{32}; };
        View Viewport() const { return {}; }
        Rect DrawRect() const { return {}; }
    } fb_helper;
    struct { unsigned ended{}; void EndRendering() { ++ended; } } renderpass_cache;
    bool Route() {
        const Framebuffer* framebuffer = &target;
        std::optional<ComputeRectPacket> compute_packet;
        int timing_slot = -1;
#include "admission.inc"
        return false;
    }
};
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
    for (M mode : {M::Custom, M::Native, M::Compute, M::Automatic, M::ComboGeneric}) {
        Fixture f = expanded(); f.compute_rect = &f.owned;
        f.owned.mode = mode; f.owned.selected = true;
        const bool enabled = mode == M::Compute || mode == M::Automatic || mode == M::ComboGeneric;
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
int main() {
    try { TestBins(); TestAdmission(); TestReports(); TestEffectiveAdmission(); std::printf("PASS checks=%u bank_bytes=%zu\n", checks, sizeof(Vulkan::ComputeStateCensus)); }
    catch (const std::exception& error) { std::fprintf(stderr,"FAILED: %s\n",error.what()); return 1; }
}
