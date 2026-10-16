// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"

namespace Vulkan {
class Instance;
struct ComputeVertexInput;

// CodexAstraLocal: Distinct terminal stages retain separate incidents, while a
// repeated unwind of the same stage cannot overwrite or flood its first packet.
enum class ComputeFailureStage : u32 {
    Capture, Admission, PreviousUseWait, GuestTranslate, GuestCompile, GuestPipeline,
    AssemblyCompile, AssemblyPipeline, Allocation, Upload, Descriptors,
    GuestDispatch, AssemblyDispatch, ReadbackCopy, ProducerWait, Invalidate,
    Completion, AssemblyState, ResourcePreparation, RasterCompile, RasterPipeline,
    TransferCompile, TransferPipeline, RasterBind, RasterDispatch, QueueSubmit, FinalUseWait,
    Count,
};
std::string_view ComputeFailureStageName(ComputeFailureStage stage) noexcept;

// CodexAstraLocal: Cached pipelines retain the exact submitted source/module,
// not a regeneration under later registers, compiler options or guest memory.
struct ComputeModuleEvidence {
    std::string role;
    std::string source;
    std::vector<u32> spirv;
    bool optimizer_disabled{};
    // CodexAstraLocal: Structural retry is distinct from the broad GLSL
    // optimizer policy; cached accepted evidence must retain its exact variant.
    u32 structural_lowering_revision{};
    // CodexAstraLocal: Immutable source/diagnostic policy for cold and cached modules.
    u64 calculated_feature_key{};
};

// CodexAstraLocal: Query immutable device limits once per compute owner. Draws
// share this value; JSON and device labels are materialized only on failure.
struct ComputeDeviceEvidence {
    u32 vendor{}, device{}, driver{}, api{}, max_storage_range{}, max_workgroup{}, max_push{};
    std::string name;
};
std::shared_ptr<const ComputeDeviceEvidence> CaptureComputeDeviceEvidence(
    const Instance& instance) noexcept;

// CodexAstraLocal TEST-ONLY LOG: Own the original draw and its failure evidence
// until the final GPU-use token retires. Remove this capture after the actual
// DM completion and FEA semantic failures have reproducible corrected cases.
// No successful draw writes files; capture costs are real and not benchmark-free.
class ComputeFailurePacket final {
public:
    explicit ComputeFailurePacket(std::shared_ptr<const ComputeDeviceEvidence> device);
    ~ComputeFailurePacket();
    ComputeFailurePacket(const ComputeFailurePacket&) = delete;
    ComputeFailurePacket& operator=(const ComputeFailurePacket&) = delete;

    void Stage(ComputeFailureStage stage) noexcept;
    ComputeFailureStage Stage() const noexcept;
    void Label(std::string_view key, std::string_view value) noexcept;
    void Number(std::string_view key, u64 value) noexcept;
    void Bytes(std::string_view name, std::span<const u8> bytes) noexcept;
    void Bytes(std::string_view name, std::vector<u8>&& bytes) noexcept;
    template <typename T> void Value(std::string_view name, const T& value) noexcept {
        Bytes(name, {reinterpret_cast<const u8*>(&value), sizeof(value)});
    }
    void Input(std::shared_ptr<const ComputeVertexInput> input, bool complete) noexcept;
    void Module(std::shared_ptr<const ComputeModuleEvidence> module) noexcept;
    void Status(std::span<const u32> words) noexcept;
    void Expected(std::span<const u32> words) noexcept;
    // Records unavailable data explicitly. Neither a failed wait nor a mapping
    // failure authorizes reading a stale status buffer or attempting GPU replay.
    void Unavailable(std::string_view section, std::string_view reason) noexcept;
    // CodexAstraLocal: Worker callbacks publish only a bounded notice. The
    // scheduler flushes it after releasing execution/submit locks; serialization
    // also releases the packet lock before touching the filesystem.
    void Notice(ComputeFailureStage stage, std::string_view reason, int vk_result = 0) noexcept;
    void FlushNotices() noexcept;
    void CancelDependency() noexcept;
    bool DependencyCanceled() const noexcept;
    bool HasNotice() const noexcept;
    void Failure(ComputeFailureStage stage, std::string_view reason, int vk_result = 0) noexcept;
    void Failure(std::string_view reason, int vk_result = 0) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void WriteNotice(ComputeFailureStage stage, std::string_view reason, int vk_result) noexcept;
};

// CodexAstraLocal: Diagnostic allocation failure is reported, but cannot invent
// a new rendering refusal or mask the original terminal error.
std::shared_ptr<ComputeFailurePacket> MakeComputeFailurePacket(
    std::shared_ptr<const ComputeDeviceEvidence> device) noexcept;

// CodexAstraLocal: Calculated serializes packet reuse. Two bounded slots retain
// recording work and an actually attempted submission interval; success does
// not retire storage. Only a completed GPU tick permits a different packet.
// A scope explicitly arms every contiguous submission during preparation.
class ComputeFailureTracker final {
public:
    void Begin(std::shared_ptr<ComputeFailurePacket> packet, u64 tick, u64 completed) noexcept;
    void End(const std::shared_ptr<ComputeFailurePacket>& packet) noexcept;
    std::shared_ptr<ComputeFailurePacket> Recording(u64 tick) noexcept;
    std::shared_ptr<ComputeFailurePacket> Attempt(u64 tick, u64 completed) noexcept;
    std::shared_ptr<ComputeFailurePacket> Submitted(u64 tick) noexcept;
    void Retire(u64 completed) noexcept;
    void FatalWait(u64 tick, std::string_view reason, int result) noexcept;
private:
    struct Slot { std::shared_ptr<ComputeFailurePacket> packet; u64 first{}, last{}; };
    std::mutex mutex;
    Slot recording, submitted;
    u32 open{};
    bool recording_used{};
};
} // namespace Vulkan
