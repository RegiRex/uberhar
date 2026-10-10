// CodexAstraLocal: Exercise production ring and cached-upload methods with real
// PICA data types. The scheduler deliberately holds consumers until a checked
// tick wait; this is a lifetime/control proof, not Vulkan execution or timing.
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "common/alignment.h"
#include "video_core/pica/pica_core.h"
#include "video_core/shader/generator/shader_uniforms.h"
#define private public
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#undef private

// CodexAstraLocal: Named failures let the driver distinguish a sensitive
// lifetime/invalidation defect from compilation failures and unrelated crashes.
static unsigned checks{};
static void Require(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
#undef ASSERT
#undef ASSERT_MSG
#define ASSERT(x) Require(bool(x), "production assertion: " #x)
#define ASSERT_MSG(x, ...) ASSERT(x)

namespace Vulkan {
constexpr u64 WATCHES_RESERVE_CHUNK = 8;
class Instance {
public:
    u64 NonCoherentAtomSize() const { return 16; }
};

// CodexAstraLocal: Execute queued readers before a successful wait returns.
// Current-tick waits model the real scheduler's submit-before-wait behavior.
class Scheduler {
public:
    u64 tick{1}, completed{};
    std::vector<u64> waits;
    std::vector<std::pair<u64, std::function<void()>>> draws;
    u64 CurrentTick() const { return tick; }
    void Record(std::function<void()> work) { draws.emplace_back(tick, std::move(work)); }
    void Wait(u64 target) {
        waits.push_back(target);
        if (target <= completed) return;
        if (target >= tick) tick = target + 1;
        for (auto& [at, work] : draws) {
            if (work && at <= target) { work(); work = {}; }
        }
        completed = target;
    }
};

// CodexAstraLocal: Only allocation and Vulkan visibility endpoints are replaced;
// Map/Commit/MarkDrawUse/watches are exact extracted production definitions.
StreamBuffer::StreamBuffer(const Instance& i, Scheduler& s, vk::BufferUsageFlags u,
                           u64 n, BufferType t)
    : instance{i}, scheduler{s}, stream_buffer_size{n}, usage{u}, type{t}, is_coherent{true} {
    mapped = new u8[n]{};
    ReserveWatches(current_watches, 8);
    ReserveWatches(previous_watches, 8);
}
StreamBuffer::~StreamBuffer() { delete[] mapped; }
#include "ring.inc"

using namespace Pica::Shader::Generator;
constexpr u32 UniformAlignment = 16;
constexpr u32 UniformSize = Common::AlignUp<u32>(sizeof(VSPicaUniformData), UniformAlignment) +
                            Common::AlignUp<u32>(sizeof(VSUniformData), UniformAlignment) +
                            Common::AlignUp<u32>(sizeof(FSUniformData), UniformAlignment);
constexpr u32 ProcSize = sizeof(Common::Vec2f) * 128 * 3 + sizeof(Common::Vec4f) * 256 * 2;
constexpr u32 LightFogSize = sizeof(Common::Vec2f) * (256 * Pica::LightingRegs::NumLightingSampler + 128);

// CodexAstraLocal: Cache the actual three UBO offsets and real prepared LUT
// offsets. A wrap must refresh every block, even when only one was dirty.
struct RasterizerVulkan {
    struct State {
        Pica::PicaCore::Lighting lighting{};
        Pica::PicaCore::Fog fog{};
        Pica::PicaCore::ProcTex proctex{};
        Pica::ShaderSetup vs_setup{};
    } pica;
    struct Pipeline {
        std::array<u32, 3> offsets{};
        void UpdateRange(u32 slot, u32 offset) { offsets.at(slot) = offset; }
    } pipeline_cache;
    VSUniformData vs_data{};
    FSUniformData fs_data{};
    bool vs_data_dirty{true}, fs_data_dirty{true};
    const u32 uniform_buffer_alignment{UniformAlignment};
    const u32 uniform_size_aligned_vs_pica{Common::AlignUp<u32>(sizeof(VSPicaUniformData), UniformAlignment)};
    const u32 uniform_size_aligned_vs{Common::AlignUp<u32>(sizeof(VSUniformData), UniformAlignment)};
    const u32 uniform_size_aligned_fs{Common::AlignUp<u32>(sizeof(FSUniformData), UniformAlignment)};
    StreamBuffer uniform_buffer, texture_buffer, texture_lf_buffer;
    RasterizerVulkan(const Instance& i, Scheduler& s)
        : uniform_buffer{i, s, 0, UniformSize * 2},
          texture_buffer{i, s, 0, ProcSize * 2},
          texture_lf_buffer{i, s, 0, LightFogSize * 2} {}
    void SyncAndUploadLUTs();
    void SyncAndUploadLUTsLF();
    void UploadUniforms(bool accelerate_draw);
    void MarkCachedShaderBuffersUsed();
};
#include "uploads.inc"

// CodexAstraLocal: A read list snapshots exact consumed ranges without retaining
// a second implementation of register conversion or changing production offsets.
struct Range {
    StreamBuffer* ring;
    u32 offset;
    std::vector<u8> expected;
};
static std::vector<Range> Ranges(RasterizerVulkan& r, unsigned kind) {
    std::vector<Range> out;
    auto add = [&](StreamBuffer& ring, u32 offset, u32 bytes) {
        Require(offset + bytes <= ring.stream_buffer_size, "range in ring");
        out.push_back({&ring, offset, {ring.mapped + offset, ring.mapped + offset + bytes}});
    };
    if (kind == 0) {
        add(r.uniform_buffer, r.pipeline_cache.offsets[0], sizeof(VSPicaUniformData));
        add(r.uniform_buffer, r.pipeline_cache.offsets[1], sizeof(VSUniformData));
        add(r.uniform_buffer, r.pipeline_cache.offsets[2], sizeof(FSUniformData));
    } else if (kind == 1) {
        for (int offset : {r.fs_data.proctex_noise_lut_offset, r.fs_data.proctex_color_map_offset,
                           r.fs_data.proctex_alpha_map_offset})
            add(r.texture_buffer, offset * sizeof(Common::Vec2f), 128 * sizeof(Common::Vec2f));
        for (int offset : {r.fs_data.proctex_lut_offset, r.fs_data.proctex_diff_lut_offset})
            add(r.texture_buffer, offset * sizeof(Common::Vec4f), 256 * sizeof(Common::Vec4f));
    } else {
        for (unsigned i = 0; i < Pica::LightingRegs::NumLightingSampler; ++i)
            add(r.texture_lf_buffer, r.fs_data.lighting_lut_offset[i / 4][i % 4] * sizeof(Common::Vec2f),
                256 * sizeof(Common::Vec2f));
        add(r.texture_lf_buffer, r.fs_data.fog_lut_offset * sizeof(Common::Vec2f), 128 * sizeof(Common::Vec2f));
    }
    return out;
}
static void CheckRanges(const std::vector<Range>& ranges) {
    for (const auto& range : ranges)
        Require(std::memcmp(range.ring->mapped + range.offset, range.expected.data(),
                            range.expected.size()) == 0, "queued cached draw bytes overwritten");
}
static void Seed(RasterizerVulkan& r, u32 value) {
    // CodexAstraLocal: Distinct nonzero table members expose partial refreshes;
    // use real source encodings and unchanged upload conversions.
    for (unsigned t = 0; t < r.pica.lighting.luts.size(); ++t)
        for (unsigned i = 0; i < 256; ++i) r.pica.lighting.luts[t][i].raw = (value + t * 19 + i) & 0xffffff;
    for (unsigned i = 0; i < 128; ++i) {
        r.pica.fog.lut[i].raw = (value + i * 7) & 0xffffff;
        r.pica.proctex.noise_table[i].raw = value + i;
        r.pica.proctex.color_map_table[i].raw = value + i * 3;
        r.pica.proctex.alpha_map_table[i].raw = value + i * 5;
    }
    for (unsigned i = 0; i < 256; ++i) {
        r.pica.proctex.color_table[i].raw = 0x80402010 + value + i;
        r.pica.proctex.color_diff_table[i].raw = 0x03020100 + value + i;
    }
    r.vs_data.enable_clip1 = value & 1;
    r.fs_data.alphatest_ref = value & 255;
    r.pica.vs_setup.uniforms.b[0] = (value & 2) != 0;
    r.pica.vs_setup.uniforms_dirty = true;
}
static void Upload(RasterizerVulkan& r) {
    r.SyncAndUploadLUTs(); r.SyncAndUploadLUTsLF(); r.UploadUniforms(true);
}

static void Lifetime(unsigned kind, bool pass_flush, bool already_complete) {
    Instance instance;
    Scheduler scheduler;
    RasterizerVulkan r{instance, scheduler};
    Seed(r, 57); Upload(r);
    auto& ring = kind == 0 ? r.uniform_buffer : kind == 1 ? r.texture_buffer : r.texture_lf_buffer;
    // CodexAstraLocal: Fill unused space at the original upload tick so the
    // later dirty upload must wrap, but has no newer ordinary watch to save it.
    auto [unused, unused_offset, invalidated] = ring.Map(ring.stream_buffer_size - ring.offset, 1);
    Require(!invalidated, "nonwrap filler");
    std::memset(unused, 0xa5, ring.mapped_size);
    ring.Commit(ring.mapped_size);
    auto original = Ranges(r, kind);
    scheduler.Record([original] { CheckRanges(original); });
    r.MarkCachedShaderBuffersUsed();
    scheduler.Wait(1);
    Require(scheduler.completed == 1, "first consumer completed");
    const auto cursor = ring.current_watch_cursor;
    const auto waits = scheduler.waits.size();
    Upload(r); // Exact clean fast paths: no fresh Commit/watch exists.
    Require(ring.current_watch_cursor == cursor, "clean reuse has no upload watch");
    Require(scheduler.waits.size() == waits, "clean reuse does not wait");
    if (pass_flush) scheduler.tick = 4; // Final pass/index Map advanced the tick.
    const auto consume_tick = scheduler.CurrentTick();
    scheduler.Record([original] { CheckRanges(original); });
    r.MarkCachedShaderBuffersUsed();
    if (already_complete) scheduler.Wait(consume_tick);
    // CodexAstraLocal: Change only one member; wrap must reproduce all other
    // cached blocks. Compare with a fresh full upload of the same logical state.
    if (kind == 0) { r.fs_data.alphatest_ref = 211; r.fs_data_dirty = true; }
    if (kind == 1) { r.pica.proctex.color_table[0].raw ^= 0x777; r.pica.proctex.lut_dirty.Assign(1); }
    if (kind == 2) { r.pica.fog.lut[0].raw ^= 0x777; r.pica.fog.lut_dirty = true; }
    RasterizerVulkan expected{instance, scheduler};
    expected.pica = r.pica; expected.vs_data = r.vs_data; expected.fs_data = r.fs_data;
    expected.pica.lighting.lut_dirty = expected.pica.lighting.LutAllDirty;
    expected.pica.fog.lut_dirty = true; expected.pica.proctex.table_dirty = expected.pica.proctex.TableAllDirty;
    expected.pica.vs_setup.uniforms_dirty = true;
    Upload(r);
    // CodexAstraLocal: A missing last-use wait deterministically exposes the
    // overwritten bytes to the still-queued reader, not a timing-sensitive race.
    scheduler.Wait(consume_tick);
    Upload(expected);
    const auto actual_ranges = Ranges(r, kind), expected_ranges = Ranges(expected, kind);
    Require(actual_ranges.size() == expected_ranges.size(), "same cached block population");
    for (std::size_t i = 0; i < actual_ranges.size(); ++i)
        Require(actual_ranges[i].expected == expected_ranges[i].expected, "all cached blocks refreshed at wrap");
    Require(ring.last_draw_use_tick == 0, "consumed fence retired at wrap");
}

static void NonWrap() {
    Instance i; Scheduler s; StreamBuffer ring{i, s, 0, 128};
    auto [data, position, invalidated] = ring.Map(16, 16);
    std::memset(data, 9, 16); ring.Commit(16); ring.MarkDrawUse();
    const auto cursor = ring.current_watch_cursor;
    for (unsigned n = 0; n < 100; ++n) { s.tick = n + 2; ring.MarkDrawUse(); }
    Require(ring.current_watch_cursor == cursor && s.waits.empty(), "last use has no per-draw watch/wait");
    auto [next, offset, wrap] = ring.Map(16, 16);
    Require(!wrap && s.waits.empty() && offset == 16, "nonwrap map adds no lifetime wait");
    ring.Commit(16);
    // CodexAstraLocal: Unmarked geometry/staging retain their original waits.
    StreamBuffer ordinary{i, s, 0, 32};
    ordinary.Map(32, 1); ordinary.Commit(32); const auto original_tick = s.tick;
    s.tick += 10; const auto before = s.waits.size(); ordinary.Map(1, 1);
    Require(s.waits.size() == before + 1 && s.waits.back() == original_tick,
            "unmarked ring retains original watch");
}
} // namespace Vulkan

int main() {
    try {
        for (unsigned kind = 0; kind < 3; ++kind)
            for (bool pass_flush : {false, true})
                for (bool complete : {false, true}) Vulkan::Lifetime(kind, pass_flush, complete);
        Vulkan::NonWrap();
        std::cout << "PASS cached lifetime cases=12 checks=" << checks << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n'; return 23;
    }
}
