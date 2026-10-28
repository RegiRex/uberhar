// CodexAstraLocal: Recording endpoints execute the extracted production sample
// lifecycle. This proves metadata/order/bounds, not Vulkan timing or GPU pixels.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>
using u32 = std::uint32_t;
using u64 = std::uint64_t;
int checks{};
void Require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::cerr << "FAIL " << what << '\n'; std::exit(1); }
}
// CodexAstraLocal: Formatting/logging is deliberately inert; report aggregation
// and interval reset execute production code and are inspected as state below.
#define LOG_INFO(...) ((void)0)
namespace vk {
enum class PipelineStageFlagBits { eTopOfPipe, eBottomOfPipe };
enum class Result { eSuccess, eNotReady, eError };
enum class QueryResultFlagBits { e64 = 1, eWithAvailability = 2 };
constexpr int operator|(QueryResultFlagBits a, QueryResultFlagBits b) {
    return int(a) | int(b);
}
using UniqueQueryPool = std::unique_ptr<int>;
struct Driver {
    std::array<std::array<u64, 4>, 16> values{};
    Result result{Result::eSuccess};
    u64 resets{}, starts{}, ends{}, queries{}, draws{};
};
struct CommandBuffer {
    Driver* driver;
    void resetQueryPool(int, u32 start, u32 count) {
        Require(count == 2 && start % 2 == 0, "one timestamp pair");
        ++driver->resets;
        driver->values[start / 2] = {};
    }
    void writeTimestamp(PipelineStageFlagBits stage, int, u32) {
        if (stage == PipelineStageFlagBits::eTopOfPipe) ++driver->starts;
        else ++driver->ends;
    }
    void draw() { ++driver->draws; }
};
struct Device {
    Driver* driver;
    Result getQueryPoolResults(int, u32 start, u32 count, std::size_t size, u64* out,
                               std::size_t stride, int flags) {
        Require(count == 2 && size == 4 * sizeof(u64) && stride == 2 * sizeof(u64),
                "query pair ABI");
        Require(flags == 3, "nonwaiting query flags");
        ++driver->queries;
        std::copy(driver->values[start / 2].begin(), driver->values[start / 2].end(), out);
        return driver->result;
    }
};
}
struct Instance { vk::Driver& driver; vk::Device GetDevice() const { return {&driver}; } };
struct Scheduler {
    vk::Driver& driver;
    u64 tick{1}, completed{}, reports{};
    std::vector<std::function<void(vk::CommandBuffer)>> queue;
    template <typename F> void Record(F&& f) { queue.emplace_back(std::forward<F>(f)); }
    void Drain() {
        auto pending = std::move(queue);
        queue.clear();
        for (auto& command : pending) command({&driver});
    }
    u64 CurrentTick() const { return tick; }
    bool IsFree(u64 value) const { return completed >= value; }
    void ReportDiagnostics(bool) { ++reports; }
};
struct RenderPass {
    struct { struct { u32 width{}, height{}; } extent; } render_area;
    u32 do_clear{};
};
struct GraphicsPipeline {
    u64 key;
    std::array<u64, 3> shaders;
    u64 Key() const { return key; }
    auto ExecutionShaders() const { return shaders; }
};
struct PipelineCache {
    const GraphicsPipeline* bound_pipeline{};
    // CodexAstraLocal: Deliberately stale request is used only by a semantic
    // defect control; production observes the extracted fulfilled accessor.
    const GraphicsPipeline* requested_pipeline{};
#include "pipeline-accessor.inc"
};
#include "reason.inc"
struct RenderManager {
    const Instance& instance;
    Scheduler& scheduler;
#include "diagnostic-fields.inc"
    RenderManager(const Instance& instance_, Scheduler& scheduler_)
        : instance(instance_), scheduler(scheduler_) {
        diagnostics_enabled = true;
        diagnostic_queries = std::make_unique<int>(1);
        diagnostic_period = 1;
        diagnostic_mask = ~u64{};
        diagnostic_next_report = std::chrono::steady_clock::time_point::max();
    }
    void BeginDiagnosticPass(const RenderPass&, bool);
    void EndDiagnosticPass();
    void RecordDiagnosticDraw(const PipelineCache&, u32, u32);
    void PollDiagnostics(bool final = false) noexcept;
};
#include "production.inc"
struct Fixture {
    vk::Driver driver;
    Instance instance{driver};
    Scheduler scheduler{driver};
    RenderManager manager{instance, scheduler};
    PipelineCache pipelines;
    RenderPass pass{{{40, 20}}, 0};
    void Draw(const GraphicsPipeline* pipeline, u32 color = 0x1000, u32 depth = 0x2000) {
        scheduler.Record([this, pipeline](vk::CommandBuffer cb) {
            // CodexAstraLocal: This endpoint models the real queued arbitration
            // followed by draw, with a stale owner-requested alternative.
            pipelines.bound_pipeline = pipeline;
            cb.draw();
        });
        manager.RecordDiagnosticDraw(pipelines, color, depth);
    }
    void Available(u32 index = 0, u64 ns = 20) { driver.values[index] = {100, 1, 100 + ns, 1}; }
};
int main() {
    GraphicsPipeline a{11, {101, 102, 0}}, b{22, {101, 202, 0}}, same_shaders{33, a.shaders};
    {
        Fixture f;
        f.pipelines.requested_pipeline = &b;
        f.manager.BeginDiagnosticPass(f.pass, true);
        Require(f.manager.diagnostic_active == 0, "first pass sampled");
        f.Draw(&a);
        f.Draw(&b);
        f.manager.PollDiagnostics();
        f.Draw(&a, 0x3000, 0x4000);
        f.manager.EndDiagnosticPass();
        // CodexAstraLocal: Even a misleading ready GPU endpoint cannot publish
        // host metadata before the queued command's release store executes.
        f.scheduler.completed = 1;
        f.Available();
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_sampled == 0 && f.driver.queries == 0,
                "unpublished worker metadata not consumed");
        f.scheduler.completed = 0;
        f.scheduler.Drain();
        const auto& sample = f.manager.diagnostic_samples[0];
        Require(sample.attribution.first_pipeline == 11, "fulfilled pipeline wins requested identity");
        Require(sample.attribution.first_shaders == a.shaders, "fulfilled shader IDs");
        Require(sample.attribution.draws == 3 && sample.attribution.pipeline_changes == 2 &&
                sample.attribution.shader_changes == 2, "ABA retains mixed changes");
        Require(sample.attribution.first_pipeline == sample.attribution.last_pipeline,
                "ABA matching endpoints");
        Require(sample.frame_begin == 1 && sample.frame_end == 2 &&
                sample.attribution.first_frame == 1 && sample.attribution.last_frame == 2,
                "frame span captured at enqueue");
        Require(sample.attribution.first_target[0] == 0x1000 &&
                sample.attribution.last_target[0] == 0x3000 && sample.attribution.target_changes == 1,
                "guest target span");
        f.Available();
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_sampled == 0 && f.driver.queries == 0,
                "unfinished GPU tick not consumed");
        f.scheduler.completed = 1;
        f.driver.result = vk::Result::eNotReady;
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_sampled == 0 && sample.pending,
                "unavailable query retains slot");
        f.driver.result = vk::Result::eSuccess;
        f.driver.values[0][3] = 0;
        f.manager.PollDiagnostics();
        Require(sample.pending, "missing end availability retains slot");
        f.Available();
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_sampled == 1 && f.manager.diagnostic_draws == 3 &&
                f.manager.diagnostic_mixed_shader_samples == 1 &&
                f.manager.diagnostic_mixed_pipeline_samples == 1, "cumulative mixed counters");
        Require(!sample.pending && f.manager.diagnostic_slowest.attribution.draws == 3 &&
                f.manager.diagnostic_slowest_ns == 20, "representative copied before slot reuse");
        f.manager.diagnostic_passes = 257;
        f.manager.BeginDiagnosticPass(f.pass, true);
        Require(!f.manager.diagnostic_published[0].load() &&
                f.manager.diagnostic_samples[0].attribution.draws == 0, "slot metadata reset");
        f.Draw(&a);
        f.Draw(&same_shaders);
        f.manager.EndDiagnosticPass();
        f.scheduler.Drain();
        f.Available(0, 10);
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_mixed_shader_samples == 1 &&
                f.manager.diagnostic_mixed_pipeline_samples == 2, "state changes differ from shaders");
        Require(f.manager.diagnostic_slowest.pass_id == 1, "slower representative retained");
        f.manager.PollDiagnostics(true);
        Require(!f.manager.diagnostic_slowest.pending && f.scheduler.reports == 1,
                "report resets one bounded representative");
    }
    {
        Fixture f;
        // CodexAstraLocal: Sparse stride and per-frame budget remain independent
        // of draw counts; pending slots cannot be reused even after worker drain.
        for (u32 i = 0; i < 1300; ++i) {
            f.manager.BeginDiagnosticPass(f.pass, true);
            f.manager.RecordDiagnosticDraw(f.pipelines, 1, 2);
            f.manager.EndDiagnosticPass();
        }
        f.scheduler.Drain();
        Require(f.driver.starts == 4 && f.driver.ends == 4 && f.driver.resets == 4,
                "stride and four per frame bound");
        for (u32 frame = 0; frame < 3; ++frame) {
            f.manager.PollDiagnostics();
            for (u32 i = 0; i < 4; ++i) {
                f.manager.diagnostic_passes = 257 * (20 + frame * 4 + i);
                f.manager.BeginDiagnosticPass(f.pass, true);
                f.manager.EndDiagnosticPass();
            }
        }
        f.scheduler.Drain();
        f.manager.PollDiagnostics();
        f.manager.diagnostic_passes = 257 * 50;
        f.manager.BeginDiagnosticPass(f.pass, true);
        Require(f.manager.diagnostic_active == -1 && f.manager.diagnostic_full == 1 &&
                f.driver.starts == 16, "sixteen pending slots bound residency");
        const auto queued = f.scheduler.queue.size();
        f.manager.RecordDiagnosticDraw(f.pipelines, 1, 2);
        Require(f.scheduler.queue.size() == queued, "unsampled draw queues no observer");
    }
    {
        Fixture f;
        f.manager.BeginDiagnosticPass(f.pass, false);
        f.manager.EndDiagnosticPass();
        f.scheduler.Drain();
        f.Available(0, 0);
        f.scheduler.completed = 1;
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_slowest.pending &&
                f.manager.diagnostic_slowest.attribution.draws == 0 &&
                f.manager.diagnostic_kind_samples[0] == 1, "utility zero duration remains explicit");
        f.manager.diagnostic_passes = 257;
        f.manager.BeginDiagnosticPass(f.pass, true);
        f.Draw(nullptr);
        f.manager.EndDiagnosticPass();
        f.scheduler.Drain();
        f.Available(0, 5);
        f.manager.PollDiagnostics();
        Require(f.manager.diagnostic_unattributed == 1 &&
                f.manager.diagnostic_slowest.pass_id == 258, "unknown binding and slower replacement");
    }
    std::cout << "PASS GPU pass attribution checks=" << checks << '\n';
}
