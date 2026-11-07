// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Recording Vulkan endpoints execute the actual production
// pass bodies without a driver. This validates ordering/guards, not image parity.
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "common/math_util.h"
#include "video_core/renderer_vulkan/uberhar_eye_policy.h"

namespace vk {
using Framebuffer = u64;
using RenderPass = u64;
using Image = u64;
using PipelineStageFlags = u32;
using AccessFlags = u32;
using ImageAspectFlags = u32;
struct Offset2D { s32 x{}, y{}; bool operator==(const Offset2D&) const = default; };
struct Extent2D { u32 width{}, height{}; bool operator==(const Extent2D&) const = default; };
struct Rect2D { Offset2D offset; Extent2D extent; bool operator==(const Rect2D&) const = default; };
struct ClearValue { std::array<u32, 4> words{}; };
struct ImageAspectFlagBits { static constexpr u32 eColor = 1, eDepth = 2; };
struct PipelineStageFlagBits {
    static constexpr u32 eFragmentShader = 1, eColorAttachmentOutput = 2,
                         eEarlyFragmentTests = 4, eLateFragmentTests = 8, eTransfer = 16;
};
struct AccessFlagBits {
    static constexpr u32 eShaderWrite = 1, eColorAttachmentWrite = 2,
                         eDepthStencilAttachmentWrite = 4, eShaderRead = 8, eTransferRead = 16;
};
enum class ImageLayout { eGeneral };
enum class DependencyFlagBits { eByRegion };
enum class SubpassContents { eInline };
enum class DriverId { eMesaTurnip, eOther };
struct ImageSubresourceRange {
    ImageAspectFlags aspectMask{};
    u32 baseMipLevel{}, levelCount{}, baseArrayLayer{}, layerCount{};
};
struct ImageMemoryBarrier {
    AccessFlags srcAccessMask{}, dstAccessMask{};
    ImageLayout oldLayout{}, newLayout{};
    u32 srcQueueFamilyIndex{}, dstQueueFamilyIndex{};
    Image image{};
    ImageSubresourceRange subresourceRange;
};
struct RenderPassBeginInfo {
    RenderPass renderPass{};
    Framebuffer framebuffer{};
    Rect2D renderArea;
    u32 clearValueCount{};
    const ClearValue* pClearValues{};
};
// CodexAstraLocal: Commands keep their production captures until explicit drain;
// copied clear/barrier bytes expose owner mutation and previous-target mistakes.
struct Commands {
    struct Begin { RenderPass pass; Framebuffer framebuffer; Rect2D area; u32 clears; ClearValue clear; };
    std::vector<Begin> begins;
    std::vector<ImageMemoryBarrier> barriers;
    std::vector<char> order;
    unsigned ends{};
};
struct CommandBuffer {
    Commands& commands;
    void beginRenderPass(const RenderPassBeginInfo& in, SubpassContents) {
        commands.begins.push_back({in.renderPass, in.framebuffer, in.renderArea,
                                  in.clearValueCount, *in.pClearValues});
        commands.order.push_back('B');
    }
    void endRenderPass() { ++commands.ends; commands.order.push_back('E'); }
    void pipelineBarrier(PipelineStageFlags, PipelineStageFlags, DependencyFlagBits,
                         u32, const void*, u32, const void*, u32 count,
                         const ImageMemoryBarrier* barriers) {
        commands.barriers.insert(commands.barriers.end(), barriers, barriers + count);
        commands.order.push_back('M');
    }
};
}
constexpr u64 VK_NULL_HANDLE = 0;
constexpr u32 VK_QUEUE_FAMILY_IGNORED = ~u32{};
constexpr u32 VK_REMAINING_ARRAY_LAYERS = ~u32{};
namespace Settings {
// CodexAstraLocal: Supply only the existing driver-policy setting endpoint.
struct Flag { bool value{}; bool GetValue() const { return value; } };
inline struct Values { Flag turnip_pass_end_flush; } values;
}
namespace Vulkan {
#include "actual_pass_types.inc"
struct Instance {
    bool flush{};
    vk::DriverId driver{vk::DriverId::eOther};
    bool ShouldFlush() const { return flush; }
    vk::DriverId GetDriverID() const { return driver; }
};
struct Scheduler {
    vk::Commands commands;
    std::vector<std::function<void(vk::CommandBuffer)>> pending;
    unsigned flushes{};
    template<class F> void Record(F&& fn) { pending.emplace_back(std::forward<F>(fn)); }
    void Drain() {
        auto captured = std::move(pending);
        pending.clear();
        for (auto& fn : captured) fn(vk::CommandBuffer{commands});
    }
    void Flush() { ++flushes; }
};
struct Framebuffer {
    u64 framebuffer{1}, render_pass{2};
    std::array<vk::Image, 2> images{11, 12};
    std::array<vk::ImageAspectFlags, 2> aspects{1, 2};
    bool shadow_rendering{};
    auto Handle() const { return framebuffer; }
    auto RenderPass() const { return render_pass; }
    auto Images() const { return images; }
    auto Aspects() const { return aspects; }
};
// CodexAstraLocal: Keep only data consumed by extracted production methods.
// Diagnostic endpoints count invocation but allocate no queries or GPU timings.
struct RenderManager {
    Instance instance;
    Scheduler scheduler;
    RenderPass pass{};
    std::array<vk::Image, 2> images{};
    std::array<vk::ImageAspectFlags, 2> aspects{};
    bool shadow_rendering{}, containment_eligible{}, reuse_contained_pass{true};
    bool diagnostics_enabled{true};
    u32 num_draws{};
    u64 contained_pass_reuses{}, containment_refusals{}, containment_submission_boundaries{};
    u64 diagnostic_reuses{}, diagnostic_feedback_requests{}, diagnostic_flushes{};
    std::array<u64, static_cast<std::size_t>(PassEndReason::Count)> diagnostic_end_reasons{};
    struct Sample { PassEndReason end_reason{}; };
    std::array<Sample, 1> diagnostic_samples{};
    int diagnostic_active{-1};
    unsigned diagnostic_begins{}, diagnostic_ends{};
    void BeginDiagnosticPass(const RenderPass&, bool) { ++diagnostic_begins; }
    void EndDiagnosticPass() { ++diagnostic_ends; }
    bool ShouldFlushPass() const;
    void BeginRendering(const Framebuffer*, Common::Rectangle<u32>, bool = false);
    void BeginRendering(const RenderPass&, bool = false, bool = false);
    void EndRendering(PassEndReason = PassEndReason::Explicit);
};
#include "actual_pass_methods.inc"
}

namespace {
unsigned checks{};
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
Vulkan::RenderPass Area(s32 x, s32 y, u32 width, u32 height) {
    return {1, 2, {{x, y}, {width, height}}, {}, 0};
}
void PassChecks() {
    using namespace Vulkan;
    const auto a = Area(0, 0, 100, 100), b = Area(10, 10, 20, 20), c = Area(70, 70, 20, 20);
    {
        RenderManager m;
        m.BeginRendering(a, true, true);
        m.BeginRendering(b, true, true);
        m.BeginRendering(c, true, true);
        Require(m.pass == a, "A must remain active through B/C, including C outside B");
        Require(m.contained_pass_reuses == 2 && m.num_draws == 2, "contained draw accounting");
        Require(m.diagnostic_begins == 1 && m.diagnostic_ends == 0, "contained pass stays open");
        m.scheduler.Drain();
        Require(m.scheduler.commands.begins.size() == 1 &&
                m.scheduler.commands.begins[0].area == a.render_area, "immutable original queued area");
        m.BeginRendering(a, true, true);
        Require(m.diagnostic_reuses == 1, "exact reuse retains original behavior");
    }
    // CodexAstraLocal: Each guard is varied alone; a refusal must end then begin,
    // rather than only incrementing a counter and leaving stale attachments open.
    for (unsigned variant = 0; variant < 10; ++variant) {
        RenderManager m;
        auto first = a, next = b;
        bool first_framebuffer = true, first_allowed = true, next_framebuffer = true, next_allowed = true;
        switch (variant) {
        case 0: m.reuse_contained_pass = false; break;
        case 1: first.do_clear = 1; break;
        case 2: next.do_clear = 1; next.clear.words[0] = 99; break;
        case 3: ++next.framebuffer; break;
        case 4: ++next.render_pass; break;
        case 5: first_framebuffer = false; break;
        case 6: next_framebuffer = false; break;
        case 7: first_allowed = false; break;
        case 8: next_allowed = false; break;
        case 9: next.render_area.offset.x = 90; break;
        }
        m.BeginRendering(first, first_framebuffer, first_allowed);
        m.BeginRendering(next, next_framebuffer, next_allowed);
        m.scheduler.Drain();
        Require(m.contained_pass_reuses == 0, "guard cannot merge");
        Require(m.pass == next && m.scheduler.commands.ends == 1 &&
                m.scheduler.commands.begins.size() == 2, "guard preserves end/begin boundary");
        Require(m.scheduler.commands.order == std::vector<char>({'B', 'E', 'B'}), "guard command order");
        Require(m.scheduler.commands.begins.back().clears == next.do_clear &&
                m.scheduler.commands.begins.back().clear.words == next.clear.words, "clear values retained");
    }
    {
        RenderManager m;
        m.BeginRendering(a, true, true);
        m.BeginRendering(a, true, false);
        Require(!m.containment_eligible, "exact-area hazardous draw revokes containment");
        m.BeginRendering(b, true, true);
        Require(m.contained_pass_reuses == 0 && m.pass == b, "hazard stays revoked until boundary");
    }
    for (unsigned kind = 0; kind < 2; ++kind) {
        RenderManager m;
        Framebuffer fb;
        fb.shadow_rendering = kind == 0;
        m.BeginRendering(&fb, {0, 100, 100, 0}, kind == 1);
        Require(!m.containment_eligible, "shadow/storage framebuffer wrapper guard");
        fb.shadow_rendering = false;
        m.BeginRendering(&fb, {10, 30, 30, 10});
        m.scheduler.Drain();
        Require(m.contained_pass_reuses == 0 && m.scheduler.commands.ends == 1, "hazard wrapper cannot merge");
        Require(m.scheduler.commands.barriers.size() == 2, "previous attachments receive barriers");
        Require(m.scheduler.commands.barriers[0].srcAccessMask ==
                (kind == 0 ? vk::AccessFlagBits::eShaderWrite : vk::AccessFlagBits::eColorAttachmentWrite),
                "end boundary uses previous shadow metadata");
    }
    {
        RenderManager m;
        Framebuffer old_fb, new_fb;
        new_fb.framebuffer = 3; new_fb.images = {21, 22};
        m.BeginRendering(&old_fb, {0, 100, 100, 0});
        m.BeginRendering(&new_fb, {0, 100, 100, 0});
        m.scheduler.Drain();
        Require(m.scheduler.commands.barriers[0].image == 11 &&
                m.scheduler.commands.barriers[1].image == 12, "old attachments captured before replacement");
    }
    for (auto reason : {PassEndReason::Explicit, PassEndReason::Submission, PassEndReason::Feedback}) {
        RenderManager m;
        m.BeginRendering(a, true, true);
        m.EndRendering(reason);
        Require(!m.pass.render_pass && !m.containment_eligible, "explicit boundary clears eligibility");
        m.BeginRendering(b, true, true);
        m.scheduler.Drain();
        Require(m.contained_pass_reuses == 0 && m.scheduler.commands.ends == 1, "explicit boundary never crosses");
        Require(m.diagnostic_end_reasons[static_cast<std::size_t>(reason)] == 1, "boundary reason preserved");
    }
    {
        RenderManager m;
        m.EndRendering(PassEndReason::Feedback);
        Require(m.diagnostic_feedback_requests == 1 && m.scheduler.pending.empty(), "empty feedback boundary harmless");
        m.instance.flush = true;
        m.BeginRendering(a, true, true);
        m.num_draws = MinDrawsToFlush;
        m.BeginRendering(b, true, true);
        Require(m.contained_pass_reuses == 1 && m.scheduler.flushes == 0, "flush budget strict lower boundary");
        m.BeginRendering(c, true, true);
        Require(m.containment_submission_boundaries == 1 && m.scheduler.flushes == 1 &&
                m.num_draws == 0 && m.pass == c, "submission budget blocks contained reuse and flushes");
    }
    {
        RenderManager m;
        m.BeginRendering(a, true, true); m.num_draws = MinDrawsToFlush + 1;
        m.BeginRendering(b, true, true);
        Require(m.contained_pass_reuses == 1 && m.scheduler.flushes == 0, "driver policy preserves no-flush path");
        m.instance.flush = true;
        m.BeginRendering(a, true, true);
        Require(m.scheduler.flushes == 0, "exact reuse must retain legacy submission behavior");
    }
    {
        RenderManager m;
        m.instance.driver = vk::DriverId::eMesaTurnip;
        Settings::values.turnip_pass_end_flush.value = true;
        Require(m.ShouldFlushPass(), "Turnip explicit pass submission policy");
        m.instance.driver = vk::DriverId::eOther;
        Require(!m.ShouldFlushPass(), "Turnip option cannot change another driver's policy");
        Settings::values.turnip_pass_end_flush.value = false;
    }
    for (unsigned edge = 0; edge < 6; ++edge) {
        RenderManager m;
        auto outer = Area(-10, -10, 100, 100), inner = Area(-10, -10, 100, 100);
        switch (edge) {
        case 0: inner.render_area.extent.width = 99; break;
        case 1: --inner.render_area.offset.x; break;
        case 2: --inner.render_area.offset.y; break;
        case 3: ++inner.render_area.extent.width; break;
        case 4: ++inner.render_area.extent.height; break;
        case 5:
            outer = Area(std::numeric_limits<s32>::max() - 10, 0, 40, 40);
            inner = Area(std::numeric_limits<s32>::max() - 5, 0, 5, 10); break;
        }
        m.BeginRendering(outer, true, true);
        m.BeginRendering(inner, true, true);
        Require(m.contained_pass_reuses == (edge == 0 || edge == 5 ? 1u : 0u), "signed/extent containment edge");
    }
}
void EyeChecks() {
    using namespace Vulkan::UnusedEyePolicy;
    const Input good{0x5000, 0x5000, 400, 240, 400, 240, {0x1000, 0x2000}, true, true,
                     false, false, false, false};
    Require(Classify(good) == Decision::Eligible, "dedicated right scanout eligible");
    for (unsigned flags = 0; flags < 64; ++flags) {
        auto in = good;
        in.mono_left = flags & 1; in.allowed = flags & 2;
        in.writes_depth_stencil = flags & 4; in.geometry = flags & 8;
        in.shadow = flags & 16; in.feedback = flags & 32;
        const auto expected = (flags & 3) != 3 ? Decision::Disabled :
                              (flags & 60) ? Decision::SharedState : Decision::Eligible;
        Require(Classify(in) == expected, "eye enable/depth/geometry/shadow/feedback guard");
    }
    for (unsigned invalid = 0; invalid < 9; ++invalid) {
        auto in = good;
        switch (invalid) {
        case 0: in.color = 0; break;
        case 1: in.right = 0; break;
        case 2: ++in.right; break;
        case 3: in.left[0] = in.color; break;
        case 4: in.left[1] = in.color; break;
        case 5: in.width = in.scan_width = 0; break;
        case 6: in.height = in.scan_height = 0; break;
        case 7: ++in.scan_width; break;
        case 8: ++in.scan_height; break;
        }
        Require(Classify(in) == (invalid < 5 ? Decision::NotRight : Decision::Shape), "eye address/shape guard");
    }
}
}
int main() {
    try {
        PassChecks(); EyeChecks();
        std::cout << "PASS production pass containment and early-eye policy: " << checks << " checks\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
