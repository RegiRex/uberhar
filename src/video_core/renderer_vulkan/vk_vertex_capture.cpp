// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>
#include <json.hpp>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#if defined(ANDROID)
#include "common/android_utils.h"
#endif
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_vertex_capture.h"
#include "video_core/shader/generator/shader_uniforms.h"

// CodexAstraLocal: CMake stamps the fork version; standalone host seam tests
// identify themselves explicitly rather than claiming an installed APK version.
#ifndef UBERHAR_CAPTURE_VERSION
#define UBERHAR_CAPTURE_VERSION "unversioned-host-probe"
#endif

namespace Vulkan::VertexCapture {
namespace {
using namespace Pica::Shader::Generator;
using Json = nlohmann::json;
static_assert(std::endian::native == std::endian::little);

// CodexAstraLocal: Fixed section names carry only defined scalar/guest bytes.
// Registers are the PICA u32 register array, never arbitrary C++ object memory.
constexpr std::array<std::string_view, 24> SectionNames{
    "regs", "program", "swizzle", "defaults", "uniform_f", "uniform_b", "uniform_i",
    "vertex_0", "vertex_1", "vertex_2", "vertex_3", "vertex_4", "vertex_5",
    "vertex_6", "vertex_7", "vertex_8", "vertex_9", "vertex_10", "vertex_11",
    "fixed", "indices", "vs_pica", "vs_extra", "fs"};
struct Section { u32 offset{}, size{}; };
struct Key {
    u64 program{}, swizzle{};
    u32 entry{}, count{}, inputs{}, outputs{}, color{}, depth{};
    bool operator==(const Key&) const = default;
};
struct VertexBindingCopy {
    u32 binding{}, guest_offset{}, guest_stride{}, upload_stride{}, span{};
};
struct PipelineIdentity {
    u64 key{}, source{};
    std::array<u64, 3> shaders{};
    std::array<s32, 4> viewport{};
    std::array<u32, 4> scissor{};
    u32 color_format{}, depth_format{};
    bool specialized{};
    bool operator==(const PipelineIdentity&) const = default;
};
struct Packet {
    Key key;
    u64 swap{};
    u32 interval{}, ordinal{}, minimum{}, maximum{}, vertex_offset{};
    u32 original_width{}, uploaded_width{}, available_attributes{};
    u32 program_words{}, swizzle_words{};
    bool indexed{}, queued{};
    RecordedState execution;
    std::unique_ptr<u8[]> data;
    std::size_t capacity{}, size{};
    std::array<Section, SectionNames.size()> sections{};
    std::array<VertexBindingCopy, 12> bindings{};
    u32 binding_count{};
    std::array<Pica::NativeInputAttribute, 16> native_inputs{};
    BindingState bound;
    PipelineIdentity identity;
};
struct Row {
    Key key;
    PipelineIdentity identity;
    u64 swap{};
    u32 interval{}, ordinal{};
    std::atomic<u32> recorded{};
    std::atomic<u64> first_tick{}, last_tick{};
};
struct Config {
    std::string id;
    bool discover{};
    u64 title{};
    u32 delay{1}, width{4}, per_swap{2}, ordinal{};
    std::optional<u64> program, swizzle;
    std::optional<u32> entry, count;
    std::array<u32, 2> colors{};
    u32 color_count{};
};

// CodexAstraLocal: Exact fixed-width identities avoid rounded JSON numbers.
u64 Hex(const Json& value) {
    const auto& s = value.get_ref<const std::string&>();
    if (s.size() != 16)
        throw std::runtime_error("expected sixteen hex digits");
    u64 result{};
    const auto parsed = std::from_chars(s.data(), s.data() + s.size(), result, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
        throw std::runtime_error("invalid hex value");
    return result;
}

// CodexAstraLocal: Configuration numbers must retain their exact integer type.
u32 Bounded(const Json& object, const char* key, u32 fallback, u32 maximum, u32 minimum = 0) {
    if (!object.contains(key))
        return fallback;
    const auto& v = object.at(key);
    if (!v.is_number_unsigned() || v.get<u64>() < minimum || v.get<u64>() > maximum)
        throw std::runtime_error("configuration bound");
    return v.get<u32>();
}

// CodexAstraLocal: Record selected pipeline context without copying ABI padding.
PipelineIdentity Identity(const BindingState& state) {
    const auto& dynamic = state.pipeline.dynamic_info;
    return {state.pipeline_key, state.source_hash, state.shader_ids,
        {dynamic.viewport.left, dynamic.viewport.top, dynamic.viewport.right, dynamic.viewport.bottom},
        {dynamic.scissor.left, dynamic.scissor.top, dynamic.scissor.right, dynamic.scissor.bottom},
        static_cast<u32>(state.pipeline.state.attachments.color),
        static_cast<u32>(state.pipeline.state.attachments.depth), state.specialized};
}

// CodexAstraLocal: Enumerate ABI semantic fields; padding is never copied from
// host memory. The same ranges are emitted for an independent replay reader.
template <typename F> void UniformRanges(u32 binding, F&& field) {
    if (binding == 0) {
        field(0, 4); field(16, 64); field(80, 1536);
    } else if (binding == 1) {
        field(0, 8); field(16, 16);
    } else {
#define CAPTURE_FS(member) field(offsetof(FSUniformData, member), sizeof(FSUniformData::member))
        CAPTURE_FS(framebuffer_scale); CAPTURE_FS(alphatest_ref); CAPTURE_FS(depth_scale);
        CAPTURE_FS(depth_offset); CAPTURE_FS(shadow_bias_constant); CAPTURE_FS(shadow_bias_linear);
        CAPTURE_FS(scissor_x1); CAPTURE_FS(scissor_y1); CAPTURE_FS(scissor_x2); CAPTURE_FS(scissor_y2);
        CAPTURE_FS(fog_lut_offset); CAPTURE_FS(proctex_noise_lut_offset);
        CAPTURE_FS(proctex_color_map_offset); CAPTURE_FS(proctex_alpha_map_offset);
        CAPTURE_FS(proctex_lut_offset); CAPTURE_FS(proctex_diff_lut_offset);
        CAPTURE_FS(proctex_bias); CAPTURE_FS(shadow_texture_bias); CAPTURE_FS(lighting_lut_offset);
        CAPTURE_FS(fog_color); CAPTURE_FS(proctex_noise_f); CAPTURE_FS(proctex_noise_a);
        CAPTURE_FS(proctex_noise_p); CAPTURE_FS(lighting_global_ambient);
        for (u32 i = 0; i < 8; ++i) {
            const u32 base = offsetof(FSUniformData, light_src) + i * sizeof(LightSrc);
#define CAPTURE_LIGHT(member) field(base + offsetof(LightSrc, member), sizeof(LightSrc::member))
            CAPTURE_LIGHT(specular_0); CAPTURE_LIGHT(specular_1); CAPTURE_LIGHT(diffuse);
            CAPTURE_LIGHT(ambient); CAPTURE_LIGHT(position); CAPTURE_LIGHT(spot_direction);
            CAPTURE_LIGHT(dist_atten_bias); CAPTURE_LIGHT(dist_atten_scale);
#undef CAPTURE_LIGHT
        }
        CAPTURE_FS(const_color); CAPTURE_FS(tev_combiner_buffer_color);
        CAPTURE_FS(tex_lod_bias); CAPTURE_FS(tex_border_color); CAPTURE_FS(blend_color);
#undef CAPTURE_FS
    }
}
} // namespace

// CodexAstraLocal: Stable fixed metadata owns a bounded set of byte buffers.
// Workers only publish atomic evidence; they never allocate, serialize or write.
struct Store {
    std::array<Packet, MaxPackets> packets;
    std::array<Row, MaxRows> rows;
    std::array<u32, 8> rows_per_interval{};
    u32 row_count{};
    std::size_t allocated{}, copied{};
};
// CodexAstraLocal: The writer and store coexist at exit; parsing occurs before
// the writer exists. Leave at least 64 KiB for config/owners/fixed bookkeeping.
static_assert(sizeof(Store) < MetadataReserve / 4);
static_assert(MaxManifestBytes + sizeof(Store) < MetadataReserve * 3 / 4);

struct Session::Impl {
    Config config;
    u64 run{}, title{};
    Window window{0, 1, 0};
    std::shared_ptr<Store> store;
    std::optional<u64> submitted;
    u64 completed{};
    std::string path;
    u32 ordinal{}, attempts{};
    u64 examined{}, filtered{}, capped{}, preparation_failures{}, errors{};
    bool current{}, indexed{}, committed{}, finished{}, identity_valid{true};
    Key key;
    u32 current_ordinal{};
    s32 packet{-1};

    // CodexAstraLocal: Abandoned preparation releases only its own bytes;
    // committed payload remains owned until queued evidence has drained.
    void Abort() noexcept {
        if (packet >= 0 && !store->packets[packet].queued) {
            auto& p = store->packets[packet];
            store->allocated -= p.capacity;
            p.data.reset();
            p.capacity = p.size = 0;
        }
        current = false;
        packet = -1;
    }

    // CodexAstraLocal: Reserve byte/copy budgets before any payload write.
    bool Append(u32 section, const void* data, std::size_t bytes) noexcept {
        if (packet < 0)
            return false;
        auto& p = store->packets[packet];
        if (section >= p.sections.size() || p.sections[section].size ||
            !CanAppend(p.size, bytes, p.capacity) ||
            !CanAppend(store->copied, bytes, MaxCopiedBytes)) {
            ++capped; Abort(); return false;
        }
        p.sections[section] = {static_cast<u32>(p.size), static_cast<u32>(bytes)};
        if (bytes)
            std::memcpy(p.data.get() + p.size, data, bytes);
        p.size += bytes;
        store->copied += bytes;
        return true;
    }
};

// CodexAstraLocal: Called only after the corresponding draw command records;
// this publishes execution evidence without asserting submission/completion.
void Token::Recorded() const noexcept {
    if (!owner || !tick)
        return;
    if (packet >= 0)
        owner->packets[packet].execution.MarkRecorded();
    if (row >= 0) {
        auto& r = owner->rows[row];
        if (r.recorded.load(std::memory_order_relaxed) == 0)
            r.first_tick.store(tick, std::memory_order_relaxed);
        r.last_tick.store(tick, std::memory_order_relaxed);
        r.recorded.fetch_add(1, std::memory_order_release);
    }
}

// CodexAstraLocal: Fixed owner state is created only for a validated opt-in request.
Session::Session() : impl{std::make_unique<Impl>()} {}
Session::~Session() = default;

// CodexAstraLocal: Parse one bounded title-specific request and refuse existing
// artifact IDs. Optional diagnostics cannot escape into normal initialization.
std::unique_ptr<Session> Session::Load(u64 title, u64 run, u32 manual_phase) noexcept {
    try {
        const std::string file = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) +
                                 "uberhar_vertex_capture.json";
        if (!FileUtil::Exists(file))
            return {};
        FileUtil::IOFile input{file, "rb"};
        if (!input.IsOpen() || input.GetSize() > MaxConfigBytes)
            return {};
        std::array<char, MaxConfigBytes + 1> bytes{};
        const auto length = input.ReadBytes(bytes.data(), bytes.size());
        if (length > MaxConfigBytes)
            return {};
        // CodexAstraLocal: Bound parser nodes/depth as well as source bytes;
        // malformed diagnostics cannot allocate an arbitrarily expanded tree.
        u32 nodes{};
        std::array<std::array<std::string, 16>, 5> object_keys;
        std::array<u32, 5> key_counts{};
        auto root = Json::parse(bytes.data(), bytes.data() + length,
            [&](int depth, Json::parse_event_t event, Json& parsed) {
                if (++nodes > 128 || depth > 4)
                    throw std::runtime_error("configuration structure bound");
                // CodexAstraLocal: Duplicate keys cannot silently replace an
                // enable bit or selector after the operator has reviewed it.
                if (event == Json::parse_event_t::object_start && depth < 4)
                    key_counts[depth + 1] = 0;
                if (event == Json::parse_event_t::key) {
                    auto& count = key_counts[depth];
                    auto& keys = object_keys[depth];
                    const auto& key = parsed.get_ref<const std::string&>();
                    if (count == keys.size() ||
                        std::find(keys.begin(), keys.begin() + count, key) != keys.begin() + count)
                        throw std::runtime_error("duplicate configuration key");
                    keys[count++] = key;
                }
                return true;
            });
        if (!root.is_object() || !root.contains("enabled") ||
            !root.at("enabled").is_boolean() || !root.at("enabled").get<bool>())
            return {};
        // CodexAstraLocal: These adapters do not preserve fopen's exclusive x
        // flag. Reject only the optional diagnostic before any capture work;
        // normal emulation/storage retains its existing behavior.
#if defined(_WIN32) || defined(HAVE_LIBRETRO_VFS)
        return {};
#elif defined(ANDROID)
        if (!AndroidUtils::CanUseRawFS())
            return {};
#endif
        if (!root.contains("schema") || !root.at("schema").is_number_unsigned() ||
            root.at("schema").get<u64>() != 1 ||
            root.value("trigger", "") != "next_gameplay_transition")
            return {};
        Config cfg;
        cfg.title = Hex(root.at("title_id"));
        if (cfg.title != title || !title)
            return {};
        cfg.id = root.at("capture_id").get<std::string>();
        if (cfg.id.empty() || cfg.id.size() > 48 ||
            !std::all_of(cfg.id.begin(), cfg.id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-';
            }))
            return {};
        const auto mode = root.at("mode").get<std::string>();
        if (mode != "discover" && mode != "capture")
            return {};
        cfg.discover = mode == "discover";
        cfg.delay = Bounded(root, "delay_swaps", 1, 120);
        cfg.width = Bounded(root, "window_swaps", 4, 8, 1);
        cfg.per_swap = Bounded(root, "packets_per_swap", 2, 2, 1);
        if (cfg.width * cfg.per_swap > MaxPackets)
            return {};
        const auto& selector = root.at("selector");
        if (!selector.is_object())
            return {};
        // CodexAstraLocal: Reject typos instead of silently broadening capture.
        constexpr std::array root_keys{"schema", "enabled", "capture_id", "mode", "title_id",
            "trigger", "delay_swaps", "window_swaps", "packets_per_swap", "selector"};
        constexpr std::array selector_keys{"attempt_ordinal", "program_hash", "swizzle_hash",
            "entry", "vertex_count", "color_addresses"};
        for (const auto& [key, value] : root.items())
            if (std::find(root_keys.begin(), root_keys.end(), key) == root_keys.end()) return {};
        for (const auto& [key, value] : selector.items())
            if (std::find(selector_keys.begin(), selector_keys.end(), key) == selector_keys.end()) return {};
        cfg.ordinal = Bounded(selector, "attempt_ordinal", 0, MaxExaminedPerSwap - 1);
        if (selector.contains("program_hash") && !selector.at("program_hash").is_null())
            cfg.program = Hex(selector.at("program_hash"));
        if (selector.contains("swizzle_hash") && !selector.at("swizzle_hash").is_null())
            cfg.swizzle = Hex(selector.at("swizzle_hash"));
        if (selector.contains("entry") && !selector.at("entry").is_null())
            cfg.entry = Bounded(selector, "entry", 0, 4095);
        if (selector.contains("vertex_count") && !selector.at("vertex_count").is_null())
            cfg.count = Bounded(selector, "vertex_count", 1, MaxVertices, 1);
        if (!cfg.discover && (!cfg.program || !cfg.swizzle || !cfg.entry))
            return {};
        if (selector.contains("color_addresses")) {
            const auto& colors = selector.at("color_addresses");
            if (!colors.is_array() || colors.size() > cfg.colors.size())
                return {};
            for (const auto& color : colors) {
                const u64 address = Hex(color);
                if (address > std::numeric_limits<u32>::max())
                    return {};
                cfg.colors[cfg.color_count++] = static_cast<u32>(address);
            }
        }
        auto result = std::unique_ptr<Session>{new Session};
        auto& s = *result->impl;
        s.path = FileUtil::GetUserPath(FileUtil::UserPath::DumpDir) +
                 "uberhar_vertex_capture/" + cfg.id + ".uvc";
        if (FileUtil::Exists(s.path))
            return {}; // CodexAstraLocal: Explicit experiment IDs never overwrite evidence.
        s.window = Window{cfg.delay, cfg.width, manual_phase};
        s.config = std::move(cfg);
        s.title = title;
        s.run = run;
        s.store = std::make_shared<Store>();
        // CodexAstraLocal Log Line: One opt-in record; absent/disabled config is silent.
        LOG_INFO(Render_Vulkan, "Uberhar vertex capture configured: id={} mode={} title={:016x} "
                 "window={} packets={} bytes={} trigger=manual_gameplay_edge",
                 s.config.id, mode, title, s.config.width, MaxPackets, MaxBytes);
        return result;
    } catch (...) {
        // CodexAstraLocal: Configuration/optional diagnostic IO never escapes emulation.
        return {};
    }
}

// CodexAstraLocal: Title switches close the window immediately, including
// switches between two swaps; a later return never re-arms this request.
void Session::ObserveIdentity(u64 title, u64 run) noexcept {
    auto& s = *impl;
    if (title != s.title || run != s.run) {
        s.identity_valid = false;
        s.window.Close();
        s.Abort();
    }
}

// CodexAstraLocal: A new run must not inherit tick or phase evidence, even
// when it happens to select the same title on a reused renderer.
void Session::NextSwap(u64 title, u64 run, u32 phase, bool startup,
                       std::optional<u64> submitted, u64 completed) noexcept {
    ObserveIdentity(title, run);
    auto& s = *impl;
    if (!s.identity_valid)
        return;
    if (submitted) s.submitted = std::max(s.submitted.value_or(0), *submitted);
    s.completed = std::max(s.completed, completed);
    s.window.NextSwap(phase, startup);
    s.ordinal = 0;
}

// CodexAstraLocal: Count global optional-request ordinals before filtering so
// discovery selectors retain the same population during payload capture.
void Session::BeginDraw(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup,
                        bool indexed) noexcept {
    auto& s = *impl;
    s.Abort();
    s.committed = false;
    if (!s.window.Active() || s.finished)
        return;
    // CodexAstraLocal: Saturate rather than wrap if a stalled swap interval
    // contains an extreme number of optional requests.
    if (s.ordinal >= MaxExaminedPerSwap) { ++s.capped; return; }
    const u32 ordinal = s.ordinal++;
    ++s.examined;
    if (!s.config.discover &&
        (ordinal < s.config.ordinal || ordinal - s.config.ordinal >= s.config.per_swap))
        return;
    s.key = {setup.GetProgramCodeHash(), setup.GetSwizzleDataHash(), regs.vs.main_offset.Value(),
        regs.pipeline.num_vertices, regs.vs.max_input_attribute_index.Value() + 1,
        regs.vs.output_mask.Value(), regs.framebuffer.framebuffer.GetColorBufferPhysicalAddress(),
        regs.framebuffer.framebuffer.GetDepthBufferPhysicalAddress()};
    const auto& c = s.config;
    if ((c.program && *c.program != s.key.program) ||
        (c.swizzle && *c.swizzle != s.key.swizzle) ||
        (c.entry && *c.entry != s.key.entry) || (c.count && *c.count != s.key.count) ||
        (c.color_count && std::find(c.colors.begin(), c.colors.begin() + c.color_count,
                                   s.key.color) == c.colors.begin() + c.color_count)) {
        ++s.filtered; return;
    }
    if (!c.discover && ++s.attempts > MaxAttempts) { ++s.capped; return; }
    s.current = true;
    s.current_ordinal = ordinal;
    s.indexed = indexed;
}

// CodexAstraLocal: Every preparation return closes the attempt; failed chosen
// ordinals are censored rather than silently replaced by unrelated later draws.
void Session::EndDraw() noexcept {
    auto& s = *impl;
    if (s.current && !s.committed)
        ++s.preparation_failures;
    s.Abort();
}
bool Session::WantsPayload() const noexcept { return impl->current && !impl->config.discover; }
bool Session::HasAttempt() const noexcept { return impl->current; }

// CodexAstraLocal: Bound count/span, aggregate allocation and copy work before
// freezing guest state. No live guest pointers survive this call.
void Session::PreparePayload(const Pica::RegsInternal& regs, const Pica::ShaderSetup& setup,
    const Pica::AttributeBuffer& defaults,
    const std::array<Pica::NativeInputAttribute, 16>& native_inputs, u32 available,
    u32 minimum, u32 maximum) noexcept {
    auto& s = *impl;
    if (!WantsPayload())
        return;
    try {
        if (!ValidShape(s.key.count, minimum, maximum) || available > 16) {
            ++s.capped; s.Abort(); return;
        }
        const u64 span = static_cast<u64>(maximum) - minimum + 1;
        u64 bytes = 3072 + 32768 + 256 + 1536 + 16 + 16 + 272 + 1616 + 32 + 1328;
        if (s.indexed)
            bytes += static_cast<u64>(s.key.count) * 2;
        for (const auto& loader : regs.pipeline.vertex_attributes.attribute_loaders) {
            if (loader.component_count && loader.byte_count)
                bytes += span * loader.byte_count;
        }
        if (bytes > MaxPacketPayloadBytes || !CanAppend(s.store->allocated, bytes, MaxPayloadBytes) ||
            !CanAppend(s.store->copied, bytes, MaxCopiedBytes)) {
            ++s.capped; s.Abort(); return;
        }
        for (u32 i = 0; i < MaxPackets; ++i) {
            if (!s.store->packets[i].data) { s.packet = i; break; }
        }
        if (s.packet < 0) { ++s.capped; s.Abort(); return; }
        auto& p = s.store->packets[s.packet];
        p.data = std::make_unique_for_overwrite<u8[]>(bytes);
        p.capacity = bytes;
        s.store->allocated += bytes;
        p.size = 0;
        p.sections = {};
        p.binding_count = 0;
        p.key = s.key;
        p.swap = s.window.Swap();
        p.interval = s.window.Interval();
        p.ordinal = s.current_ordinal;
        p.minimum = minimum; p.maximum = maximum;
        p.vertex_offset = regs.pipeline.vertex_offset;
        p.indexed = s.indexed; p.queued = false;
        p.execution.recorded.store(false, std::memory_order_relaxed);
        p.execution.tick = 0;
        p.available_attributes = available;
        p.program_words = setup.GetBiggestProgramSize();
        p.swizzle_words = setup.GetBiggestSwizzleSize();
        p.native_inputs = native_inputs;
        s.Append(0, regs.reg_array.data(), regs.reg_array.size() * 4);
        s.Append(1, setup.GetProgramCode().data(), setup.GetProgramCode().size() * 4);
        s.Append(2, setup.GetSwizzleData().data(), setup.GetSwizzleData().size() * 4);
        static_assert(sizeof(defaults) == 256 && sizeof(setup.uniforms.f) == 1536);
        s.Append(3, &defaults, sizeof(defaults));
        s.Append(4, setup.uniforms.f.data(), sizeof(setup.uniforms.f));
        std::array<u8, 16> booleans{};
        for (u32 i = 0; i < 16; ++i) booleans[i] = setup.uniforms.b[i] ? 1 : 0;
        s.Append(5, booleans.data(), booleans.size());
        s.Append(6, setup.uniforms.i.data(), 16);
    } catch (...) { ++s.errors; s.Abort(); }
}

// CodexAstraLocal: Preserve actual uploaded compact rows while excluding
// uninitialized alignment gaps; sparse/high-base indices retain their span.
void Session::CopyVertex(u32 binding, u32 guest_offset, u32 guest_stride, u32 upload_stride,
                         u32 span, const u8* uploaded) noexcept {
    auto& s = *impl;
    if (s.packet < 0) return;
    auto& p = s.store->packets[s.packet];
    const u64 bytes = static_cast<u64>(span) * guest_stride;
    if (!uploaded || binding >= 12 || p.binding_count >= 12 || upload_stride < guest_stride ||
        span != p.maximum - p.minimum + 1 || !CanAppend(p.size, bytes, p.capacity) ||
        !CanAppend(s.store->copied, bytes, MaxCopiedBytes)) {
        ++s.capped; s.Abort(); return;
    }
    const auto offset = p.size;
    for (u32 vertex = 0; vertex < span; ++vertex)
        std::memcpy(p.data.get() + offset + static_cast<std::size_t>(vertex) * guest_stride,
                    uploaded + static_cast<std::size_t>(vertex) * upload_stride, guest_stride);
    p.sections[7 + binding] = {static_cast<u32>(offset), static_cast<u32>(bytes)};
    p.size += bytes; s.store->copied += bytes;
    p.bindings[p.binding_count++] = {binding, guest_offset, guest_stride, upload_stride, span};
}
// CodexAstraLocal: Defaults are the initialized bytes from the existing upload,
// separate from original f24 defaults used by the CPU oracle.
void Session::CopyFixed(std::span<const u8> uploaded) noexcept {
    if (uploaded.size() > 272) { ++impl->capped; impl->Abort(); return; }
    impl->Append(19, uploaded.data(), uploaded.size());
}
// CodexAstraLocal: Keep the actual widened index sequence and original width;
// ordered duplicate keys are essential for the CPU FIFO replay.
void Session::CopyIndices(u32 original, u32 width, std::span<const u8> uploaded) noexcept {
    auto& s = *impl;
    if (s.packet < 0) return;
    auto& p = s.store->packets[s.packet];
    if ((width != 1 && width != 2) || (original != 1 && original != 2) ||
        uploaded.size() != static_cast<std::size_t>(p.key.count) * width) {
        ++s.capped; s.Abort(); return;
    }
    for (u32 i = 0; i < p.key.count; ++i) {
        const u32 index = width == 1 ? uploaded[i] :
            static_cast<u32>(uploaded[2 * i]) | (static_cast<u32>(uploaded[2 * i + 1]) << 8);
        if (index < p.minimum || index > p.maximum) { ++s.capped; s.Abort(); return; }
    }
    p.original_width = original; p.uploaded_width = width;
    s.Append(20, uploaded.data(), uploaded.size());
}

// CodexAstraLocal: Freeze final bound semantics after successful pipeline binding;
// the returned token still needs real command execution and queue acceptance.
Token Session::Commit(const BindingState& bound, const StreamBuffer& uniforms, u64 tick) noexcept {
    auto& s = *impl;
    Token result;
    if (!s.current || !tick) return result;
    try {
        if (s.config.discover) {
            const auto identity = Identity(bound);
            s32 row = -1;
            for (u32 i = 0; i < s.store->row_count; ++i) {
                const auto& r = s.store->rows[i];
                if (r.interval == s.window.Interval() && r.key == s.key && r.identity == identity) {
                    row = i; break;
                }
            }
            if (row < 0) {
                const auto interval = s.window.Interval();
                if (s.store->row_count >= MaxRows ||
                    s.store->rows_per_interval[interval] >= MaxRows / s.config.width) {
                    ++s.capped; return result;
                }
                row = s.store->row_count++;
                ++s.store->rows_per_interval[interval];
                auto& r = s.store->rows[row];
                r.key = s.key; r.identity = identity; r.swap = s.window.Swap();
                r.interval = interval; r.ordinal = s.current_ordinal;
            }
            result.row = row;
        } else {
            if (s.packet < 0) return result;
            auto& p = s.store->packets[s.packet];
            if (!p.sections[19].size || (p.indexed && !p.sections[20].size)) {
                ++s.errors; s.Abort(); return result;
            }
            p.bound = bound; p.identity = Identity(bound);
            constexpr std::array<u32, 3> sizes{1616, 32, 1328};
            for (u32 binding = 0; binding < 3; ++binding) {
                // CodexAstraLocal: Copy directly into the reserved packet. A
                // temporary uniform image would duplicate unbudgeted copies.
                u32 semantic_bytes{};
                UniformRanges(binding, [&](u32, u32 size) { semantic_bytes += size; });
                const auto charge = sizes[binding] + semantic_bytes;
                if (!CanAppend(p.size, sizes[binding], p.capacity) ||
                    !CanAppend(s.store->copied, charge, MaxCopiedBytes)) {
                    ++s.capped; s.Abort(); return result;
                }
                auto* clean = p.data.get() + p.size;
                std::memset(clean, 0, sizes[binding]);
                bool valid = true;
                UniformRanges(binding, [&](u32 offset, u32 size) {
                    valid &= uniforms.CopyHostWrittenBytes(
                        static_cast<u64>(bound.uniform_offsets[binding]) + offset,
                        std::span{clean + offset, size});
                });
                s.store->copied += charge;
                if (!valid) {
                    ++s.errors; s.Abort(); return result;
                }
                p.sections[21 + binding] = {static_cast<u32>(p.size), sizes[binding]};
                p.size += sizes[binding];
            }
            p.execution.tick = tick;
            p.queued = true;
            result.packet = s.packet;
        }
        result.owner = s.store;
        result.tick = tick;
        s.committed = true;
        return result;
    } catch (...) { ++s.errors; s.Abort(); return {}; }
}

// CodexAstraLocal: Bounded serialization and file publication are below; neither
// this session nor its queued evidence token creates a logging/IO worker.

namespace {
// CodexAstraLocal: Format directly into a fixed 128 KiB buffer. There is no JSON
// DOM or second payload-sized serialization buffer at artifact publication.
class Writer {
public:
    void Put(std::string_view text) noexcept {
        if (!CanAppend(used, text.size(), bytes.size())) { good = false; return; }
        std::memcpy(bytes.data() + used, text.data(), text.size()); used += text.size();
    }
    template <typename... T> void Format(fmt::format_string<T...> format, T&&... args) {
        const auto remaining = bytes.size() - used;
        const auto result = fmt::format_to_n(bytes.data() + used, remaining, format,
                                             std::forward<T>(args)...);
        if (result.size > remaining) { good = false; return; }
        used += result.size;
    }
    void String(std::string_view value) {
        Put("\"");
        for (const unsigned char c : value) {
            if (c < 32 || c == '\\' || c == '"') Format("\\u{:04x}", c);
            else Put({reinterpret_cast<const char*>(&c), 1});
        }
        Put("\"");
    }
    void Hash(u64 value) { Format("\"{:016x}\"", value); }
    void Boolean(bool value) { Put(value ? "true" : "false"); }
    std::array<char, MaxManifestBytes> bytes{};
    std::size_t used{};
    bool good{true};
};

// CodexAstraLocal: Canonical scalar-only records keep artifacts portable and
// selectors independent of host pointer values or C++ structure layout.
void WriteKey(Writer& out, const Key& key) {
    out.Format("{{\"program_hash\":\"{:016x}\",\"swizzle_hash\":\"{:016x}\","
               "\"entry\":{},\"vertex_count\":{},\"input_count\":{},\"output_mask\":{},"
               "\"color_address\":\"{:016x}\",\"depth_address\":\"{:016x}\"}}",
               key.program, key.swizzle, key.entry, key.count, key.inputs, key.outputs,
               key.color, key.depth);
}
// CodexAstraLocal: Shader identities name the selected transport route, not
// proof that a differing vertex value contributed to visible pixels.
void WriteIdentity(Writer& out, const PipelineIdentity& value) {
    out.Format("{{\"key\":\"{:016x}\",\"vs_config_hash\":\"{:016x}\","
               "\"fs_config_hash\":\"{:016x}\",\"gs_config_hash\":\"{:016x}\","
               "\"vs_source_hash\":\"{:016x}\",\"fragment_route\":\"{}\","
               "\"viewport\":[{},{},{},{}],\"scissor\":[{},{},{},{}],"
               "\"color_format\":{},\"depth_format\":{}}}",
        value.key, value.shaders[0], value.shaders[1], value.shaders[2], value.source,
        value.specialized ? "specialized" : "generic",
        value.viewport[0], value.viewport[1], value.viewport[2], value.viewport[3],
        value.scissor[0], value.scissor[1], value.scissor[2], value.scissor[3],
        value.color_format, value.depth_format);
}
// CodexAstraLocal: Retain exact generation/format decisions for independent
// replay; host capabilities must not silently replace the captured profile.
void WriteProfile(Writer& out, const Pica::Shader::Profile& profile) {
    out.Put("{");
#define PROFILE_FLAG(member) out.Format("\"" #member "\":{},", static_cast<u32>(profile.member))
    PROFILE_FLAG(enable_accurate_mul); PROFILE_FLAG(has_separable_shaders);
    PROFILE_FLAG(has_clip_planes); PROFILE_FLAG(has_geometry_shader);
    PROFILE_FLAG(has_custom_border_color); PROFILE_FLAG(has_fragment_shader_interlock);
    PROFILE_FLAG(has_fragment_shader_barycentric); PROFILE_FLAG(has_blend_minmax_factor);
    PROFILE_FLAG(has_minus_one_to_one_range); PROFILE_FLAG(has_logic_op);
    PROFILE_FLAG(has_gl_ext_framebuffer_fetch); PROFILE_FLAG(has_gl_arm_framebuffer_fetch);
    PROFILE_FLAG(has_gl_nv_fragment_shader_interlock); PROFILE_FLAG(has_gl_intel_fragment_shader_ordering);
    PROFILE_FLAG(has_gl_nv_fragment_shader_barycentric); PROFILE_FLAG(vk_disable_spirv_optimizer);
    PROFILE_FLAG(vk_use_spirv_generator); PROFILE_FLAG(is_vulkan);
#undef PROFILE_FLAG
    out.Put("\"vk_format_traits\":[");
    bool first = true;
    for (const auto& trait : profile.vk_format_traits) {
        if (!first) out.Put(","); first = false;
        out.Put("{");
#define TRAIT_FIELD(member) out.Format("\"" #member "\":{},", static_cast<u32>(trait.member))
        TRAIT_FIELD(transfer_support); TRAIT_FIELD(blit_support); TRAIT_FIELD(attachment_support);
        TRAIT_FIELD(storage_support); TRAIT_FIELD(needs_conversion); TRAIT_FIELD(needs_emulation);
        TRAIT_FIELD(usage_flags); TRAIT_FIELD(aspect_flags);
#undef TRAIT_FIELD
        out.Format("\"native_format\":{}}}", trait.native_format);
    }
    out.Put("]}");
}

// CodexAstraLocal: Emit named fields/sections and explicit lifecycle states,
// including actual buffer formats and zero-masked UBO semantic ranges.
void WritePacket(Writer& out, const Packet& p, u32 id, std::size_t base,
                 std::optional<u64> submitted, u64 completed) {
    out.Format("{{\"id\":{},\"swap\":{},\"interval\":{},\"attempt_ordinal\":{},"
               "\"tick\":\"{:016x}\",\"recorded\":{},\"accepted\":{},\"completed\":{},"
               "\"status\":\"{}\",\"key\":", id, p.swap, p.interval, p.ordinal,
        p.execution.tick, p.execution.WasRecorded(), p.execution.Accepted(submitted),
        p.execution.Completed(submitted, completed),
        p.execution.Completed(submitted, completed) ? "completed" :
        p.execution.Accepted(submitted) ? "accepted" :
        p.execution.WasRecorded() ? "recorded_pending" : "not_recorded");
    WriteKey(out, p.key);
    out.Format(",\"program_words\":{},\"swizzle_words\":{},"
               "\"draw\":{{\"indexed\":{},\"count\":{},\"minimum\":{},\"maximum\":{},"
               "\"vertex_offset\":{},\"base_vertex\":{},\"original_index_width\":{},"
               "\"uploaded_index_width\":{}}},\"sections\":{{", p.program_words, p.swizzle_words,
        p.indexed, p.key.count,
        p.minimum, p.maximum, p.vertex_offset, p.indexed ? -static_cast<s64>(p.minimum) : 0,
        p.indexed ? p.original_width : 0, p.indexed ? p.uploaded_width : 0);
    bool first = true;
    for (u32 i = 0; i < p.sections.size(); ++i) {
        if (!p.sections[i].size) continue;
        if (!first) out.Put(","); first = false;
        out.Format("\"{}\":{{\"offset\":{},\"size\":{}}}", SectionNames[i],
                   base + p.sections[i].offset, p.sections[i].size);
    }
    out.Put("},\"bindings\":[");
    for (u32 i = 0; i < p.binding_count; ++i) {
        if (i) out.Put(",");
        const auto& b = p.bindings[i];
        out.Format("{{\"binding\":{},\"guest_offset\":{},\"guest_stride\":{},"
                   "\"upload_stride\":{},\"span\":{},\"section\":\"vertex_{}\"}}",
                   b.binding, b.guest_offset, b.guest_stride, b.upload_stride, b.span, b.binding);
    }
    out.Format("],\"available_attributes\":{},\"native_inputs\":[", p.available_attributes);
    for (u32 i = 0; i < p.native_inputs.size(); ++i) {
        if (i) out.Put(",");
        const auto& n = p.native_inputs[i];
        out.Format("{{\"offset\":{},\"stride\":{},\"elements\":{},\"format\":{},"
                   "\"is_default\":{}}}", n.is_default ? 0 : n.offset,
                   n.is_default ? 0 : n.stride, n.is_default ? 0 : n.elements,
                   static_cast<u32>(n.format), n.is_default);
    }
    out.Put("],\"layout\":{\"bindings\":[");
    const auto& layout = p.bound.pipeline.state.vertex_layout;
    for (u32 i = 0; i < layout.binding_count; ++i) {
        if (i) out.Put(",");
        const auto& b = layout.bindings[i];
        u32 stride{};
        for (u32 j = 0; j < p.binding_count; ++j)
            if (p.bindings[j].binding == b.binding.Value()) stride = p.bindings[j].upload_stride;
        out.Format("{{\"binding\":{},\"stride\":{},\"fixed\":{}}}",
                   b.binding.Value(), stride, b.fixed.Value() != 0);
    }
    out.Put("],\"attributes\":[");
    for (u32 i = 0; i < layout.attribute_count; ++i) {
        if (i) out.Put(",");
        const auto& a = layout.attributes[i];
        const u32 type = static_cast<u32>(a.type.Value()), size = a.size.Value();
        const auto& trait = p.bound.profile.vk_format_traits[type * 4 + size - 1];
        // CodexAstraLocal: Match GraphicsPipeline's actual widened fetch when
        // the original three-component format requires four-component emulation.
        const auto& effective = trait.needs_emulation
            ? p.bound.profile.vk_format_traits[type * 4 + 3] : trait;
        out.Format("{{\"binding\":{},\"location\":{},\"offset\":{},\"type\":{},\"size\":{},"
                   "\"native_format\":{},\"needs_conversion\":{},\"needs_emulation\":{}}}",
                   a.binding.Value(), a.location.Value(), a.offset.Value(), type, size,
                   effective.native_format, trait.needs_conversion != 0, trait.needs_emulation != 0);
    }
    const auto& e = p.bound.extra;
    out.Format("]}},\"extra\":{{\"use_clip_planes\":{},\"use_geometry_shader\":{},"
               "\"sanitize_mul\":{},\"separable_shader\":{},\"load_flags\":[",
               e.use_clip_planes != 0, e.use_geometry_shader != 0,
               e.sanitize_mul != 0, e.separable_shader != 0);
    for (u32 i = 0; i < e.load_flags.size(); ++i) {
        if (i) out.Put(","); out.Format("{}", static_cast<u32>(e.load_flags[i]));
    }
    out.Put("]},\"pipeline\":"); WriteIdentity(out, p.identity);
    out.Put(",\"profile\":"); WriteProfile(out, p.bound.profile);
    out.Put(",\"ubo_offsets\":[");
    for (u32 i = 0; i < 3; ++i) { if (i) out.Put(","); out.Format("{}", p.bound.uniform_offsets[i]); }
    out.Put("],\"ubo_ranges\":{");
    constexpr std::array names{"vs_pica", "vs_extra", "fs"};
    for (u32 i = 0; i < 3; ++i) {
        if (i) out.Put(","); out.Format("\"{}\":[", names[i]);
        bool first_range = true;
        UniformRanges(i, [&](u32 offset, u32 size) {
            if (!first_range) out.Put(","); first_range = false;
            out.Format("[{},{}]", offset, size);
        });
        out.Put("]");
    }
    out.Put("}}");
}
} // namespace

// CodexAstraLocal: Existing drained teardown owns one bounded exclusive write.
// Killing the process may lose RAM evidence; no new wait or writer is introduced.
void Session::Finish(std::optional<u64> submitted, u64 completed) noexcept {
    auto& s = *impl;
    if (s.finished) return;
    s.finished = true;
    s.Abort();
    // CodexAstraLocal: Retain proven old-run watermarks after an identity close;
    // never authorize its pending evidence using a later run's submission.
    if (s.identity_valid) {
        if (submitted) s.submitted = std::max(s.submitted.value_or(0), *submitted);
        s.completed = std::max(s.completed, completed);
    }
    try {
        Writer out;
        out.Put("{\"schema\":1,\"capture_id\":"); out.String(s.config.id);
        out.Format(",\"mode\":\"{}\",\"title_id\":\"{:016x}\",\"run\":\"{:016x}\","
                   "\"version\":", s.config.discover ? "discover" : "capture", s.title, s.run);
        out.String(UBERHAR_CAPTURE_VERSION); out.Put(",\"revision\":"); out.String(Common::g_scm_rev);
        out.Format(",\"first_swap\":{},\"window_swaps\":{},\"packets_per_swap\":{},"
                   "\"selector\":{{\"attempt_ordinal\":{},\"program_hash\":",
                   s.window.FirstSwap(), s.config.width, s.config.per_swap, s.config.ordinal);
        if (s.config.program) out.Hash(*s.config.program); else out.Put("null");
        out.Put(",\"swizzle_hash\":");
        if (s.config.swizzle) out.Hash(*s.config.swizzle); else out.Put("null");
        out.Put(",\"entry\":");
        if (s.config.entry) out.Format("{}", *s.config.entry); else out.Put("null");
        out.Put(",\"vertex_count\":");
        if (s.config.count) out.Format("{}", *s.config.count); else out.Put("null");
        out.Put(",\"color_addresses\":[");
        for (u32 i = 0; i < s.config.color_count; ++i) {
            if (i) out.Put(","); out.Hash(s.config.colors[i]);
        }
        out.Format("]}},\"limits\":{{\"packets\":{},\"bytes\":{},\"payload_bytes\":{},"
                   "\"packet_bytes\":{},\"vertices\":{},\"rows\":{}}},"
                   "\"summary\":{{\"examined\":{},\"filtered\":{},\"capped\":{},"
                   "\"preparation_failures\":{},\"errors\":{},\"selected_attempts\":{},"
                   "\"allocated_payload_bytes\":{},\"copied_bytes\":{}}},\"discovery\":[",
                   MaxPackets, MaxBytes, MaxPayloadBytes, MaxPacketBytes, MaxVertices, MaxRows,
                   s.examined, s.filtered, s.capped, s.preparation_failures, s.errors, s.attempts,
                   s.store->allocated, s.store->copied);
        bool first = true;
        for (u32 i = 0; i < s.store->row_count; ++i) {
            const auto& row = s.store->rows[i];
            const u32 count = row.recorded.load(std::memory_order_acquire);
            if (!count) continue;
            if (!first) out.Put(","); first = false;
            const u64 last = row.last_tick.load(std::memory_order_relaxed);
            const bool accepted = s.submitted && last <= *s.submitted;
            out.Format("{{\"swap\":{},\"interval\":{},\"attempt_ordinal\":{},\"key\":",
                       row.swap, row.interval, row.ordinal);
            WriteKey(out, row.key); out.Put(",\"pipeline\":"); WriteIdentity(out, row.identity);
            out.Format(",\"recorded_count\":{},\"first_tick\":\"{:016x}\","
                       "\"last_tick\":\"{:016x}\",\"accepted_all\":{},\"completed_all\":{}}}",
                       count, row.first_tick.load(std::memory_order_relaxed), last,
                       accepted, accepted && last <= s.completed);
        }
        out.Put("],\"packets\":[");
        std::size_t payload_size{};
        u32 packets{};
        for (u32 i = 0; i < MaxPackets; ++i) {
            const auto& p = s.store->packets[i];
            if (!p.data || !p.queued) continue;
            if (packets++) out.Put(",");
            const auto manifest_start = out.used;
            WritePacket(out, p, i, payload_size, s.submitted, s.completed);
            // CodexAstraLocal: Keep the producer's complete-packet cap aligned
            // with strict readers even if a later schema adds metadata fields.
            const auto packet_metadata = out.used - manifest_start;
            if (!out.good || packet_metadata > PacketMetadataReserve ||
                !CanAppend(p.size, packet_metadata, MaxPacketBytes))
                throw std::runtime_error("capture packet metadata bound");
            payload_size += p.size;
        }
        out.Put("]}");
        if (!out.good || payload_size > MaxPayloadBytes ||
            !CanAppend(16 + out.used, payload_size, MaxBytes) || FileUtil::Exists(s.path))
            throw std::runtime_error("capture serialization bound or existing output");
        if (!FileUtil::CreateFullPath(s.path))
            throw std::runtime_error("capture directory unavailable");
        // CodexAstraLocal: This provider must honor exclusive creation. SAF,
        // Windows and libretro adapters discard the x mode, so stay disabled.
#if defined(_WIN32) || defined(HAVE_LIBRETRO_VFS)
        throw std::runtime_error("capture requires exclusive raw filesystem creation");
#elif defined(ANDROID)
        if (!AndroidUtils::CanUseRawFS())
            throw std::runtime_error("capture requires exclusive raw filesystem creation");
#endif
        FileUtil::IOFile output{s.path, "wbx"};
        std::array<u8, 16> header{'U','B','V','C','A','P','0','1'};
        for (u32 i = 0; i < 4; ++i) {
            header[8 + i] = static_cast<u8>(out.used >> (i * 8));
            header[12 + i] = static_cast<u8>(payload_size >> (i * 8));
        }
        bool ok = output.IsOpen() && output.WriteBytes(header.data(), header.size()) == header.size()
                  && output.WriteBytes(out.bytes.data(), out.used) == out.used;
        for (const auto& p : s.store->packets) {
            if (p.data && p.queued)
                ok &= output.WriteBytes(p.data.get(), p.size) == p.size;
        }
        ok &= output.Flush();
        // CodexAstraLocal: Report retention only after the provider also closes
        // the stream successfully; a failed/partial artifact remains untouched.
        ok &= output.Close();
        // CodexAstraLocal Log Line: One finite opt-in artifact summary, no guest bytes.
        LOG_INFO(Render_Vulkan, "Uberhar vertex capture final: id={} written={} packets={} "
                 "rows={} payload={} capped={} errors={} path={}", s.config.id, ok, packets,
                 s.store->row_count, payload_size, s.capped, s.errors, s.path);
    } catch (...) {
        // CodexAstraLocal: Even teardown/format/provider failures stay diagnostic.
        try {
            // CodexAstraLocal Log Line: One bounded failure; preserve any partial file.
            LOG_WARNING(Render_Vulkan, "Uberhar vertex capture write unavailable: id={}", s.config.id);
        } catch (...) {
        }
    }
}

} // namespace Vulkan::VertexCapture
