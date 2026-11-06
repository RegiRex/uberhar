// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Execute the production helper and text adapter on host inputs;
// these tests measure no device performance and simulate no Vulkan execution.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "video_core/uberhar_frame_workload_census_log.h"

std::atomic<std::size_t> allocations{};
// CodexAstraLocal: Keep replacement allocation boundaries opaque to GCC's
// mismatched-new-delete heuristic; new/delete intentionally share malloc/free.
[[gnu::noinline]] void* operator new(std::size_t n) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc{};
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using Census = VideoCore::FrameWorkloadCensus;
using Delivery = Census::Delivery;
unsigned checks{};
void Check(bool good, const char* reason) {
    ++checks;
    if (!good) throw std::runtime_error(reason);
}

// CodexAstraLocal: Substitute only the asynchronous logger endpoint; all tuple
// formatting and the truthful Unknown-delivery adapter are production code.
struct Log { Common::Log::Delivery delivery; std::string text; };
std::vector<Log> logs;
namespace Common::Log {
void FmtLogMessageWithDeliveryImpl(Class, Level, const char*, unsigned int, const char*,
                                   fmt::string_view format, const fmt::format_args& args,
                                   Delivery delivery) {
    logs.push_back({delivery, fmt::vformat(format, args)});
}
} // namespace Common::Log

struct RecordingSink {
    Census::FrameReport frame{};
    unsigned summaries{}, targets{}, textures{}, transfers{};
    Delivery descriptor_delivery{Delivery::Accepted}, summary_delivery{Delivery::Accepted};
    bool throw_summary{};
    Delivery operator()(const Census::FrameReport& report) {
        frame = report;
        ++summaries;
        if (throw_summary) throw std::bad_alloc{};
        return summary_delivery;
    }
    Delivery operator()(const Census::Descriptor<Census::Target>&) {
        ++targets; return descriptor_delivery;
    }
    Delivery operator()(const Census::Descriptor<Census::Texture>&) {
        ++textures; return descriptor_delivery;
    }
    Delivery operator()(const Census::Descriptor<Census::TransferKey>&) {
        ++transfers; return descriptor_delivery;
    }
};
Census::Draw MakeDraw(unsigned id) {
    Census::Draw d;
    d.target.known = Census::Target::GuestColor | Census::Target::GuestExtent;
    d.target.color_address = 0x1000 + id * 0x1000;
    d.target.guest_width = 240; d.target.guest_height = 400;
    d.target.viewport = {0, 0, 960, 1600};
    d.target.known |= Census::Target::Viewport;
    d.submitted_vertices = 6;
    d.route = Census::Route::CpuVertexUnclassified;
    for (unsigned i = 0; i < d.textures.size(); ++i) {
        auto& t = d.textures[i];
        t.unit = i; t.address = id * 3 + i; t.width = 64; t.height = 128;
        t.known = Census::Texture::Address | Census::Texture::Extent | Census::Texture::MipRange;
        t.mip_max = 6;
    }
    d.texture_mask = 7;
    return d;
}

void TestDisabledAndAllocation() {
    const auto before = allocations.load();
    Census disabled{false};
    RecordingSink sink;
    auto draw = MakeDraw(0);
    for (unsigned i = 0; i < 100; ++i) {
        disabled.RecordDraw(draw); disabled.RecordTransfer({}); disabled.FinishFrame({}, sink);
    }
    Check(!disabled.Enabled() && !disabled.AllocationFailed(), "disabled state");
    Check(allocations.load() == before && sink.summaries == 0, "OFF no allocation/sink");
    Census enabled{true};
    Check(enabled.Enabled() && allocations.load() == before + 1, "ON one state allocation");
    const auto after = allocations.load();
    for (unsigned i = 0; i < 100; ++i) enabled.RecordDraw(draw);
    enabled.FinishFrame({}, sink);
    Check(allocations.load() == after, "no helper allocation in draw/finish");
    Check(sink.frame.draws == 100 && sink.frame.submitted_vertices == 600, "all draw totals");
    Check(sink.frame.routes[3].draws == 100, "truthful unclassified CPU route");
    Check(sink.frame.targets[0].texture_slots == 7 && sink.frame.texture_bindings == 300,
          "target texture co-occurrence and full bindings");
}

void TestOverflowAndGenerations() {
    Census c{true}; RecordingSink sink;
    for (unsigned i = 0; i < 40; ++i) c.RecordDraw(MakeDraw(i));
    for (unsigned i = 0; i < 12; ++i) {
        Census::Transfer t; t.key.source_address = i; t.logical_bytes_known = i % 2 == 0;
        t.logical_bytes = 128; c.RecordTransfer(t);
    }
    c.FinishFrame({73, 144, 39, true, true, false}, sink);
    const auto& f = sink.frame;
    Check(f.draws == 40 && f.submitted_vertices == 240 && f.texture_bindings == 120,
          "overflow preserves all-draw denominator");
    Check(f.quality.target_overflow_draws == 24 && f.quality.texture_overflow_bindings == 88,
          "target/texture overflow exact");
    Check(f.transfer_operations == 12 && f.transfer_logical_bytes == 768 &&
          f.transfer_unknown_byte_operations == 6 && f.quality.transfer_overflow_operations == 4,
          "known/unknown bytes preserved after transfer overflow");
    Check(f.descriptor_attempts == 4 && f.quality.pending_descriptors == 52,
          "metadata quota independent from all totals");
    Check(f.identity.callback_index == 73 && f.identity.command_lists_total == 144 &&
          f.identity.game_submissions_total == 39, "separate source denominators");
    const auto generation = f.targets[0].generation;
    auto next = MakeDraw(90); c.RecordDraw(next); c.FinishFrame({}, sink);
    Check(sink.frame.draws == 1 && sink.frame.targets[0].generation == generation + 1,
          "slot reuse changes generation and resets counters");
    Check(sink.frame.quality.evicted_pending_descriptors +
          sink.frame.quality.evicted_unconfirmed_descriptors > 0, "descriptor loss explicit");
    c.FinishFrame({}, sink);
    Check(sink.frame.draws == 0 && sink.frame.transfer_operations == 0,
          "empty callbacks retained without replay");
    Check(sink.frame.descriptor_attempts <= 4, "empty-frame metadata quota");
}

void TestDeliveryAndTextureIdentity() {
    Census c{true}; RecordingSink sink;
    auto d = MakeDraw(0); d.texture_mask = 1;
    c.RecordDraw(d);
    d.textures[0].mip_max = 1; c.RecordDraw(d);
    d.target.viewport.width = 480; c.RecordDraw(d);
    sink.descriptor_delivery = Delivery::Dropped;
    c.FinishFrame({}, sink);
    Check(sink.frame.targets[0].operations == 2 && sink.frame.targets[1].operations == 1,
          "target viewport changes retain distinct populations");
    Check(sink.frame.textures[0].operations == 1 && sink.frame.textures[1].operations == 2,
          "configured mip differences retain distinct populations");
    Check(sink.frame.quality.sink_known_drops == 4 && sink.frame.quality.pending_descriptors == 4,
          "known dropped descriptors stay retryable");
    sink.descriptor_delivery = Delivery::Unknown;
    c.RecordDraw(d); c.FinishFrame({}, sink);
    Check(sink.frame.quality.sink_unconfirmed == 4 && sink.frame.quality.pending_descriptors == 0,
          "Unknown is not delivered or known-dropped");
    Check(sink.frame.quality.used_unconfirmed_descriptors == 2, "used uncertain definitions explicit");
    c.RecordDraw(d); c.FinishFrame({}, sink);
    Check(sink.frame.descriptor_attempts == 0, "unchanged attempted descriptors not spammed");
    sink.throw_summary = true;
    c.RecordDraw(d); c.FinishFrame({}, sink);
    sink.throw_summary = false;
    c.FinishFrame({}, sink);
    Check(sink.frame.draws == 0 && sink.frame.quality.sink_exceptions == 1 &&
          sink.frame.quality.sink_known_drops == 5, "sink throw contained and lost interval not replayed");
}

void TestSaturationAndClockScope() {
    Census c{true}; RecordingSink sink;
    auto d = MakeDraw(0); d.submitted_vertices = std::numeric_limits<Census::U64>::max();
    c.RecordDraw(d); c.RecordDraw(d); c.FinishFrame({}, sink);
    Check(sink.frame.quality.counter_saturated && sink.frame.submitted_vertices == d.submitted_vertices,
          "saturation invalidates conservation instead of wrapping");
    d.submitted_vertices = 3; d.route = static_cast<Census::Route>(999); d.texture_mask = 8;
    c.RecordDraw(d); c.FinishFrame({}, sink);
    Check(sink.frame.quality.invalid_routes == 1 && sink.frame.routes[0].draws == 1 &&
          sink.frame.quality.invalid_texture_mask_draws == 1, "invalid future enums explicit");
    while (sink.frame.report_sequence < 64) c.FinishFrame({}, sink);
    Check(sink.frame.sampled_report_sequence == 0, "clock sample reported after measured boundary");
    c.FinishFrame({}, sink);
    Check(sink.frame.sampled_report_sequence == 64, "sparse prior report-only host duration");
}

void TestProductionText() {
    logs.clear(); Census c{true}; VideoCore::FrameWorkloadCensusLog logger;
    c.RecordDraw(MakeDraw(0)); c.FinishFrame({}, logger);
    Check(logs.size() == 5, "one summary plus at most four definitions");
    for (const auto& log : logs) Check(log.delivery == Common::Log::Delivery::Diagnostic,
                                    "only droppable diagnostic delivery");
    const auto& line = logs.back().text;
    Check(line.find("delivery=unconfirmed") != std::string::npos &&
          line.find("sink_unconfirmed_before_this_summary=4") != std::string::npos,
          "production adapter does not claim delivery");
    Check(line.find("targets(slot:gen:draws:") != std::string::npos &&
          line.find("host_sample_scope=finish_report_only_not_draw_bookkeeping_or_gpu") != std::string::npos,
          "tuple schema and host scope textual");
    Check(logs[0].text.find("viewport_xywh=0,0,960,1600") != std::string::npos,
          "actual dimensions emitted");
    Census::FrameReport maximum;
    for (auto& b : maximum.targets) b = {~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0U, ~0ULL};
    for (auto& b : maximum.textures) b = maximum.targets[0];
    for (auto& b : maximum.transfers) b = maximum.targets[0];
    logger(maximum);
    Check(logs.back().text.find("tuple_text_truncated=false") != std::string::npos,
          "maximum integer tuple payload fits bounded buffer");
}

// CodexAstraLocal: Host synthetic cost only. Opaque call boundary and observable
// counts retain both OFF/ON loops; formatting, queueing and renderer field setup
// are excluded. This is not an Android frame-time or device-overhead result.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void Observe(Census& c, const Census::Draw& d) { c.RecordDraw(d); }
double Benchmark(bool enabled, bool churn) {
    Census c{enabled}; auto d = MakeDraw(0); RecordingSink sink;
    constexpr unsigned frames = 1000, draws = 256;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned f = 0; f < frames; ++f) {
        for (unsigned i = 0; i < draws; ++i) {
            if (churn) {
                d.target.color_address = i % 40;
                for (unsigned unit = 0; unit < d.textures.size(); ++unit)
                    d.textures[unit].address = 3 * (i % 40) + unit;
            }
            Observe(c, d);
        }
        c.FinishFrame({f}, sink);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (enabled) Check(sink.frame.draws == draws, "benchmark observable complete count");
    return double(ns) / (frames * draws);
}
int main() {
    try {
        TestDisabledAndAllocation(); TestOverflowAndGenerations(); TestDeliveryAndTextureIdentity();
        TestSaturationAndClockScope(); TestProductionText();
        const auto off = Benchmark(false, false), on = Benchmark(true, false), churn = Benchmark(true, true);
        std::printf("PASS checks=%u host_synthetic_ns_per_draw off=%.2f on_stable=%.2f on_churn=%.2f "
                    "includes_finish_per_256_draws=1 excludes_descriptor_construction_logger_and_device=1\n",
                    checks, off, on, churn);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
