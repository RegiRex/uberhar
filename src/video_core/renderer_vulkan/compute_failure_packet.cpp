// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <map>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <fmt/format.h>
#include <json.hpp>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/uberhar_activity.h"
#include "video_core/renderer_vulkan/compute_failure_packet.h"
#include "video_core/renderer_vulkan/vk_compute_vertex.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#ifdef ANDROID
#include "common/android_utils.h"
#endif

namespace Vulkan {
namespace {
constexpr std::size_t MaximumPayload = 64 * 1024 * 1024;
std::atomic<u64> next_packet{1};
constexpr std::array Names{
    "capture", "admission", "previous_use_wait", "guest_translate", "guest_compile",
    "guest_pipeline", "assembly_compile", "assembly_pipeline", "allocation", "upload",
    "descriptors", "guest_dispatch", "assembly_dispatch", "readback_copy", "producer_wait",
    "invalidate", "completion", "assembly_state", "resource_preparation", "raster_compile",
    "raster_pipeline", "transfer_compile", "transfer_pipeline", "raster_bind",
    "raster_dispatch", "queue_submit", "final_use_wait"};
static_assert(Names.size() == static_cast<u32>(ComputeFailureStage::Count));
static_assert(Names.size() < 64);
u64 SectionHash(const void* data, std::size_t size) {
    u64 hash = 14695981039346656037ULL;
    const auto* bytes = static_cast<const u8*>(data);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    return hash;
}
}

std::string_view ComputeFailureStageName(ComputeFailureStage stage) noexcept {
    const auto index = static_cast<u32>(stage);
    return index < Names.size() ? Names[index] : "invalid_stage";
}

struct ComputeFailurePacket::Impl {
    std::atomic<ComputeFailureStage> stage{ComputeFailureStage::Capture};
    std::atomic<u64> reported{}, pending{};
    std::atomic<bool> dependency_canceled{};
    struct Notice { std::array<char,1024> reason{}; int result{}; };
    std::array<Notice,Names.size()> notices{};
    std::atomic<bool> degraded{};
    std::mutex mutex;
    nlohmann::json labels = nlohmann::json::object();
    nlohmann::json unavailable = nlohmann::json::object();
    std::map<std::string, std::shared_ptr<const std::vector<u8>>> sections;
    std::vector<std::shared_ptr<const ComputeModuleEvidence>> modules;
    std::shared_ptr<const ComputeVertexInput> input;
    std::shared_ptr<const ComputeDeviceEvidence> device;
    bool input_complete{};
    std::array<u32, 64> actual{};
    std::vector<u32> expected;
    bool status_available{};
    const u64 id{next_packet.fetch_add(1)};
    const u64 session{Common::UberharActivity::Capture().run};
};

std::shared_ptr<const ComputeDeviceEvidence> CaptureComputeDeviceEvidence(const Instance& instance) noexcept {
    try {
        const auto p = instance.GetPhysicalDevice().getProperties();
        return std::make_shared<ComputeDeviceEvidence>(ComputeDeviceEvidence{
            p.vendorID, p.deviceID, p.driverVersion, p.apiVersion,
            p.limits.maxStorageBufferRange, p.limits.maxComputeWorkGroupInvocations,
            p.limits.maxPushConstantsSize, p.deviceName.data()});
    } catch (...) { return {}; }
}
ComputeFailurePacket::ComputeFailurePacket(std::shared_ptr<const ComputeDeviceEvidence> device)
    : impl{std::make_unique<Impl>()} {
    impl->device = std::move(device);
}
ComputeFailurePacket::~ComputeFailurePacket() = default;
void ComputeFailurePacket::Stage(ComputeFailureStage stage) noexcept { impl->stage.store(stage); }
ComputeFailureStage ComputeFailurePacket::Stage() const noexcept { return impl->stage.load(); }
void ComputeFailurePacket::Label(std::string_view key, std::string_view value) noexcept {
    try { std::scoped_lock lock{impl->mutex}; impl->labels[std::string{key}] = value; }
    catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Number(std::string_view key, u64 value) noexcept {
    try { std::scoped_lock lock{impl->mutex}; impl->labels[std::string{key}] = value; }
    catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Bytes(std::string_view name, std::span<const u8> bytes) noexcept {
    try { Bytes(name, std::vector<u8>{bytes.begin(), bytes.end()}); }
    catch (...) { impl->degraded = true; Unavailable(name, "capture_allocation_failed"); }
}
void ComputeFailurePacket::Bytes(std::string_view name, std::vector<u8>&& bytes) noexcept {
    try {
        std::scoped_lock lock{impl->mutex};
        impl->sections[std::string{name}] = std::make_shared<const std::vector<u8>>(std::move(bytes));
        impl->unavailable.erase(std::string{name});
    } catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Input(std::shared_ptr<const ComputeVertexInput> input, bool complete) noexcept {
    try { std::scoped_lock lock{impl->mutex};
        impl->input = std::move(input); impl->input_complete = complete;
    } catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Module(std::shared_ptr<const ComputeModuleEvidence> module) noexcept {
    try {
        std::scoped_lock lock{impl->mutex};
        for (auto& old : impl->modules) if (old->role == module->role) { old = std::move(module); return; }
        impl->modules.push_back(std::move(module));
    } catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Status(std::span<const u32> words) noexcept {
    if (words.size() != impl->actual.size()) { Unavailable("status", "invalid_word_count"); return; }
    try { std::scoped_lock lock{impl->mutex};
        std::copy(words.begin(), words.end(), impl->actual.begin()); impl->status_available = true;
    } catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Expected(std::span<const u32> words) noexcept {
    try { std::scoped_lock lock{impl->mutex}; impl->expected.assign(words.begin(), words.end()); }
    catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::Unavailable(std::string_view section, std::string_view reason) noexcept {
    try { std::scoped_lock lock{impl->mutex}; impl->unavailable[std::string{section}] = reason; }
    catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::CancelDependency() noexcept { impl->dependency_canceled = true; }
bool ComputeFailurePacket::DependencyCanceled() const noexcept { return impl->dependency_canceled.load(); }
bool ComputeFailurePacket::HasNotice() const noexcept { return impl->reported.load()!=0; }
void ComputeFailurePacket::Failure(std::string_view reason, int vk_result) noexcept {
    Failure(Stage(), reason, vk_result);
}
void ComputeFailurePacket::Failure(ComputeFailureStage stage, std::string_view reason, int vk_result) noexcept {
    Notice(stage, reason, vk_result);
    FlushNotices();
}
void ComputeFailurePacket::Notice(ComputeFailureStage stage, std::string_view reason, int vk_result) noexcept {
    if (DependencyCanceled()) return;
    if (static_cast<u32>(stage) >= Names.size()) stage = ComputeFailureStage::Admission;
    const u64 mask = u64{1} << static_cast<u32>(stage);
    if (impl->reported.fetch_or(mask) & mask) return;
    try {
        std::scoped_lock lock{impl->mutex};
        auto& note = impl->notices[static_cast<u32>(stage)];
        const auto length = std::min(reason.size(), note.reason.size()-1);
        std::copy_n(reason.data(), length, note.reason.data());
        note.reason[length] = 0;
        note.result = vk_result;
        impl->pending.fetch_or(mask);
    } catch (...) { impl->degraded = true; }
}
void ComputeFailurePacket::FlushNotices() noexcept {
    u64 pending = impl->pending.exchange(0);
    while (pending) {
        const auto index = std::countr_zero(pending);
        pending &= pending - 1;
        Impl::Notice note;
        try { std::scoped_lock lock{impl->mutex}; note = impl->notices[index]; }
        catch (...) { impl->degraded = true; continue; }
        WriteNotice(static_cast<ComputeFailureStage>(index), note.reason.data(), note.result);
    }
}
void ComputeFailurePacket::WriteNotice(ComputeFailureStage stage, std::string_view reason, int vk_result) noexcept {
    bool written = false;
    std::string path;
    const char* write_error = "serialization_failed";
    try {
        // CodexAstraLocal: Freeze references/metadata under the short capture
        // lock. Section contents and modules are immutable when published;
        // neither serialization nor file I/O holds this or scheduler locks.
        nlohmann::json labels, unavailable;
        decltype(impl->sections) sections;
        decltype(impl->modules) modules;
        decltype(impl->input) input_owner;
        decltype(impl->device) device;
        std::array<u32,64> actual{};
        std::vector<u32> expected;
        bool input_complete{}, status_available{}, degraded{};
        {
            std::scoped_lock lock{impl->mutex};
            labels=impl->labels; unavailable=impl->unavailable;
            sections=impl->sections; modules=impl->modules;
            input_owner=impl->input; device=impl->device;
            actual=impl->actual; expected=impl->expected;
            input_complete=impl->input_complete; status_available=impl->status_available;
            degraded=impl->degraded.load();
        }
        nlohmann::json metadata{{"schema", 1}, {"build", std::string{Common::g_scm_rev}},
            {"shader_cache_version", std::string{Common::g_shader_cache_version}}, {"session", impl->session},
            {"packet", impl->id}, {"stage", ComputeFailureStageName(stage)}, {"reason", reason},
            {"vk_result", vk_result}, {"byte_order", "little"}, {"labels", labels},
            {"capture_degraded", degraded}, {"input_complete", input_complete},
            {"status_available", status_available}, {"unavailable", unavailable},
            {"sections", nlohmann::json::object()}, {"modules", nlohmann::json::array()}};
#ifdef _WIN32
        metadata["process"]=_getpid();
#else
        metadata["process"]=getpid();
#endif
        metadata["labels"]["shader_entry_state"] = "fresh_draw_zero_input_output_temporary_address_condition";
        metadata["labels"]["later_vertex_entry_state"] = "serial_carry_reconstruct_from_original_packet_not_sampled";
        if (device) {
            const auto& d = *device;
            metadata["device"] = {{"vendor",d.vendor},{"device",d.device},{"driver_version",d.driver},
                {"api_version",d.api},{"device_name",d.name},{"max_storage_buffer_range",d.max_storage_range},
                {"max_workgroup_invocations",d.max_workgroup},{"max_push_constants",d.max_push}};
        } else metadata["unavailable"]["device"] = "device_identity_capture_failed";
        static_assert(std::endian::native == std::endian::little);
        std::vector<u8> payload;
        const auto append = [&](const std::string& name, const void* data, std::size_t bytes) {
            if (bytes > MaximumPayload - payload.size()) {
                metadata["unavailable"][name] = "packet_64MiB_payload_bound";
                metadata["capture_degraded"] = true;
                return;
            }
            metadata["sections"][name] = {{"offset", payload.size()}, {"bytes", bytes},
                {"fnv1a64", fmt::format("{:016X}",SectionHash(data,bytes))}};
            const auto* first = static_cast<const u8*>(data);
            if (bytes) payload.insert(payload.end(), first, first + bytes);
        };
        if (input_owner) {
            const auto& input = *input_owner;
            append("input.metadata.u32", input.metadata.data(), sizeof(input.metadata));
            append("input.raw", input.raw.data(), input.raw.size());
            append("input.program.u32", input.program.data(), sizeof(input.program));
            append("input.swizzles.u32", input.swizzles.data(), sizeof(input.swizzles));
            metadata["labels"]["sanitize_mul"] = input.sanitize_mul;
        } else metadata["unavailable"]["input"] = "capture_not_completed_no_substitution";
        if (status_available) append("actual.status.u32", actual.data(), sizeof(actual));
        else metadata["unavailable"]["actual.status.u32"] = "no_successful_current_packet_readback";
        if (!expected.empty()) append("expected.completion.u32", expected.data(), expected.size()*4);
        else metadata["unavailable"]["expected.completion.u32"] = "validation_not_reached";
        for (const auto& [name, bytes] : sections) append(name, bytes->data(), bytes->size());
        for (const auto& module : modules) {
            if (module->source.empty()) metadata["unavailable"][module->role+".comp"]="generation_not_completed";
            else append(module->role + ".comp", module->source.data(), module->source.size());
            if (module->spirv.empty()) metadata["unavailable"][module->role+".spv"]="compilation_not_completed";
            else append(module->role + ".spv", module->spirv.data(), module->spirv.size()*4);
            metadata["modules"].push_back({{"role", module->role},
                {"optimizer_disabled", module->optimizer_disabled}, {"entry", "main"},
                // CodexAstraLocal: Preserve compute-only structural identity
                // on initial creation and every later cached-module capture.
                {"structural_lowering_revision", module->structural_lowering_revision},
                {"calculated_feature_key", module->calculated_feature_key}});
        }
        // CodexAstraLocal TEST-ONLY LOG: A bounded or unavailable section is
        // explicit evidence loss, never a complete GPU replay capsule. Remove
        // together with the temporary incident recorder after corrected replay.
        metadata["payload_limit_bytes"] = MaximumPayload;
        metadata["full_capture"] = metadata["unavailable"].empty() &&
            !metadata["capture_degraded"].get<bool>() && input_complete;
        metadata["incomplete"] = !metadata["full_capture"].get<bool>();
        metadata["pixel_replay_complete"] = false;
        metadata["scope"] = "immutable producer packet; explicitly listed raster data only";
        const auto json = metadata.dump();
        if (json.size() > 1024*1024) throw std::runtime_error{"failure metadata bound"};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = FileUtil::GetUserPath(FileUtil::UserPath::DumpDir) + fmt::format(
            "calculated_failure_{}_{}_{}_{}.ucp", impl->session, impl->id,
            ComputeFailureStageName(stage), stamp);
        // CodexAstraLocal TEST-ONLY LOG: Exclusive creation and a required footer
        // preserve old incidents and make partial writes detectable. A provider
        // without exclusive raw-file support reports failure instead of clobbering.
        // Remove with this temporary first-fault capture after corrected replay.
#if !defined(_WIN32) && !defined(HAVE_LIBRETRO_VFS)
#ifdef ANDROID
        const bool raw = AndroidUtils::CanUseRawFS();
#else
        const bool raw = true;
#endif
        write_error = "raw_filesystem_or_parent_unavailable";
        if (raw && !FileUtil::Exists(path) && FileUtil::CreateFullPath(path)) {
            std::array<u8,16> header{'U','B','C','A','L','0','0','1'};
            for (u32 i=0;i<4;++i) {
                header[8+i] = static_cast<u8>(json.size() >> (i*8));
                header[12+i] = static_cast<u8>(payload.size() >> (i*8));
            }
            constexpr std::array<u8,8> footer{'U','C','E','N','D','0','0','1'};
            FileUtil::IOFile file{path,"wbx"};
            write_error = "exclusive_open_or_write_flush_close_failed";
            written = file && file.WriteBytes(header.data(),header.size())==header.size() &&
                file.WriteBytes(json.data(),json.size())==json.size() &&
                file.WriteBytes(payload.data(),payload.size())==payload.size() &&
                file.WriteBytes(footer.data(),footer.size())==footer.size();
            written &= file.Flush(); written &= file.Close();
            if (written) write_error = "none";
        }
#else
        write_error = "exclusive_capture_not_supported_on_this_filesystem";
#endif
    } catch (...) { /* Preserve the original failure, including OOM/readback errors. */ }
    try {
        LOG_ERROR(Render_Vulkan,
            "Calculated failure packet: schema=1 session={} packet={} stage={} written={} "
            "write_error={} capture_degraded={} path={} reason={}",
            impl->session, impl->id, ComputeFailureStageName(stage), written, write_error,
            impl->degraded.load(), path, reason);
    } catch (...) {}
}

std::shared_ptr<ComputeFailurePacket> MakeComputeFailurePacket(
    std::shared_ptr<const ComputeDeviceEvidence> device) noexcept {
    try { return std::make_shared<ComputeFailurePacket>(std::move(device)); }
    catch (...) {
        try { LOG_ERROR(Render_Vulkan, "Calculated failure packet unavailable: creation_failed=true"); }
        catch (...) {}
        return {};
    }
}

void ComputeFailureTracker::Begin(std::shared_ptr<ComputeFailurePacket> packet,
                                 u64 tick, u64 completed) noexcept {
    if (!packet) return;
    bool conflict=false;
    try {
        std::scoped_lock lock{mutex};
        if (submitted.packet && submitted.last<=completed) submitted={};
        conflict=(submitted.packet && submitted.packet!=packet) ||
            (recording.packet && recording.packet!=packet && recording.last>completed);
        if (!conflict) {
            if(recording.packet!=packet || recording.first!=tick)recording_used=false;
            open=recording.packet==packet ? open+1 : 1;
            recording={packet,tick,tick};
        }
    } catch (...) { conflict=true; }
    if (conflict) packet->Unavailable("submission_context",
        "unretired_other_packet; no current-draw substitution or rendering refusal");
}
void ComputeFailureTracker::End(const std::shared_ptr<ComputeFailurePacket>& packet) noexcept {
    try { std::scoped_lock lock{mutex};
        if(recording.packet==packet && open) {
            --open;
            if(!open && !recording_used)recording={};
        }
    } catch (...) {}
}
std::shared_ptr<ComputeFailurePacket> ComputeFailureTracker::Recording(u64 tick) noexcept {
    try { std::scoped_lock lock{mutex};
        if(recording.packet && recording.first==tick) {
            recording_used=true; return recording.packet;
        }
    } catch (...) {}
    return {};
}
std::shared_ptr<ComputeFailurePacket> ComputeFailureTracker::Attempt(u64 tick,u64 completed) noexcept {
    std::shared_ptr<ComputeFailurePacket> packet;
    bool conflict=false;
    try {
        std::scoped_lock lock{mutex};
        if(submitted.packet && submitted.last<=completed)submitted={};
        if(!recording.packet || recording.first!=tick)return {};
        if(!recording_used) {
            if(open)recording.first=recording.last=tick+1; else recording={};
            return {};
        }
        packet=recording.packet;
        conflict=submitted.packet && (submitted.packet!=packet || submitted.last+1!=tick);
        if(!conflict) {
            if(!submitted.packet)submitted={packet,tick,tick};
            else submitted.last=tick;
        }
        if(open)recording={packet,tick+1,tick+1}; else recording={};
        recording_used=false;
    } catch (...) { conflict=true; }
    if(packet) {
        packet->Number("submit_attempted_tick",tick);
        if(conflict)packet->Unavailable("retirement_context","noncontiguous_or_conflicting_submission");
    }
    return packet;
}
std::shared_ptr<ComputeFailurePacket> ComputeFailureTracker::Submitted(u64 tick) noexcept {
    try { std::scoped_lock lock{mutex};
        if(submitted.packet && tick>=submitted.first && tick<=submitted.last)return submitted.packet;
    } catch (...) {}
    return {};
}
void ComputeFailureTracker::Retire(u64 completed) noexcept {
    try { std::scoped_lock lock{mutex};
        if(submitted.packet && submitted.last<=completed)submitted={};
        if(!open && recording.packet && recording.last<=completed)recording={};
    } catch (...) {}
}
void ComputeFailureTracker::FatalWait(u64 tick,std::string_view reason,int result) noexcept {
    // No semaphore/free-queue/execution lock may be held by this caller.
    if(auto packet=Submitted(tick)) {
        packet->Number("failed_wait_tick",tick);
        packet->Unavailable("retirement_status","device_fence_wait_failed_no_readback_attempted");
        packet->Failure(ComputeFailureStage::FinalUseWait,reason,result);
    }
}
} // namespace Vulkan
