// CodexAstraLocal: Execute extracted stream/scheduler/pass and CPU draw seams
// with recording endpoints. No Vulkan driver, device, shader or pixel executes.
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;
static unsigned checks{};

void Require(bool condition, const char* reason) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

#define ASSERT(c) Require(bool(c), #c)
#define ASSERT_MSG(c, ...) Require(bool(c), #c)
#define MICROPROFILE_SCOPE(...) ((void)0)
constexpr u64 VK_NULL_HANDLE = 0;
constexpr u32 VK_QUEUE_FAMILY_IGNORED = ~0U;
constexpr u32 VK_REMAINING_ARRAY_LAYERS = ~0U;

namespace Common {
template <class T, class U>
auto AlignUp(T value, U alignment) {
    return (value + alignment - 1) / alignment * alignment;
}
template <class T>
struct Rectangle {
    T left{}, top{}, right{}, bottom{};
    T GetWidth() const { return right - left; }
    T GetHeight() const { return top - bottom; }
};
} // namespace Common

// CodexAstraLocal: Command-buffer state is independent for each allocation;
// unique payload expectations detect overwrites as well as missing draw setup.
struct Observation {
    bool begun{}, ended{}, pass{}, pipeline{}, vertices{};
};
struct DrawObservation {
    unsigned cb{};
    u32 vertices{};
    bool pass{}, pipeline{}, bytes{};
};
struct Driver {
    unsigned next{};
    std::map<unsigned, Observation> states;
    std::vector<std::string> events;
    std::vector<DrawObservation> draws;
    std::vector<std::pair<u8, u32>> expected;
    std::vector<u64> draw_ticks, query_begin_ticks, query_end_ticks;
    unsigned selections{}, completions{};
    std::vector<u8>* data{};
    u64 vertex_offset{};

    void Event(unsigned cb, const std::string& label) {
        events.push_back(std::to_string(cb) + ":" + label);
    }
};

// CodexAstraLocal: These types only satisfy the extracted command signatures.
// Their checks are not a substitute for Vulkan validation or memory visibility.
namespace vk {
using Semaphore = void*;
using Buffer = u64;
using DeviceMemory = u64;
using Framebuffer = u64;
using RenderPass = u64;
using Image = u64;
using PipelineStageFlags = u32;
using AccessFlags = u32;
using ImageAspectFlags = u32;
enum class PipelineStageFlagBits : u32 {
    eFragmentShader = 1, eColorAttachmentOutput = 2,
    eEarlyFragmentTests = 4, eLateFragmentTests = 8, eTransfer = 16
};
enum AccessFlagBits : u32 {
    eShaderWrite = 1, eColorAttachmentWrite = 2, eDepthStencilAttachmentWrite = 4,
    eShaderRead = 8, eTransferRead = 16
};
enum class ImageAspectFlagBits : u32 { eColor = 1, eDepth = 2 };
enum class ImageLayout : u32 { eGeneral };
enum class DependencyFlagBits : u32 { eByRegion };
enum class SubpassContents : u32 { eInline };
enum class PipelineBindPoint : u32 { eGraphics };
enum class CommandBufferUsageFlagBits : u32 { eOneTimeSubmit = 1 };

template <class T> requires std::is_enum_v<T>
u32 operator|(T a, T b) { return u32(a) | u32(b); }
template <class T> requires std::is_enum_v<T>
u32 operator&(u32 a, T b) { return a & u32(b); }
template <class T> requires std::is_enum_v<T>
u32& operator|=(u32& a, T b) { a |= u32(b); return a; }

struct Offset2D {
    s32 x{}, y{};
    bool operator==(const Offset2D&) const = default;
};
struct Extent2D {
    u32 width{}, height{};
    bool operator==(const Extent2D&) const = default;
};
struct Rect2D {
    Offset2D offset;
    Extent2D extent;
    bool operator==(const Rect2D&) const = default;
};
struct ClearValue { std::array<u32, 4> raw{}; };
struct RenderPassBeginInfo {
    RenderPass renderPass;
    Framebuffer framebuffer;
    Rect2D renderArea;
    u32 clearValueCount;
    const ClearValue* pClearValues;
};
struct ImageSubresourceRange {
    ImageAspectFlags aspectMask;
    u32 baseMipLevel, levelCount, baseArrayLayer, layerCount;
};
struct ImageMemoryBarrier {
    AccessFlags srcAccessMask, dstAccessMask;
    ImageLayout oldLayout, newLayout;
    u32 srcQueueFamilyIndex, dstQueueFamilyIndex;
    Image image;
    ImageSubresourceRange subresourceRange;
};
struct MappedMemoryRange { DeviceMemory memory; u64 offset, size; };
struct CommandBufferBeginInfo { CommandBufferUsageFlagBits flags; };
struct Device {
    void invalidateMappedMemoryRanges(MappedMemoryRange) {}
    void flushMappedMemoryRanges(MappedMemoryRange) {}
};

struct CommandBuffer {
    Driver* d{};
    unsigned id{};
    void begin(CommandBufferBeginInfo) {
        auto& state = d->states[id];
        state = {};
        state.begun = true;
        d->Event(id, "begin-cmdbuf");
    }
    void end() {
        auto& state = d->states[id];
        Require(state.begun && !state.ended, "end live command buffer");
        Require(!state.pass, "submit must close render pass");
        state.ended = true;
        d->Event(id, "end-cmdbuf");
    }
    void beginRenderPass(RenderPassBeginInfo, SubpassContents) {
        auto& state = d->states[id];
        Require(state.begun && !state.ended && !state.pass, "begin fresh render pass");
        state.pass = true;
        d->Event(id, "begin-renderpass");
    }
    void endRenderPass() {
        auto& state = d->states[id];
        Require(state.pass && !state.ended, "end active render pass");
        state.pass = false;
        d->Event(id, "end-renderpass");
    }
    template <class... T>
    void pipelineBarrier(T&&...) { d->Event(id, "barrier"); }
    void bindPipeline(PipelineBindPoint, u64) {
        d->states[id].pipeline = true;
        d->Event(id, "bind-pipeline");
    }
    template <class... T>
    void bindDescriptorSets(T&&...) { d->Event(id, "bind-descriptors"); }
    void bindVertexBuffers(u32, Buffer, u64 offset) {
        d->states[id].vertices = true;
        d->vertex_offset = offset;
        d->Event(id, "bind-vertices");
    }
    void draw(u32 n, u32, u32, u32) {
        auto& state = d->states[id];
        const auto expected = d->expected.at(d->draws.size());
        bool bytes = state.vertices && d->vertex_offset + n * 88 <= d->data->size();
        if (bytes) {
            for (u64 i = d->vertex_offset; i < d->vertex_offset + n * 88; ++i) {
                bytes &= (*d->data)[i] == expected.first;
            }
        }
        Require(n == expected.second, "original vertex count");
        d->draws.push_back({id, n, state.pass, state.pipeline, bytes});
        d->Event(id, std::string("draw:") +
                     (state.pass && state.pipeline && bytes ? "valid" : "INVALID"));
    }
};
} // namespace vk

namespace Vulkan {
enum class StateFlags { AllDirty = 0, Pipeline = 1, DescriptorSets = 2, FragmentConstants = 4 };
StateFlags operator|(StateFlags a, StateFlags b) { return StateFlags(int(a) | int(b)); }
StateFlags operator&(StateFlags a, StateFlags b) { return StateFlags(int(a) & int(b)); }
StateFlags& operator|=(StateFlags& a, StateFlags b) { return a = a | b; }
bool False(StateFlags flags) { return int(flags) == 0; }
struct Instance {
    bool should_flush{};
    u64 NonCoherentAtomSize() const { return 64; }
    bool ShouldFlush() const { return should_flush; }
};

// CodexAstraLocal: Submission and completion are explicit deterministic events.
// Real scheduler method bodies run, while queue draining and OS waiting do not.
struct Master {
    u64 current = 1, gpu{}, submitted{};
    u64 CurrentTick() const { return current; }
    bool IsFree(u64 tick) const { return gpu >= tick; }
    u64 NextTick() { return current++; }
    void Refresh() {}
    void Wait(u64 tick) {
        Require(submitted >= tick, "cannot wait an unsubmitted tick");
        gpu = std::max(gpu, tick);
    }
    void SubmitWork(vk::CommandBuffer cb, vk::Semaphore, vk::Semaphore, u64 tick) {
        cb.end();
        submitted = std::max(submitted, tick);
    }
};
struct Failure {
    Master* master{};
    std::function<void()> drain;
    bool Failed() const { return false; }
    void Check() const {}
    bool WaitSubmitted(u64 tick) {
        drain();
        Require(master->submitted >= tick, "queued submit precedes completion");
        return true;
    }
    void Submitted(u64 tick) {
        Require(master->submitted >= tick, "submission acknowledged");
    }
};
struct Chunk {
    std::deque<std::function<void(vk::CommandBuffer)>> commands;
    bool submit{};
    bool Empty() const { return commands.empty(); }
    void Discard() { commands.clear(); }
    void MarkSubmit() { submit = true; }
};
class Scheduler {
public:
    Driver& driver;
    std::unique_ptr<Master> master_semaphore = std::make_unique<Master>();
    Failure shader_failure;
    bool use_worker_thread = true;
    std::unique_ptr<Chunk> chunk = std::make_unique<Chunk>();
    std::queue<std::unique_ptr<Chunk>> work_queue;
    std::mutex queue_mutex, submit_mutex;
    std::condition_variable event_cv;
    std::function<void()> on_submit = [] {};
    std::function<void()> on_dispatch = [] {};
    StateFlags state = StateFlags::AllDirty;
    struct CommandPool {
        Driver& d;
        vk::CommandBuffer Commit() { return {&d, ++d.next}; }
    } command_pool;
    vk::CommandBuffer current_cmdbuf;

    explicit Scheduler(Driver& d) : driver(d), command_pool{d} {
        shader_failure.master = master_semaphore.get();
        shader_failure.drain = [this] { Drain(); };
        AllocateWorkerCommandBuffers();
    }
    template <class F>
    void Record(F&& f) { chunk->commands.emplace_back(std::forward<F>(f)); }
    void AcquireNewChunk() { chunk = std::make_unique<Chunk>(); }
    void Drain() {
        while (!work_queue.empty()) {
            auto queued = std::move(work_queue.front());
            work_queue.pop();
            for (auto& command : queued->commands) command(current_cmdbuf);
            if (queued->submit) AllocateWorkerCommandBuffers();
        }
    }
    void Flush(vk::Semaphore signal = nullptr, vk::Semaphore wait = nullptr);
    void Wait(u64);
    void DispatchWork();
    void SubmitExecution(vk::Semaphore, vk::Semaphore);
    void AllocateWorkerCommandBuffers();
    u64 CurrentTick() const { return master_semaphore->CurrentTick(); }
    void MarkStateNonDirty(StateFlags flag) noexcept { state |= flag; }
    bool IsStateDirty(StateFlags flag) const noexcept { return False(state & flag); }
};

#include "renderpass.inc"
struct Framebuffer {
    bool shadow_rendering{};
    u64 handle{1};
    u64 Handle() const { return handle; }
    u64 RenderPass() const { return 2; }
    std::array<u64, 2> Images() const { return {1, 2}; }
    std::array<u32, 2> Aspects() const { return {1, 2}; }
};
constexpr u32 MinDrawsToFlush = 20;
class RenderManager {
public:
    const Instance& instance;
    Scheduler& scheduler;
    std::array<vk::Image, 2> images{};
    std::array<vk::ImageAspectFlags, 2> aspects{};
    bool shadow_rendering{};
    RenderPass pass{};
    u32 num_draws{};
    void BeginRendering(const Framebuffer*, Common::Rectangle<u32>);
    void BeginRendering(const RenderPass&);
    void EndRendering();
};
enum class BufferType : u32 { Upload, Download, Stream };
constexpr u64 WATCHES_RESERVE_CHUNK = 0x1000;
class StreamBuffer {
public:
    struct Watch { u64 tick{}, upper_bound{}; };
    const Instance& instance;
    Scheduler& scheduler;
    vk::Device device{};
    u64 memory{}, buffer = 1;
    std::vector<u8> data;
    u8* mapped;
    u64 stream_buffer_size;
    BufferType type = BufferType::Stream;
    u32 offset{}, mapped_size{};
    bool is_coherent = true;
    std::vector<Watch> current_watches = std::vector<Watch>(64);
    std::vector<Watch> previous_watches = std::vector<Watch>(64);
    std::size_t current_watch_cursor{}, wait_cursor{};
    std::optional<std::size_t> invalidation_mark;
    u64 wait_bound{};

    StreamBuffer(const Instance& i, Scheduler& s, u32 bytes)
        : instance(i), scheduler(s), data(bytes), mapped(data.data()), stream_buffer_size(bytes) {
        s.driver.data = &data;
    }
    std::tuple<u8*, u32, bool> Map(u32, u64);
    void Commit(u32);
    void ReserveWatches(std::vector<Watch>&, std::size_t);
    void WaitPendingOperations(u64);
    u64 Handle() const { return buffer; }
};
#include "production.inc"

// CodexAstraLocal: Selection/token bookkeeping is a counter model; only the
// selected pipeline/descriptor recording statements are production extracts.
struct SelectedPipeline { u64 Handle() const { return 3; } };
struct PipelineCache {
    struct CpuFragmentToken {
        u64 tick{};
        explicit operator bool() const { return tick != 0; }
    };
    bool optional{};
    Scheduler& scheduler;
    SelectedPipeline owner;
    SelectedPipeline* bound_pipeline{};
    std::unique_ptr<u64> pipeline_layout = std::make_unique<u64>(1);
    bool BindPipeline(u32&, bool, void*, bool, void*, u32*, CpuFragmentToken* token) {
        ++scheduler.driver.selections;
        if (token) {
            *token = optional ? CpuFragmentToken{scheduler.CurrentTick()} : CpuFragmentToken{};
        }
        const bool is_dirty = scheduler.IsStateDirty(StateFlags::Pipeline);
        auto* selected = &owner;
        std::array<u64, 3> descriptor_sets{};
        std::array<u32, 3> offsets{};
        scheduler.Record([this, is_dirty, selected, descriptor_sets, offsets](vk::CommandBuffer cmdbuf) {
#include "bind-commands.inc"
        });
        scheduler.MarkStateNonDirty(StateFlags::Pipeline | StateFlags::DescriptorSets |
                                    StateFlags::FragmentConstants);
        return true;
    }
    void CompleteReadyCpuDraw(CpuFragmentToken token) {
        Require(token.tick == scheduler.CurrentTick(), "selected owner tick matches final CPU draw");
        ++scheduler.driver.completions;
    }
};

// CodexAstraLocal: Synthetic records witness byte preservation only, not the
// actual HardwareVertex arithmetic/ABI already covered by other gates.
struct HardwareVertex { std::array<u8, 88> bytes; };
static_assert(sizeof(HardwareVertex) == 88);
struct Raster {
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    StreamBuffer& stream_buffer;
    PipelineCache pipeline_cache;
    std::vector<HardwareVertex> vertex_batch;
    Framebuffer framebuffer;
    Common::Rectangle<u32> draw_rect{0, 32, 32, 0};
    u32 pipeline_info{}, software_layout{};
    struct { void* ready{}; } cpu_bridge;

    // CodexAstraLocal: The original sample call sites execute below, while
    // this owner only records their ticks and does not allocate a GPU query.
    struct ComputeQueries {
        Scheduler& scheduler;
        void BeginSample(int slot) {
            if (slot >= 0) scheduler.driver.query_begin_ticks.push_back(scheduler.CurrentTick());
        }
        void EndSample(int slot) {
            if (slot >= 0) scheduler.driver.query_end_ticks.push_back(scheduler.CurrentTick());
        }
    };
    void Payload(u32 count) {
        auto& driver = scheduler.driver;
        const u8 pattern = static_cast<u8>(0x31 + driver.expected.size());
        driver.expected.emplace_back(pattern, count);
        vertex_batch.resize(count);
        std::memset(vertex_batch.data(), pattern, count * 88);
    }
    void Draw(u32 count, bool sampled, int mutant) {
        Payload(count);
        const bool accelerate = false;
        const auto* framebuffer = &this->framebuffer;
        const int timing_slot = sampled ? 0 : -1;
        ComputeQueries queries{scheduler};
        auto* compute_rect = &queries;
        if (mutant == 1) {
#include "late-prepare.inc"
#include "setup.inc"
#include "late-tail.inc"
        } else if (mutant == 2) {
#include "early-prepare.inc"
#include "setup.inc"
#include "early-tail.inc"
        } else {
#include "candidate-prepare.inc"
#include "setup.inc"
#include "candidate-tail.inc"
        }
        // CodexAstraLocal: Query pairing is modeled tick accounting, not query
        // execution. The storage watch must nevertheless name the final draw tick.
#include "end-sample.inc"
        scheduler.driver.draw_ticks.push_back(scheduler.CurrentTick());
        Require(stream_buffer.current_watch_cursor != 0, "draw allocation recorded");
        Require(stream_buffer.current_watches[stream_buffer.current_watch_cursor - 1].tick ==
                    scheduler.CurrentTick(), "geometry watch stamped at final draw tick");
        vertex_batch.clear();
    }
};
} // namespace Vulkan

// CodexAstraLocal: Preserve every draw in six wrap/pass situations with both
// sampled and optional-owner states; two source-derived defects must fail.
int main(int argc, char** argv) {
    try {
        using namespace Vulkan;
        const int mutant = argc > 1 ? std::stoi(argv[1]) : 0;
        unsigned cases{};
        for (bool sampled : {false, true}) {
            for (bool optional : {false, true}) {
                for (const std::string name : {"no_wrap", "old_pending", "old_complete",
                                               "current_wrap", "mali_pass_change", "wrap_and_mali"}) {
                    Driver driver;
                    Instance instance;
                    Scheduler scheduler(driver);
                    RenderManager manager{instance, scheduler};
                    scheduler.on_submit = [&] { manager.EndRendering(); };
                    StreamBuffer buffer{instance, scheduler, 8192};
                    Raster raster{scheduler, manager, buffer, PipelineCache{optional, scheduler}};
                    const bool wraps = name == "old_pending" || name == "old_complete" ||
                                       name == "current_wrap" || name == "wrap_and_mali";
                    raster.Draw(wraps ? 90 : 3, false, mutant);
                    raster.Draw(3, false, mutant);
                    if (name == "old_pending" || name == "old_complete") {
                        scheduler.Flush();
                        if (name == "old_complete") {
                            scheduler.Drain();
                            scheduler.master_semaphore->Wait(1);
                        }
                    }
                    if (name == "mali_pass_change" || name == "wrap_and_mali") {
                        instance.should_flush = true;
                        manager.num_draws = MinDrawsToFlush + 1;
                        raster.framebuffer.handle = 3;
                    }
                    raster.Draw(3, sampled, mutant);
                    Require(manager.pass.render_pass != 0 &&
                                !scheduler.IsStateDirty(StateFlags::Pipeline), "final owner state ready");
                    scheduler.DispatchWork();
                    scheduler.Drain();
                    Require(driver.draws.size() == 3 && driver.expected.size() == 3,
                            "three complete original draws retained");
                    for (const auto& draw : driver.draws) {
                        Require(draw.pass && draw.pipeline && draw.bytes,
                                "valid draw with unique original bytes");
                    }
                    Require(driver.selections == 3, "one pipeline selection per original draw");
                    Require(driver.completions == (optional ? 3U : 0U),
                            "only actual optional draws complete once");
                    Require(driver.query_begin_ticks.size() == (sampled ? 1U : 0U) &&
                                driver.query_end_ticks.size() == driver.query_begin_ticks.size(),
                            "sample begins and ends once");
                    if (sampled) {
                        Require(driver.query_begin_ticks[0] == driver.query_end_ticks[0] &&
                                    driver.query_end_ticks[0] == driver.draw_ticks.back(),
                                "sample stays on final draw tick");
                    }
                    std::cout << "CASE " << name << " sampled=" << sampled << " optional=" << optional
                              << " tick=" << scheduler.CurrentTick()
                              << " bytes=exact draws=3 selects=3 watch_tick=" << driver.draw_ticks.back()
                              << " last_cb=" << driver.draws.back().cb
                              << " vertex_count=" << driver.draws.back().vertices << '\n';
                    // CodexAstraLocal: Retain the finite queue trace as evidence
                    // of command ordering; it is not runtime diagnostic output.
                    for (const auto& event : driver.events) std::cout << " EVENT " << event << '\n';
                    ++cases;
                }
            }
        }
        std::cout << "PASS cases=" << cases << " checks=" << checks << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
