// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <vector>
#include "common/alignment.h"
#include "video_core/renderer_vulkan/compute_failure_packet.h"
#include "video_core/renderer_vulkan/uberhar_compute_pipeline.h"
#include "video_core/renderer_vulkan/uberhar_compute_shadow.h"
#include "video_core/renderer_vulkan/uberhar_compute_target_format.h"
#include "video_core/renderer_vulkan/vk_compute_raster.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader_recovery_error.h"
// CodexAstraLocal: Use the existing renderer's VMA allocator and feature macros;
// scratch depth is device storage, not a CPU readback or replacement framebuffer.
#include <vk_mem_alloc.h>

namespace Vulkan {
namespace {
constexpr u32 UploadBytes = 16 * 1024 * 1024;
constexpr std::size_t PipelineLimit = 32;
using namespace Pica::Shader;
using namespace Pica::Shader::Generator;

// CodexAstraLocal: General-layout copies preserve the cache's image contract
// and allow texture snapshots of a depth image before its ordered writeback.
void MemoryBarrier(vk::CommandBuffer cmd, vk::PipelineStageFlags src_stage,
                   vk::PipelineStageFlags dst_stage, vk::AccessFlags src,
                   vk::AccessFlags dst) {
    const vk::MemoryBarrier barrier{.srcAccessMask = src, .dstAccessMask = dst};
    cmd.pipelineBarrier(src_stage, dst_stage, {}, barrier, {}, {});
}
} // namespace

struct ComputeRasterizer::Impl {
    // CodexAstraLocal: Each scratch allocation is reused only in queue order.
    // Growth waits its actual last consumer before destruction; ordinary reuse
    // uses explicit transfer/compute barriers without a host read or GPU wait.
    struct Scratch {
        explicit Scratch(VmaAllocator allocator_) : allocator{allocator_} {}
        ~Scratch() { if (buffer) vmaDestroyBuffer(allocator, buffer, allocation); }
        VmaAllocator allocator;
        VkBuffer buffer{};
        VmaAllocation allocation{};
        vk::DeviceSize bytes{};
        u64 tick{};
    };
    struct Program {
        FSConfig config;
        UserConfig user;
        Profile profile;
        vk::UniqueShaderModule module;
        vk::UniquePipeline pipeline;
        u64 tick{};
        std::shared_ptr<const ComputeModuleEvidence> evidence;
    };

    Impl(const Instance& instance_, Scheduler& scheduler_, DescriptorUpdateQueue& updates_)
        : instance{instance_}, scheduler{scheduler_}, updates{updates_},
          upload{instance, scheduler, vk::BufferUsageFlagBits::eUniformBuffer |
                                         vk::BufferUsageFlagBits::eStorageBuffer, UploadBytes} {
        const auto family = instance.GetPhysicalDevice().getQueueFamilyProperties().at(
            instance.GetGraphicsQueueFamilyIndex());
        if (!(family.queueFlags & vk::QueueFlagBits::eCompute))
            throw VideoCore::ShaderRecoveryError("Calculated requires a compute-capable queue");
        constexpr auto stage = vk::ShaderStageFlagBits::eCompute;
        const std::array uniform_bindings{
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eUniformBuffer, 1, stage},
            vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eUniformTexelBuffer, 1, stage},
            vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eUniformTexelBuffer, 1, stage},
            vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eUniformTexelBuffer, 1, stage}};
        const std::array texture_bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 6, stage},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 1, stage},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eCombinedImageSampler, 1, stage}};
        const std::array raster_bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, stage},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, stage},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageImage, 1, stage},
            vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1, stage}};
        uniforms = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(),
                                                     uniform_bindings, 32);
        textures = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(),
                                                     texture_bindings, 32);
        raster = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(),
                                                   raster_bindings, 32);
        const std::array layouts{uniforms->Layout(), textures->Layout(), raster->Layout()};
        const vk::PushConstantRange push{stage, 0, sizeof(ComputeRasterPush)};
        layout = instance.GetDevice().createPipelineLayoutUnique(
            {.setLayoutCount = static_cast<u32>(layouts.size()), .pSetLayouts = layouts.data(),
             .pushConstantRangeCount = 1, .pPushConstantRanges = &push});
    }

    Program& Pipeline(const FSConfig& config, const UserConfig& user, const Profile& profile,
                      const std::shared_ptr<ComputeFailurePacket>& failure) {
        auto found = std::find_if(programs.begin(), programs.end(), [&](const Program& program) {
            return program.config == config && program.user.raw == user.raw &&
                   program.profile == profile;
        });
        if (found != programs.end()) {
            if (failure) failure->Module(found->evidence);
            return *found;
        }
        if (failure) failure->Stage(ComputeFailureStage::RasterCompile);
        const auto evaluator = GLSL::GenerateComputeFragmentEvaluator(config, user, profile);
        if (!evaluator)
            throw VideoCore::ShaderRecoveryError("Calculated fragment configuration unsupported");
        // CodexAstraLocal: Bound resident pipelines without ever dropping a draw
        // on a miss. Eviction waits the selected owner's submitted use, then the
        // exact configuration is compiled. Cold compilation cost remains real.
        if (programs.size() == PipelineLimit) {
            const auto oldest = std::min_element(programs.begin(), programs.end(),
                [](const Program& a, const Program& b) { return a.tick < b.tick; });
            scheduler.Wait(oldest->tick);
            programs.erase(oldest);
        }
        const auto source = "#version 450\n" + *evaluator +
                            std::string{ComputeTargetFormat::KernelFunctions} +
                            std::string{ComputeShadow::KernelFunctions} +
                            std::string{ComputeRasterKernel};
        auto evidence = std::make_shared<ComputeModuleEvidence>();
        evidence->role = "raster"; evidence->source = source;
        evidence->optimizer_disabled = profile.vk_disable_spirv_optimizer != 0;
        if (failure) failure->Module(evidence);
        const auto code = CompileGLSL(source, vk::ShaderStageFlagBits::eCompute, "",
                                     profile.vk_disable_spirv_optimizer != 0);
        // CodexAstraLocal: A queued failure may already hold the source-only
        // snapshot. Replace it atomically; never mutate published module vectors.
        evidence=std::make_shared<ComputeModuleEvidence>(*evidence);
        evidence->spirv=code;
        if(failure)failure->Module(evidence);
        if (code.empty())
            throw VideoCore::ShaderRecoveryError("Calculated compute module compilation failed");
        Program program{config, user, profile, {}, {}, 0, evidence};
        // CodexAstraLocal: This compute owner does not concurrently mutate
        // the legacy worker-owned driver cache; a private pipeline has no saved-
        // cache dependency and cannot cause graphics pipeline selection.
        if (failure) failure->Stage(ComputeFailureStage::RasterPipeline);
        auto objects = CreateComputePipeline(
            instance, *layout, source, code,
            profile.vk_disable_spirv_optimizer != 0, ComputePipelineRole::Raster,
            reported_creation_failure, failure.get());
        program.module = std::move(objects.module);
        program.pipeline = std::move(objects.pipeline);
        programs.push_back(std::move(program));
        return programs.back();
    }

    void EnsureScratch(std::unique_ptr<Scratch>& slot, vk::DeviceSize bytes) {
        if (slot && slot->bytes >= bytes) return;
        if (slot) scheduler.Wait(slot->tick);
        auto next = std::make_unique<Scratch>(instance.GetAllocator());
        const vk::BufferCreateInfo create{.size = bytes,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer |
                     vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst};
        const auto raw = static_cast<VkBufferCreateInfo>(create);
        const VmaAllocationCreateInfo allocation{
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        const auto result = vmaCreateBuffer(next->allocator, &raw, &allocation,
                                            &next->buffer, &next->allocation, nullptr);
        if (result != VK_SUCCESS)
            throw VideoCore::ShaderRecoveryError("Calculated target storage allocation failed");
        next->bytes = bytes;
        slot = std::move(next);
    }

    // CodexAstraLocal: Byte packing is a separate GPU buffer transform. One
    // invocation owns each packed word; no CPU pixel conversion or cross-pixel
    // atomic read/modify/write enters the native-format merger.
    void PrepareTransfer(const std::shared_ptr<ComputeFailurePacket>& failure) {
        if (pack_pipeline) {
            if (failure) { failure->Module(expand_evidence); failure->Module(pack_evidence); }
            return;
        }
        if (failure) failure->Stage(ComputeFailureStage::TransferCompile);
        constexpr auto stage = vk::ShaderStageFlagBits::eCompute;
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, stage},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, stage}};
        transfer = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(),
                                                    bindings, 32);
        const auto set_layout = transfer->Layout();
        const vk::PushConstantRange range{stage, 0, 2 * sizeof(u32)};
        transfer_layout = instance.GetDevice().createPipelineLayoutUnique(
            {.setLayoutCount = 1, .pSetLayouts = &set_layout,
             .pushConstantRangeCount = 1, .pPushConstantRanges = &range});
        const auto compile = [&](ComputePipelineRole role, std::string_view source,
                                 vk::UniqueShaderModule& module,
                                 vk::UniquePipeline& pipeline,
                                 std::shared_ptr<const ComputeModuleEvidence>& retained) {
            auto evidence = std::make_shared<ComputeModuleEvidence>();
            evidence->role = role == ComputePipelineRole::Expand ? "expand" : "pack";
            evidence->source = source; evidence->optimizer_disabled = true;
            if (failure) { failure->Stage(ComputeFailureStage::TransferCompile); failure->Module(evidence); }
            const auto code = CompileGLSL(std::string{source}, stage, "", true);
            // CodexAstraLocal: A queued failure may already hold the source-only
            // snapshot. Replace it atomically; never mutate published module vectors.
            evidence=std::make_shared<ComputeModuleEvidence>(*evidence);
            evidence->spirv=code;
            if(failure)failure->Module(evidence);
            if (code.empty())
                throw VideoCore::ShaderRecoveryError("Calculated target transfer compilation failed");
            // CodexAstraLocal: Transfer kernels have the same terminal contract
            // as guest/raster kernels; tag their exact independent role.
            if (failure) failure->Stage(ComputeFailureStage::TransferPipeline);
            auto objects = CreateComputePipeline(instance, *transfer_layout, source, code, true,
                                                  role, reported_creation_failure, failure.get());
            module = std::move(objects.module);
            pipeline = std::move(objects.pipeline);
            retained = std::move(evidence);
        };
        compile(ComputePipelineRole::Expand, ComputeTargetFormat::ExpandTransferKernel,
                expand_module, expand_pipeline, expand_evidence);
        compile(ComputePipelineRole::Pack, ComputeTargetFormat::PackTransferKernel,
                pack_module, pack_pipeline, pack_evidence);
    }

    void Draw(Surface* color, Surface* depth, vk::ImageView inactive_color_view,
              ComputeRasterPush push, const FSConfig& config, const UserConfig& user,
              const Profile& profile, const FSUniformData& fragment,
              const TextureBindings& texture_bindings, const LutBindings& luts,
              GpuVertices vertices) {
        const auto& failure = vertices.failure;
        // CodexAstraLocal TEST-ONLY LOG: Preserve actual resolved configuration,
        // ABI bytes and binding identities before they can change. Image pixels
        // are explicitly unavailable rather than reading unsafe GPU state during
        // unwinding. Remove after resource incidents have corrected replay cases.
        if (failure) {
            failure->Stage(ComputeFailureStage::ResourcePreparation);
            failure->Value("raster.config.native_abi", config);
            failure->Value("raster.user.native_abi", user);
            failure->Value("raster.profile.native_abi", profile);
            failure->Unavailable("raster.fragment_uniforms.native_abi",
                "available to synchronous owner failure only; not copied on success");
            failure->Value("raster.texture_bindings.native_abi", texture_bindings);
            failure->Value("raster.lut_bindings.native_abi", luts);
            failure->Unavailable("raster.image_pixels", "GPU-only sampled/target contents not read after failure");
            const auto surface = [&](const char* name, Surface* value) {
                if (!value) { failure->Label(name, "absent"); return; }
                const std::array<u64, 7> description{
                    static_cast<u64>(value->pixel_format), static_cast<u64>(value->traits.native),
                    value->RealExtent().width, value->RealExtent().height,
                    value->traits.transfer_support, value->traits.storage_support,
                    std::bit_cast<u64>(static_cast<VkImage>(value->Image()))};
                failure->Value(name, description);
            };
            surface("raster.color_target.u64", color); surface("raster.depth_target.u64", depth);
        }
        using Encoding = ComputeTargetFormat::Encoding;
        const auto color_format = color ? ComputeTargetFormat::Describe(color->pixel_format,
                                                    color->traits.native) : std::nullopt;
        const auto depth_format = depth ? ComputeTargetFormat::Describe(depth->pixel_format,
                                                    depth->traits.native) : std::nullopt;
        const bool shadow = config.framebuffer.shadow_rendering;
        const bool direct_color = color && color->traits.native == vk::Format::eR8G8B8A8Unorm &&
                                  color->traits.storage_support;
        const bool buffered_color = color && !direct_color;
        const bool packed_depth = depth_format && depth_format->transfer_bytes != 4;
        const bool stencil = depth_format && depth_format->has_stencil;
        const auto matching_extent = [&](Surface* surface) {
            return !surface || (surface->RealExtent().width == push.extent[0] &&
                                surface->RealExtent().height == push.extent[1]);
        };
        // CodexAstraLocal: Admission describes actual cache storage, including
        // fallback formats. Shadow bits require the existing mutable RGBA8/R32
        // representation. Every rejection precedes descriptor/image mutation.
        if ((!color && !depth) || (color && !color_format) || (depth && !depth_format) ||
            (buffered_color && !color->traits.transfer_support) ||
            (depth && !depth->traits.transfer_support) ||
            (shadow && (!direct_color || depth)) || !matching_extent(color) || !matching_extent(depth))
            throw VideoCore::ShaderRecoveryError("Calculated target representation unsupported");
        const auto limits = instance.GetPhysicalDevice().getProperties().limits;
        const vk::DeviceSize pixels = vk::DeviceSize{push.extent[0]} * push.extent[1];
        const vk::DeviceSize depth_bytes = depth ? 4 * pixels : 4;
        const vk::DeviceSize storage_bytes = depth_bytes + (stencil ? Common::AlignUp(pixels, 4ULL) : 0);
        const vk::DeviceSize color_bytes = buffered_color ? 4 * pixels : 4;
        const vk::DeviceSize color_raw = buffered_color ?
            Common::AlignUp(pixels * color_format->transfer_bytes, 4ULL) : 0;
        const vk::DeviceSize depth_raw = packed_depth ?
            Common::AlignUp(pixels * depth_format->transfer_bytes, 4ULL) : 0;
        const auto transfer_alignment = std::max<vk::DeviceSize>(4, limits.minStorageBufferOffsetAlignment);
        const auto depth_offset = Common::AlignUp(color_raw, transfer_alignment);
        const auto transfer_bytes = std::max<vk::DeviceSize>(4, depth_offset + depth_raw);
        if (!pixels || pixels > std::numeric_limits<u32>::max() / 4 || !push.extent[2] ||
            push.extent[2] % 3 || storage_bytes > limits.maxStorageBufferRange ||
            color_bytes > limits.maxStorageBufferRange || color_raw > limits.maxStorageBufferRange ||
            depth_raw > limits.maxStorageBufferRange ||
            (push.extent[0] + 7) / 8 > limits.maxComputeWorkGroupCount[0] ||
            (push.extent[1] + 7) / 8 > limits.maxComputeWorkGroupCount[1])
            throw VideoCore::ShaderRecoveryError("Calculated draw storage domain unsupported");
        if (!vertices.buffer || vertices.bytes > limits.maxStorageBufferRange ||
            vk::DeviceSize{vertices.first_word} * 4 + vk::DeviceSize{push.extent[2]} * 88 > vertices.bytes)
            throw VideoCore::ShaderRecoveryError("Calculated GPU vertex range invalid");
        auto& program = Pipeline(config, user, profile, failure);
        if (failure) failure->Stage(ComputeFailureStage::Allocation);
        EnsureScratch(depth_scratch, storage_bytes);
        EnsureScratch(color_scratch, color_bytes);
        const bool needs_transfer = buffered_color || packed_depth;
        if (needs_transfer) {
            EnsureScratch(transfer_scratch, transfer_bytes);
            PrepareTransfer(failure);
        }
        // CodexAstraLocal: All potentially flushing allocation/compilation/Map
        // preparation precedes every descriptor stamp. Only uniforms cross the
        // host upload ring; guest vertices and native pixels remain on GPU.
        const u32 upload_size = sizeof(fragment);
        if (upload_size > upload.Capacity())
            throw VideoCore::ShaderRecoveryError("Calculated upload exceeds allocated ring capacity");
        const u64 alignment = std::max<u64>(16, limits.minUniformBufferOffsetAlignment);
        if (failure) failure->Stage(ComputeFailureStage::Upload);
        const auto [mapped, offset, _] = upload.Map(upload_size, alignment);
        std::memcpy(mapped, &fragment, sizeof(fragment));
        push.geometry[2] = vertices.first_word;
        push.geometry[3] = static_cast<u32>(pixels);
        push.merger[2] = (push.merger[2] & ~(128u | 256u)) |
                        (shadow ? 128u : 0u) | (buffered_color ? 256u : 0u);
        push.reserved[0] = static_cast<u32>(color_format ? color_format->encoding : Encoding::Rgba8);
        push.reserved[1] = static_cast<u32>(depth_format ? depth_format->encoding : Encoding::D24);
        push.reserved[3] = static_cast<u32>(config.proctex.coord.Value());
        if (failure) {
            failure->Value("raster.push.native_abi", push);
            failure->Number("raster.vertex_buffer_bytes", vertices.bytes);
            failure->Number("raster.vertex_first_word", vertices.first_word);
            failure->Stage(ComputeFailureStage::Descriptors);
        }
        upload.Commit(upload_size);
        const std::array sets{uniforms->Commit(), textures->Commit(), raster->Commit()};
        updates.AddBuffer(sets[0], 2, upload.Handle(), offset, sizeof(fragment),
                          vk::DescriptorType::eUniformBuffer);
        for (u32 i = 0; i < 3; ++i) {
            updates.AddTexelBuffer(sets[0], i + 3, luts[i]);
            updates.AddImageSampler(sets[1], i, 0, texture_bindings[i].imageView,
                                   texture_bindings[i].sampler, texture_bindings[i].imageLayout);
        }
        // CodexAstraLocal: A fixed six-element layout handles the production
        // shadow-cube array. Ordinary samplers consume only its first element;
        // initialized duplicates keep unused descriptors valid without uploads.
        using TextureType = Pica::TexturingRegs::TextureConfig::TextureType;
        for (u32 face = 1; face < 6; ++face) {
            const auto& binding = texture_bindings[
                config.texture.texture0_type == TextureType::ShadowCube ? face + 2 : 0];
            updates.AddImageSampler(sets[1], 0, face, binding.imageView, binding.sampler,
                                   binding.imageLayout);
        }
        const vk::Buffer depth_buffer{depth_scratch->buffer};
        const vk::Buffer color_buffer{color_scratch->buffer};
        const vk::Buffer raw_buffer = needs_transfer ? vk::Buffer{transfer_scratch->buffer} : vk::Buffer{};
        updates.AddBuffer(sets[2], 0, vertices.buffer, 0, vertices.bytes,
                          vk::DescriptorType::eStorageBuffer);
        updates.AddBuffer(sets[2], 1, depth_buffer, 0, storage_bytes,
                          vk::DescriptorType::eStorageBuffer);
        updates.AddStorageImage(sets[2], 2, direct_color ? color->StorageView() : inactive_color_view);
        updates.AddBuffer(sets[2], 3, color_buffer, 0, color_bytes, vk::DescriptorType::eStorageBuffer);
        std::array<vk::DescriptorSet, 4> transfer_sets{};
        const auto bind_transfer = [&](u32 index, vk::DeviceSize raw_offset, vk::DeviceSize raw_size,
                                       vk::Buffer expanded, vk::DeviceSize expanded_size) {
            transfer_sets[index] = transfer->Commit();
            transfer_sets[index + 1] = transfer->Commit();
            updates.AddBuffer(transfer_sets[index], 0, raw_buffer, raw_offset, raw_size,
                              vk::DescriptorType::eStorageBuffer);
            updates.AddBuffer(transfer_sets[index], 1, expanded, 0, expanded_size,
                              vk::DescriptorType::eStorageBuffer);
            updates.AddBuffer(transfer_sets[index + 1], 0, expanded, 0, expanded_size,
                              vk::DescriptorType::eStorageBuffer);
            updates.AddBuffer(transfer_sets[index + 1], 1, raw_buffer, raw_offset, raw_size,
                              vk::DescriptorType::eStorageBuffer);
        };
        if (buffered_color) bind_transfer(0, 0, color_raw, color_buffer, color_bytes);
        if (packed_depth) bind_transfer(2, depth_offset, depth_raw, depth_buffer, depth_bytes);
        const auto depth_image = depth ? depth->Image() : vk::Image{};
        const auto color_image = buffered_color ? color->Image() : vk::Image{};
        const u32 color_bpp = color_format ? color_format->transfer_bytes : 4;
        const u32 depth_bpp = depth_format ? depth_format->transfer_bytes : 4;
        // CodexAstraLocal: Surface barriers/cache readback must include compute
        // access after this owner uses an image, even when its format is packed.
        if (color) color->is_compute = true;
        if (depth) depth->is_compute = true;
        scheduler.Record([failure, push, sets, pipeline = *program.pipeline, view_layout = *layout,
                          depth_image, color_image, depth_buffer, raw_buffer, stencil,
                          depth_bytes, depth_offset, packed_depth, buffered_color,
                          transfer_sets, color_bpp, depth_bpp,
                          transfer_view = needs_transfer ? *transfer_layout : vk::PipelineLayout{},
                          expand = needs_transfer ? *expand_pipeline : vk::Pipeline{},
                          pack = needs_transfer ? *pack_pipeline : vk::Pipeline{},
                          group_limit = limits.maxComputeWorkGroupCount[0]](vk::CommandBuffer cmd) {
            auto stage = ComputeFailureStage::RasterBind;
            try {
            constexpr auto general = vk::ImageLayout::eGeneral;
            constexpr auto memory = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            constexpr auto compute_access = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            MemoryBarrier(cmd, vk::PipelineStageFlagBits::eAllCommands,
                          vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader,
                          memory, vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite |
                                  compute_access);
            const vk::BufferImageCopy color_copy{.bufferOffset = 0,
                .imageSubresource{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .imageExtent{push.extent[0], push.extent[1], 1}};
            auto depth_copy = color_copy;
            depth_copy.bufferOffset = packed_depth ? depth_offset : 0;
            depth_copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
            auto stencil_copy = depth_copy;
            stencil_copy.bufferOffset = depth_bytes;
            stencil_copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eStencil;
            if (color_image) cmd.copyImageToBuffer(color_image, general, raw_buffer, color_copy);
            if (depth_image) {
                cmd.copyImageToBuffer(depth_image, general, packed_depth ? raw_buffer : depth_buffer, depth_copy);
                if (stencil) cmd.copyImageToBuffer(depth_image, general, depth_buffer, stencil_copy);
            }
            if (color_image || depth_image)
                MemoryBarrier(cmd, vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eTransferWrite,
                              compute_access);
            const auto transform = [&](vk::Pipeline operation, u32 set_index, u32 bpp, bool packing) {
                const std::array<u32, 2> transfer_push{push.extent[0] * push.extent[1], bpp};
                const u32 work = packing ? (transfer_push[0] * bpp + 3) / 4 : transfer_push[0];
                const u32 groups = (work + 63) / 64;
                const u32 x = std::min(groups, group_limit);
                cmd.bindPipeline(vk::PipelineBindPoint::eCompute, operation);
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, transfer_view, 0,
                                       transfer_sets[set_index], {});
                cmd.pushConstants(transfer_view, vk::ShaderStageFlagBits::eCompute, 0,
                                  sizeof(transfer_push), transfer_push.data());
                cmd.dispatch(x, (groups + x - 1) / x, 1);
            };
            if (buffered_color) transform(expand, 0, color_bpp, false);
            if (packed_depth) transform(expand, 2, depth_bpp, false);
            if (buffered_color || packed_depth)
                MemoryBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eComputeShader, compute_access, compute_access);
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, view_layout, 0, sets, {});
            cmd.pushConstants(view_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
            stage = ComputeFailureStage::RasterDispatch;
            cmd.dispatch((push.extent[0] + 7) / 8, (push.extent[1] + 7) / 8, 1);
            if (buffered_color || packed_depth) {
                MemoryBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eComputeShader, compute_access, compute_access);
                if (buffered_color) transform(pack, 1, color_bpp, true);
                if (packed_depth) transform(pack, 3, depth_bpp, true);
            }
            if (color_image || depth_image) {
                MemoryBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader,
                              vk::PipelineStageFlagBits::eTransfer, compute_access,
                              vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite);
                if (color_image) cmd.copyBufferToImage(raw_buffer, color_image, general, color_copy);
                if (depth_image) {
                    cmd.copyBufferToImage(packed_depth ? raw_buffer : depth_buffer, depth_image,
                                          general, depth_copy);
                    if (stencil) cmd.copyBufferToImage(depth_buffer, depth_image, general, stencil_copy);
                }
            }
            MemoryBarrier(cmd, vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
                          vk::PipelineStageFlagBits::eAllCommands,
                          vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite, memory);
            } catch (const vk::SystemError& error) {
                if (failure) failure->Notice(stage, error.what(), error.code().value());
                throw VideoCore::ShaderRecoveryError("Calculated raster command recording failed");
            } catch (const std::exception& error) {
                if (failure) failure->Notice(stage, error.what());
                throw;
            }
        });
        if (failure) failure->Number("raster_recorded_tick", scheduler.CurrentTick());
        program.tick = depth_scratch->tick = color_scratch->tick = scheduler.CurrentTick();
        if (needs_transfer) transfer_scratch->tick = scheduler.CurrentTick();
        upload.MarkDrawUse();
        scheduler.MakeDirty(StateFlags::Pipeline | StateFlags::DescriptorSets);
    }

    const Instance& instance;
    Scheduler& scheduler;
    DescriptorUpdateQueue& updates;
    StreamBuffer upload;
    std::unique_ptr<DescriptorHeap> uniforms, textures, raster, transfer;
    vk::UniquePipelineLayout layout;
    vk::UniquePipelineLayout transfer_layout;
    vk::UniqueShaderModule expand_module, pack_module;
    vk::UniquePipeline expand_pipeline, pack_pipeline;
    std::shared_ptr<const ComputeModuleEvidence> expand_evidence, pack_evidence;
    std::unique_ptr<Scratch> depth_scratch, color_scratch, transfer_scratch;
    std::vector<Program> programs;
    bool reported_creation_failure{};
};

ComputeRasterizer::ComputeRasterizer(const Instance& instance, Scheduler& scheduler,
                                   DescriptorUpdateQueue& updates)
    : impl{std::make_unique<Impl>(instance, scheduler, updates)} {}
ComputeRasterizer::~ComputeRasterizer() = default;

void ComputeRasterizer::DrawGpu(Surface* color, Surface* depth, vk::ImageView inactive_color_view,
    const ComputeRasterPush& push, const FSConfig& config, const UserConfig& user,
    const Profile& profile, const FSUniformData& uniforms, const TextureBindings& textures,
    const LutBindings& luts, const GpuVertices& vertices) {
    const auto recording=impl->scheduler.RecordComputeFailure(vertices.failure);
    try {
        impl->Draw(color, depth, inactive_color_view, push, config, user, profile, uniforms, textures,
                   luts, vertices);
    } catch (const vk::SystemError& error) {
        if (vertices.failure) {
            vertices.failure->Value("raster.fragment_uniforms.native_abi", uniforms);
            vertices.failure->Failure(error.what(), error.code().value());
        }
        throw VideoCore::ShaderRecoveryError("Calculated raster preparation failed");
    } catch (const std::exception& error) {
        if (vertices.failure) {
            vertices.failure->Value("raster.fragment_uniforms.native_abi", uniforms);
            vertices.failure->Failure(error.what());
        }
        throw;
    }
}
} // namespace Vulkan
