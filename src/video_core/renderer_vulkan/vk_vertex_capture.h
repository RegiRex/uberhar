// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <memory>
#include <span>
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/uberhar_vertex_input.h"
#include "video_core/renderer_vulkan/uberhar_vertex_capture_policy.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Vulkan {
class StreamBuffer;

namespace VertexCapture {

// CodexAstraLocal: Owned values from the final successful optional-ready bind;
// dynamic offsets describe actual UBO transport, not current PICA intentions.
struct BindingState {
    PipelineInfo pipeline{};
    Pica::Shader::Generator::ExtraVSConfig extra{};
    Pica::Shader::Profile profile{};
    std::array<u32, 3> uniform_offsets{};
    std::array<u64, 3> shader_ids{};
    u64 pipeline_key{}, source_hash{};
    bool specialized{};
};

struct Store;

// CodexAstraLocal: Only this small owner is queued. Payload and descriptor rows
// remain alive if preparation is canceled or the rasterizer is destroyed.
class Token {
public:
    Token() = default;
    explicit operator bool() const noexcept { return owner != nullptr; }
    void Recorded() const noexcept;
private:
    friend class Session;
    std::shared_ptr<Store> owner;
    s32 packet{-1}, row{-1};
    u64 tick{};
};

// CodexAstraLocal: One opt-in configuration, one finite window and one artifact.
// All public operations fail closed for diagnostics without changing a draw.
class Session {
public:
    static std::unique_ptr<Session> Load(u64 title, u64 run, u32 manual_phase) noexcept;
    ~Session();
    void ObserveIdentity(u64 title, u64 run) noexcept;
    void NextSwap(u64 title, u64 run, u32 manual_phase, bool startup,
                  std::optional<u64> submitted, u64 completed) noexcept;
    void BeginDraw(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup,
                   bool indexed) noexcept;
    void EndDraw() noexcept;
    bool WantsPayload() const noexcept;
    bool HasAttempt() const noexcept;
    void PreparePayload(const Pica::RegsInternal& regs, const Pica::ShaderSetup& setup,
                        const Pica::AttributeBuffer& defaults,
                        const std::array<Pica::NativeInputAttribute, 16>& native_inputs,
                        u32 available_attributes, u32 minimum, u32 maximum) noexcept;
    void CopyVertex(u32 binding, u32 guest_offset, u32 guest_stride, u32 upload_stride,
                    u32 span, const u8* uploaded) noexcept;
    void CopyFixed(std::span<const u8> uploaded) noexcept;
    void CopyIndices(u32 original_width, u32 uploaded_width,
                     std::span<const u8> uploaded) noexcept;
    Token Commit(const BindingState& binding, const StreamBuffer& uniform_buffer,
                 u64 tick) noexcept;
    void Finish(std::optional<u64> submitted, u64 completed) noexcept;
private:
    Session();
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // CodexAstraLocal: namespace VertexCapture
} // namespace Vulkan
