// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/shader_recovery_error.h"

namespace Vulkan {

// CodexAstraLocal: Four roles belong to the guest and raster compute owners.
// Raster/expand/pack share one first-failure latch because every failure is terminal.
// A failed module/pipeline must stop before any draw publication.
enum class ComputePipelineRole { GuestVertex, Raster, Expand, Pack };

inline std::string_view ComputePipelineRoleName(ComputePipelineRole role) {
    switch (role) {
    case ComputePipelineRole::GuestVertex: return "guest_vertex";
    case ComputePipelineRole::Raster: return "raster";
    case ComputePipelineRole::Expand: return "expand";
    case ComputePipelineRole::Pack: return "pack";
    }
    return "invalid";
}

// CodexAstraLocal TEST-ONLY LOG: Retain the first rejected module of this owner
// in the user's local log directory, never a public/cache upload. Remove this
// artifact path after the Thor rejection has an exact retained reproducer and
// a qualified correction. There is no success-path hashing or file I/O. Each
// source/SPIR-V payload is capped at 4 MiB; existing same-name evidence is never
// overwritten. Payload bytes, not the noncryptographic filename hash, identify it.
inline void RecordComputePipelineFailure(const Instance& instance, ComputePipelineRole role,
                                         std::string_view source, std::span<const u32> code,
                                         bool disable_optimizer, std::string_view operation,
                                         const vk::SystemError& error, bool& reported) noexcept {
    if (reported) return;
    reported = true;
    try {
        constexpr std::size_t MaxPayload = 4 * 1024 * 1024;
        const auto source_hash = Common::ComputeHash64(source.data(), source.size());
        const auto code_hash = Common::ComputeHash64(code.data(), code.size_bytes());
        const auto role_name = ComputePipelineRoleName(role);
        const auto path = fmt::format("{}calculated-{}-{:016X}-{:016X}",
            FileUtil::GetUserPath(FileUtil::UserPath::LogDir), role_name, source_hash, code_hash);
        const auto write_once = [&](const std::string& name, const void* data, std::size_t bytes) {
            if (bytes > MaxPayload) return false;
            if (FileUtil::Exists(name)) {
                FileUtil::IOFile old{name, "rb"};
                if (!old || old.GetSize() != bytes) return false;
                std::vector<u8> previous(bytes);
                return old.ReadBytes(previous.data(), bytes) == bytes &&
                       std::equal(previous.begin(), previous.end(), static_cast<const u8*>(data));
            }
            FileUtil::IOFile file{name, "wb"};
            return file && file.WriteBytes(static_cast<const u8*>(data), bytes) == bytes && file.Flush();
        };
        const bool source_saved = write_once(path + ".comp", source.data(), source.size());
        const bool code_saved = write_once(path + ".spv", code.data(), code.size_bytes());
        const auto properties = instance.GetPhysicalDevice().getProperties();
        const auto& limits = properties.limits;
        const std::string context = fmt::format(
            "schema=1\nrole={}\noperation={}\nresult={}\nerror={}\nsource_bytes={}\n"
            "spirv_bytes={}\nsource_hash64={:016X}\nspirv_hash64={:016X}\n"
            "source_saved={}\nspirv_saved={}\nbuild={}\nshader_cache_version={}\n"
            "optimizer_disabled={}\nentry=main\nspecialization=none\n"
            "vendor={}\ndevice={}\ndriver_version={}\napi_version={}\n"
            "max_push_constants={}\nmax_workgroup_invocations={}\n"
            "max_workgroup_size={},{},{}\nmax_per_stage_resources={}\n"
            "layout_and_enabled_features=published_compute_owner_and_Instance_CreateDevice\n",
            role_name, operation, error.code().value(), error.what(), source.size(), code.size_bytes(),
            source_hash, code_hash, source_saved, code_saved, Common::g_scm_rev,
            Common::g_shader_cache_version, disable_optimizer, properties.vendorID,
            properties.deviceID, properties.driverVersion, properties.apiVersion,
            limits.maxPushConstantsSize, limits.maxComputeWorkGroupInvocations,
            limits.maxComputeWorkGroupSize[0], limits.maxComputeWorkGroupSize[1],
            limits.maxComputeWorkGroupSize[2], limits.maxPerStageResources);
        const bool context_saved = write_once(path + ".txt", context.data(), context.size());
        LOG_ERROR(Render_Vulkan,
            "Calculated pipeline first failure: role={} operation={} result={} "
            "source_hash64={:016X} spirv_hash64={:016X} source_bytes={} spirv_bytes={} "
            "source_saved={} spirv_saved={} context_saved={} artifact={}",
            role_name, operation, error.code().value(), source_hash, code_hash, source.size(),
            code.size_bytes(), source_saved, code_saved, context_saved, path);
    } catch (...) {
        // CodexAstraLocal: Optional diagnostic I/O must never replace the original
        // Vulkan failure or escape a generated guest-code frame.
    }
}

struct ComputePipelineObjects {
    vk::UniqueShaderModule module;
    vk::UniquePipeline pipeline;
};

// CodexAstraLocal: Vulkan-Hpp reports pipeline rejection as vk::SystemError,
// which cannot unwind through the generated SVC caller. Normalize it here to
// the existing terminal renderer error; no pipeline retry or graphics fallback.
inline ComputePipelineObjects CreateComputePipeline(
    const Instance& instance, vk::PipelineLayout layout, std::string_view source,
    std::span<const u32> code, bool disable_optimizer, ComputePipelineRole role, bool& reported) {
    std::string_view operation = "create_shader_module";
    try {
        const auto device = instance.GetDevice();
        ComputePipelineObjects result;
        result.module = device.createShaderModuleUnique(
            {.codeSize = code.size_bytes(), .pCode = code.data()});
        operation = "create_compute_pipeline";
        result.pipeline = device.createComputePipelineUnique({},
            {.stage{.stage = vk::ShaderStageFlagBits::eCompute, .module = *result.module,
                    .pName = "main"}, .layout = layout}).value;
        return result;
    } catch (const vk::SystemError& error) {
        RecordComputePipelineFailure(instance, role, source, code, disable_optimizer,
                                     operation, error, reported);
        // CodexAstraLocal: Bound formatting in this failure path without an
        // additional heap string; runtime_error's own allocation is inherited.
        std::array<char, 512> message{};
        const auto name = ComputePipelineRoleName(role);
        std::snprintf(message.data(), message.size(),
            "Calculated %.*s %.*s failed (VkResult %d): %.320s",
            static_cast<int>(name.size()), name.data(), static_cast<int>(operation.size()),
            operation.data(), error.code().value(), error.what());
        throw VideoCore::ShaderRecoveryError(message.data());
    }
}

} // namespace Vulkan
