// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <limits> // AstraPro: Reject wraparound before speculative memory reads.
#include "common/alignment.h"
#include "common/literals.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "common/uberhar_activity.h" // CodexAstraUlt: Existing atomic run correlation.
#include "common/uberhar_test_profile.h" // CodexAstraLocal: Capture only optional Combo routes.
#include "common/scope_exit.h" // AstraPro: Clear speculative draw state on every exit.
#include "core/core.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/vertex_loader.h" // CodexAstraLocal: Authoritative capture input descriptors.
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture/texture_decode.h"

namespace Vulkan {

namespace {

MICROPROFILE_DEFINE(Vulkan_VS, "Vulkan", "Vertex Shader Setup", MP_RGB(192, 128, 128));
MICROPROFILE_DEFINE(Vulkan_GS, "Vulkan", "Geometry Shader Setup", MP_RGB(128, 192, 128));
MICROPROFILE_DEFINE(Vulkan_Drawing, "Vulkan", "Drawing", MP_RGB(128, 128, 192));

using TriangleTopology = Pica::PipelineRegs::TriangleTopology;
using VideoCore::SurfaceType;

using namespace Common::Literals;
using namespace Pica::Shader::Generator;

constexpr u64 STREAM_BUFFER_SIZE = 64_MiB;
constexpr u64 UNIFORM_BUFFER_SIZE = 8_MiB;
constexpr u64 TEXTURE_BUFFER_SIZE = 2_MiB;

constexpr vk::BufferUsageFlags BUFFER_USAGE =
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer;

struct DrawParams {
    u32 vertex_count;
    s32 vertex_offset;
    u32 binding_count;
    std::array<u32, 16> bindings;
    bool is_indexed;
};

[[nodiscard]] u64 TextureBufferSize(const Instance& instance) {
    // Use the smallest texel size from the texel views
    // which corresponds to eR32G32Sfloat
    const u64 max_size = instance.MaxTexelBufferElements() * 8;
    return std::min(max_size, TEXTURE_BUFFER_SIZE);
}

} // Anonymous namespace

RasterizerVulkan::RasterizerVulkan(Memory::MemorySystem& memory, Pica::PicaCore& pica,
                                   VideoCore::CustomTexManager& custom_tex_manager,
                                   VideoCore::RendererBase& renderer,
                                   Frontend::EmuWindow& emu_window, const Instance& instance,
                                   Scheduler& scheduler, RenderManager& renderpass_cache,
                                   DescriptorUpdateQueue& update_queue_, u32 image_count)
    : RasterizerAccelerated{memory, pica}, instance{instance}, scheduler{scheduler},
      renderpass_cache{renderpass_cache}, update_queue{update_queue_},
      pipeline_cache{instance, scheduler, renderpass_cache, update_queue},
      runtime{instance, scheduler, renderpass_cache, update_queue, image_count},
      res_cache{memory, custom_tex_manager, runtime, regs, renderer},
      stream_buffer{instance, scheduler, BUFFER_USAGE, STREAM_BUFFER_SIZE},
      uniform_buffer{instance, scheduler, vk::BufferUsageFlagBits::eUniformBuffer,
                     UNIFORM_BUFFER_SIZE},
      texture_buffer{instance, scheduler, vk::BufferUsageFlagBits::eUniformTexelBuffer,
                     TextureBufferSize(instance)},
      texture_lf_buffer{instance, scheduler, vk::BufferUsageFlagBits::eUniformTexelBuffer,
                        TextureBufferSize(instance)},
      async_shaders{Settings::values.async_shader_compilation.GetValue()} {

    // AstraEH: All test modes share cached CPU vertex/geometry processing.
    if (Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom) {
        compute_rect = std::make_unique<ComputeRectRenderer>(instance, scheduler, update_queue);
    }
    vertex_buffers.fill(stream_buffer.Handle());

    // Query uniform buffer alignment.
    uniform_buffer_alignment = instance.UniformMinAlignment();
    uniform_size_aligned_vs_pica =
        Common::AlignUp<u32>(sizeof(VSPicaUniformData), uniform_buffer_alignment);
    uniform_size_aligned_vs = Common::AlignUp<u32>(sizeof(VSUniformData), uniform_buffer_alignment);
    uniform_size_aligned_fs = Common::AlignUp<u32>(sizeof(FSUniformData), uniform_buffer_alignment);

    // Define vertex layout for software shaders
    MakeSoftwareVertexLayout();
    pipeline_info.state.vertex_layout = software_layout;

    const vk::Device device = instance.GetDevice();
    texture_lf_view = device.createBufferViewUnique({
        .buffer = texture_lf_buffer.Handle(),
        .format = vk::Format::eR32G32Sfloat,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    });
    texture_rg_view = device.createBufferViewUnique({
        .buffer = texture_buffer.Handle(),
        .format = vk::Format::eR32G32Sfloat,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    });
    texture_rgba_view = device.createBufferViewUnique({
        .buffer = texture_buffer.Handle(),
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = 0,
        .range = VK_WHOLE_SIZE,
    });

    scheduler.RegisterOnSubmit([&renderpass_cache] { renderpass_cache.EndRendering(); });

    // Prepare the static buffer descriptor set.
    const auto buffer_set = pipeline_cache.Acquire(DescriptorHeapType::Buffer);
    update_queue.AddBuffer(buffer_set, 0, uniform_buffer.Handle(), 0, sizeof(VSPicaUniformData));
    update_queue.AddBuffer(buffer_set, 1, uniform_buffer.Handle(), 0, sizeof(VSUniformData));
    update_queue.AddBuffer(buffer_set, 2, uniform_buffer.Handle(), 0, sizeof(FSUniformData));
    update_queue.AddTexelBuffer(buffer_set, 3, *texture_lf_view);
    update_queue.AddTexelBuffer(buffer_set, 4, *texture_rg_view);
    update_queue.AddTexelBuffer(buffer_set, 5, *texture_rgba_view);

    const auto texture_set = pipeline_cache.Acquire(DescriptorHeapType::Texture);
    Surface& null_surface = res_cache.GetSurface(VideoCore::NULL_SURFACE_ID);
    Sampler& null_sampler = res_cache.GetSampler(VideoCore::NULL_SAMPLER_ID);

    // Prepare texture and utility descriptor sets.
    for (u32 i = 0; i < 3; i++) {
        update_queue.AddImageSampler(texture_set, i, 0, null_surface.ImageView(),
                                     null_sampler.Handle());
    }

    const auto utility_set = pipeline_cache.Acquire(DescriptorHeapType::Utility);
    update_queue.AddStorageImage(utility_set, 0, null_surface.StorageView());
    update_queue.AddImageSampler(utility_set, 1, 0, null_surface.ImageView(),
                                 null_sampler.Handle());
    update_queue.Flush();
}

RasterizerVulkan::~RasterizerVulkan() {
    // AstraPro Log Line: One bounded lifetime summary, never a per-draw log.
    LOG_INFO(Render_Vulkan,
             "Uberhar uniform transport totals: pending_vs_resyncs={} "
             "scope=rasterizer_lifetime observed=unuploaded_clip_viewport_block_revisited",
             pending_vs_uniform_resyncs);
    // AstraPro Log Line: One final capacity summary, no crash-cause inference.
    LOG_INFO(Render_Vulkan,
             "Uberhar fixed attributes totals: max_bytes={} over_legacy_reservation={} "
             "reserved_bytes=272 scope=rasterizer_lifetime",
             fixed_attribute_max_bytes, fixed_attribute_over_legacy);
    // CodexAstraUlt Log Line: Extend the input-only lifetime summary with the
    // quaternion-correction boundary; a killed process may skip this destructor.
    LOG_INFO(Render_Vulkan,
             "Uberhar GPU input fallback totals: zero_stride={} short_stride={} "
             "default_attribute={} register_alias={} quaternion_interpolation={} "
             "scope=rasterizer_lifetime action=cpu",
             ready_vertex_zero_stride_rejections,
             ready_vertex_layout_rejections[static_cast<std::size_t>(
                 ReadyVertexPolicy::InputLayoutIssue::ShortStride)],
             ready_vertex_layout_rejections[static_cast<std::size_t>(
                 ReadyVertexPolicy::InputLayoutIssue::DefaultAttribute)],
             ready_vertex_layout_rejections[static_cast<std::size_t>(
                 ReadyVertexPolicy::InputLayoutIssue::RegisterAlias)],
             ready_vertex_quaternion_rejections);
    if (ready_vertex_output_checks != 0) {
        // CodexAstraUlt Log Line: One optional-route coverage summary; neither
        // an un-rejected shader nor this count establishes general output parity.
        // CodexAstraLocal Log Line: Retain the recovered bounded summary so real
        // route coverage and memo reuse can be checked after normal game exit.
        LOG_INFO(Render_Vulkan,
                 "Uberhar GPU output fallback totals: checked={} consumed_w={} "
                 "never_written_w={} memo_entries={} memo_hits={} memo_scans={} "
                 "scope=rasterizer_lifetime action=cpu guard=missing_write_union_only",
                 ready_vertex_output_checks, ready_vertex_output_w_checks,
                 ready_vertex_output_rejections, ready_vertex_output_writes.Size(),
                 ready_vertex_output_writes.Hits(), ready_vertex_output_writes.Scans());
    }
    if (compute_rect) {
        // AstraEH: Queued compute/timestamp commands must finish before their owners die.
        scheduler.Finish();
        compute_rect->Poll();
        compute_rect->Report();
    }
    // CodexAstraLocal: Use the existing drained shutdown opportunity. Capture
    // adds no GPU wait and reports incomplete evidence honestly after failure.
    if (vertex_capture)
        vertex_capture->Finish(scheduler.TrySubmittedTick(),
                               scheduler.GetMasterSemaphore()->KnownGpuTick());
    // CodexAstraUlt: Members still own their resources here. The Instance emits a
    // second sample after those members die, without adding a shutdown GPU wait.
    instance.ReportMemoryUsage("before_dependents", pipeline_cache.GetProgramID(),
                               Common::UberharActivity::session.load(std::memory_order_relaxed),
                               scheduler.CurrentTick(), scheduler.GetMasterSemaphore()->KnownGpuTick());
}

void RasterizerVulkan::TickFrame() {
    scheduler.WaitWorker();
    if (vertex_capture) {
        // CodexAstraLocal: One coherent phase/startup token avoids false edges;
        // submission is observed only after the existing worker drain.
        const auto activity = Common::UberharActivity::Capture();
        vertex_capture->NextSwap(pipeline_cache.GetProgramID(), activity.run, activity.token & 3U,
            (activity.token & 4U) != 0, scheduler.TrySubmittedTick(),
            scheduler.GetMasterSemaphore()->KnownGpuTick());
    }
    res_cache.TickFrame();
    // AstraEH: Read only completed GPU queries; do not add a per-frame GPU wait.
    if (compute_rect)
        compute_rect->Poll();
    // CodexAstraUlt: Reuse the renderer owner's frame cadence. Idle/stalled rendering
    // may delay a native sample; Android process health keeps its existing IO cadence.
    if ((memory_diagnostic_frames++ & 63) == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_memory_snapshot) {
            instance.ReportMemoryUsage(next_memory_snapshot == std::chrono::steady_clock::time_point{}
                                           ? "start" : "progress",
                                       pipeline_cache.GetProgramID(),
                                       Common::UberharActivity::session.load(std::memory_order_relaxed),
                                       scheduler.CurrentTick(),
                                       scheduler.GetMasterSemaphore()->KnownGpuTick());
            next_memory_snapshot = now + std::chrono::seconds{30};
        }
    }
}

void RasterizerVulkan::LoadDefaultDiskResources(
    const std::atomic_bool& stop_loading, const VideoCore::DiskResourceLoadCallback& callback) {

    u64 program_id;
    if (Core::System::GetInstance().GetAppLoader().ReadProgramId(program_id) !=
        Loader::ResultStatus::Success) {
        program_id = 0;
    }

    if (callback) {
        callback(VideoCore::LoadCallbackStage::Prepare, 0, 0, "");
    }

    pipeline_cache.SetProgramID(program_id);
    // CodexAstraLocal: Cold Android loading bypasses the rights-switch
    // hook, so it must reach the same one-shot diagnostic initializer.
    InitializeVertexCapture(program_id);
    pipeline_cache.SetAccurateMul(accurate_mul);
    pipeline_cache.LoadCache(stop_loading, callback);
    // AstraEH: The compute program is prepared before the loading screen completes.
    if (compute_rect && !stop_loading)
        compute_rect->Initialize(pipeline_cache.DriverCache());

    if (callback) {
        callback(VideoCore::LoadCallbackStage::Complete, 0, 0, "");
    }
}

void RasterizerVulkan::SyncDrawState() {
    SyncDrawUniforms();

    // SyncCullMode();
    pipeline_info.state.rasterization.cull_mode.Assign(regs.rasterizer.cull_mode);
    // If the framebuffer is flipped, request to also flip vulkan viewport
    const bool is_flipped = regs.framebuffer.framebuffer.IsFlipped();
    pipeline_info.state.rasterization.flip_viewport.Assign(is_flipped);
    // SyncBlendEnabled();
    pipeline_info.state.blending.blend_enable = regs.framebuffer.output_merger.alphablend_enable;
    // SyncBlendFuncs();
    pipeline_info.state.blending.color_blend_eq.Assign(
        regs.framebuffer.output_merger.alpha_blending.blend_equation_rgb);
    pipeline_info.state.blending.alpha_blend_eq.Assign(
        regs.framebuffer.output_merger.alpha_blending.blend_equation_a);
    pipeline_info.state.blending.src_color_blend_factor.Assign(
        regs.framebuffer.output_merger.alpha_blending.factor_source_rgb);
    pipeline_info.state.blending.dst_color_blend_factor.Assign(
        regs.framebuffer.output_merger.alpha_blending.factor_dest_rgb);
    pipeline_info.state.blending.src_alpha_blend_factor.Assign(
        regs.framebuffer.output_merger.alpha_blending.factor_source_a);
    pipeline_info.state.blending.dst_alpha_blend_factor.Assign(
        regs.framebuffer.output_merger.alpha_blending.factor_dest_a);
    // SyncBlendColor();
    pipeline_info.dynamic_info.blend_color = regs.framebuffer.output_merger.blend_const.raw;
    // SyncLogicOp();
    // SyncColorWriteMask();
    pipeline_info.state.blending.logic_op = regs.framebuffer.output_merger.logic_op;

    const u32 color_mask = regs.framebuffer.framebuffer.allow_color_write != 0
                               ? (regs.framebuffer.output_merger.depth_color_mask >> 8) & 0xF
                               : 0;
    pipeline_info.state.blending.color_write_mask = color_mask;

    // SyncStencilTest();
    const auto& stencil_test = regs.framebuffer.output_merger.stencil_test;
    const bool test_enable = stencil_test.enable && regs.framebuffer.framebuffer.depth_format ==
                                                        Pica::FramebufferRegs::DepthFormat::D24S8;

    pipeline_info.state.depth_stencil.stencil_test_enable.Assign(test_enable);
    pipeline_info.state.depth_stencil.stencil_fail_op.Assign(stencil_test.action_stencil_fail);
    pipeline_info.state.depth_stencil.stencil_pass_op.Assign(stencil_test.action_depth_pass);
    pipeline_info.state.depth_stencil.stencil_depth_fail_op.Assign(stencil_test.action_depth_fail);
    pipeline_info.state.depth_stencil.stencil_compare_op.Assign(stencil_test.func);
    pipeline_info.dynamic_info.stencil_reference = stencil_test.reference_value;
    pipeline_info.dynamic_info.stencil_compare_mask = stencil_test.input_mask;
    // SyncStencilWriteMask();
    pipeline_info.dynamic_info.stencil_write_mask =
        (regs.framebuffer.framebuffer.allow_depth_stencil_write != 0)
            ? static_cast<u32>(regs.framebuffer.output_merger.stencil_test.write_mask)
            : 0;
    // SyncDepthTest();
    const bool test_enabled = regs.framebuffer.output_merger.depth_test_enable == 1 ||
                              regs.framebuffer.output_merger.depth_write_enable == 1;
    const auto compare_op = regs.framebuffer.output_merger.depth_test_enable == 1
                                ? regs.framebuffer.output_merger.depth_test_func.Value()
                                : Pica::FramebufferRegs::CompareFunc::Always;

    pipeline_info.state.depth_stencil.depth_test_enable.Assign(test_enabled);
    pipeline_info.state.depth_stencil.depth_compare_op.Assign(compare_op);
    // SyncDepthWriteMask();
    const bool write_enable = (regs.framebuffer.framebuffer.allow_depth_stencil_write != 0 &&
                               regs.framebuffer.output_merger.depth_write_enable);
    pipeline_info.state.depth_stencil.depth_write_enable.Assign(write_enable);
}

void RasterizerVulkan::SetupVertexArray() {
    const auto [vs_input_index_min, vs_input_index_max, vs_input_size] = vertex_info;
    if (vertex_capture && vertex_capture->WantsPayload()) {
        // CodexAstraLocal: Describe the production loader without reading guest
        // memory. Bound its diagnostic/error paths and isolate exceptions;
        // the unchanged real upload proceeds if optional description fails.
        try {
            const auto& loaders = regs.pipeline.vertex_attributes.attribute_loaders;
            if (std::any_of(std::begin(loaders), std::end(loaders),
                            [](const auto& loader) { return loader.component_count > 12; })) {
                vertex_capture->EndDraw();
            } else {
                const Pica::VertexLoader loader{memory, regs.pipeline};
                std::array<Pica::NativeInputAttribute, 16> native_inputs{};
                for (u32 i = 0; i < native_inputs.size(); ++i)
                    native_inputs[i] = loader.DescribeNativeInput(i);
                vertex_capture->PreparePayload(regs, pica.vs_setup, pica.input_default_attributes,
                    native_inputs, loader.GetNumTotalAttributes(), vs_input_index_min, vs_input_index_max);
            }
        } catch (...) {
            vertex_capture->EndDraw();
        }
    }
    auto [array_ptr, array_offset, invalidate] = stream_buffer.Map(vs_input_size, 16);

    /**
     * The Nintendo 3DS has 12 attribute loaders which are used to tell the GPU
     * how to interpret vertex data. The program firsts sets GPUREG_ATTR_BUF_BASE to the base
     * address containing the vertex array data. The data for each attribute loader (i) can be found
     * by adding GPUREG_ATTR_BUFi_OFFSET to the base address. Attribute loaders can be thought
     * as something analogous to Vulkan bindings. The user can store attributes in separate loaders
     * or interleave them in the same loader.
     **/
    const auto& vertex_attributes = regs.pipeline.vertex_attributes;
    const PAddr base_address = vertex_attributes.GetPhysicalBaseAddress(); // GPUREG_ATTR_BUF_BASE
    const u32 stride_alignment = instance.GetMinVertexStrideAlignment();

    VertexLayout& layout = pipeline_info.state.vertex_layout;
    layout.binding_count = 0;
    layout.attribute_count = 16;
    enable_attributes.fill(false);

    u32 buffer_offset = 0;
    for (const auto& loader : vertex_attributes.attribute_loaders) {
        if (loader.component_count == 0 || loader.byte_count == 0) {
            continue;
        }

        // Analyze the attribute loader by checking which attributes it provides
        u32 offset = 0;
        for (u32 comp = 0; comp < loader.component_count && comp < 12; comp++) {
            const u32 attribute_index = loader.GetComponent(comp);
            if (attribute_index >= 12) {
                // Attribute ids 12, to 15 signify 4, 8, 12 and 16-byte paddings respectively.
                offset = Common::AlignUp(offset, 4);
                offset += (attribute_index - 11) * 4;
                continue;
            }

            const u32 size = vertex_attributes.GetNumElements(attribute_index);
            if (size == 0) {
                continue;
            }

            offset =
                Common::AlignUp(offset, vertex_attributes.GetElementSizeInBytes(attribute_index));

            const u32 input_reg = regs.vs.GetRegisterForAttribute(attribute_index);
            const auto format = vertex_attributes.GetFormat(attribute_index);

            VertexAttribute& attribute = layout.attributes[input_reg];
            attribute.binding.Assign(layout.binding_count);
            attribute.location.Assign(input_reg);
            attribute.offset.Assign(offset);
            attribute.type.Assign(format);
            attribute.size.Assign(size);

            enable_attributes[input_reg] = true;
            offset += vertex_attributes.GetStride(attribute_index);
        }

        const PAddr data_addr =
            base_address + loader.data_offset + (vs_input_index_min * loader.byte_count);
        const u32 vertex_num = vs_input_index_max - vs_input_index_min + 1;
        u32 data_size = loader.byte_count * vertex_num;
        res_cache.FlushRegion(data_addr, data_size);

        const MemoryRef src_ref = memory.GetPhysicalRef(data_addr);
        if (src_ref.GetSize() < data_size) {
            LOG_ERROR(Render_Vulkan,
                      "Vertex buffer size {} exceeds available space {} at address {:#016X}",
                      data_size, src_ref.GetSize(), data_addr);
        }

        const u8* src_ptr = src_ref.GetPtr();
        u8* dst_ptr = array_ptr + buffer_offset;

        // Align stride up if required by Vulkan implementation.
        const u32 aligned_stride =
            Common::AlignUp(static_cast<u32>(loader.byte_count), stride_alignment);
        if (aligned_stride == loader.byte_count) {
            std::memcpy(dst_ptr, src_ptr, data_size);
        } else {
            for (std::size_t vertex = 0; vertex < vertex_num; vertex++) {
                std::memcpy(dst_ptr + vertex * aligned_stride, src_ptr + vertex * loader.byte_count,
                            loader.byte_count);
            }
        }

        // CodexAstraLocal: Copy only initialized uploaded row bytes, after the
        // existing cache flush/conversion and before stream reuse can occur.
        if (vertex_capture && vertex_capture->WantsPayload())
            vertex_capture->CopyVertex(layout.binding_count, loader.data_offset,
                loader.byte_count, aligned_stride, vertex_num, dst_ptr);

        // Create the binding associated with this loader
        VertexBinding& binding = layout.bindings[layout.binding_count];
        binding.binding.Assign(layout.binding_count);
        binding.fixed.Assign(0);
        // Will be adjusted on pipeline build, to keep the info transferable.
        binding.byte_count.Assign(loader.byte_count);

        // Keep track of the binding offsets so we can bind the vertex buffer later
        binding_offsets[layout.binding_count++] = static_cast<u32>(array_offset + buffer_offset);
        buffer_offset += Common::AlignUp(aligned_stride * vertex_num, 4);
    }

    stream_buffer.Commit(buffer_offset);

    // Assign the rest of the attributes to the last binding
    SetupFixedAttribs();
}

void RasterizerVulkan::SetupFixedAttribs() {
    const auto& vertex_attributes = regs.pipeline.vertex_attributes;
    VertexLayout& layout = pipeline_info.state.vertex_layout;

    // AstraPro: Reserve the leading fallback vector PLUS up to sixteen distinct
    // fixed-register vectors. The old 16-vector reservation could write/commit
    // 272 bytes into a 256-byte reservation. This is a proven capacity bound,
    // not evidence that Dark Moon used this worst-case layout.
    auto [fixed_ptr, fixed_offset, _] = stream_buffer.Map(17 * sizeof(Common::Vec4f), 0);
    binding_offsets[layout.binding_count] = static_cast<u32>(fixed_offset);

    // Reserve the last binding for fixed and default attributes
    // Place the default attrib at offset zero for easy access
    static const Common::Vec4f default_attrib{0.f, 0.f, 0.f, 1.f};
    std::memcpy(fixed_ptr, default_attrib.AsArray(), sizeof(Common::Vec4f));

    // Find all fixed attributes and assign them to the last binding
    u32 offset = sizeof(Common::Vec4f);
    for (std::size_t i = 0; i < 16; i++) {
        if (vertex_attributes.IsDefaultAttribute(i)) {
            const u32 reg = regs.vs.GetRegisterForAttribute(i);
            if (!enable_attributes[reg]) {
                const auto& attr = pica.input_default_attributes[i];
                const std::array data = {attr.x.ToFloat32(), attr.y.ToFloat32(), attr.z.ToFloat32(),
                                         attr.w.ToFloat32()};

                const u32 data_size = sizeof(float) * static_cast<u32>(data.size());
                std::memcpy(fixed_ptr + offset, data.data(), data_size);

                VertexAttribute& attribute = layout.attributes[reg];
                attribute.binding.Assign(layout.binding_count);
                attribute.location.Assign(reg);
                attribute.offset.Assign(offset);
                attribute.type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
                attribute.size.Assign(4);

                offset += data_size;
                enable_attributes[reg] = true;
            }
        }
    }

    // Loop one more time to find unused attributes and assign them to the default one
    // If the attribute is just disabled, shove the default attribute to avoid
    // errors if the shader ever decides to use it.
    for (u32 i = 0; i < 16; i++) {
        if (!enable_attributes[i]) {
            VertexAttribute& attribute = layout.attributes[i];
            attribute.binding.Assign(layout.binding_count);
            attribute.location.Assign(i);
            attribute.offset.Assign(0);
            attribute.type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
            attribute.size.Assign(4);
        }
    }

    // Define the fixed+default binding
    VertexBinding& binding = layout.bindings[layout.binding_count];
    binding.binding.Assign(layout.binding_count++);
    binding.fixed.Assign(1);
    binding.byte_count.Assign(offset);

    // AstraPro: Count layouts exceeding the former reservation without logging
    // each draw or attributing the earlier crash to an unobserved layout.
    fixed_attribute_max_bytes = std::max(fixed_attribute_max_bytes, offset);
    fixed_attribute_over_legacy += offset > 16 * sizeof(Common::Vec4f);
    // CodexAstraLocal: Freeze the actual initialized default upload before
    // stream-buffer reuse; absent/discovery requests copy no payload.
    if (vertex_capture && vertex_capture->WantsPayload())
        vertex_capture->CopyFixed({fixed_ptr, offset});
    stream_buffer.Commit(offset);
}

bool RasterizerVulkan::SetupVertexShader() {
    MICROPROFILE_SCOPE(Vulkan_VS);
    return pipeline_cache.UseProgrammableVertexShader(regs, pica.vs_setup,
                                                      pipeline_info.state.vertex_layout);
}

bool RasterizerVulkan::SetupGeometryShader() {
    MICROPROFILE_SCOPE(Vulkan_GS);

    if (regs.pipeline.use_gs != Pica::PipelineRegs::UseGS::No) {
        LOG_ERROR(Render_Vulkan, "Accelerate draw doesn't support geometry shader");
        return false;
    }

    // Enable the quaternion fix-up geometry-shader only if we are actually doing per-fragment
    // lighting and care about proper quaternions. Otherwise just use standard vertex+fragment
    // shaders. We also don't need a geometry shader if the barycentric extension is supported,
    // but that will be decided later as the GS config needs to be cached anyways.
    if (regs.lighting.disable) {
        pipeline_cache.UseTrivialGeometryShader();
        return true;
    }

    return pipeline_cache.UseFixedGeometryShader(regs);
}

// AstraPro: PICA admits only complete, independent no-GS triangle lists. Keep
// Native unchanged; Combo uses the inherited GPU VS/GS translation only when ready.
bool RasterizerVulkan::AccelerateDrawBatchReady(bool is_indexed) {
    // CodexAstraLocal: The ordinal is global within the optional entry point,
    // independent of filters. Every early return discards staged evidence only.
    if (vertex_capture)
        vertex_capture->BeginDraw(regs, pica.vs_setup, is_indexed);
    SCOPE_EXIT({ if (vertex_capture) vertex_capture->EndDraw(); });
    // AstraPro: Never bypass buffered CPU output left by an earlier non-draw.
    if (!vertex_batch.empty())
        return false;
    // AstraPro: Revalidate actual debugger/assembly/winding/topology state. The
    // Shader enum alone does not mean a guest geometry shader is executing.
    if (!ReadyVertexPolicy::IsEligible(pica.GetReadyGpuVertexAdmission()))
        return false;
    // CodexAstraUlt-2: Reject known input divergence before reads or GPU setup;
    // PicaCore will execute the complete CPU batch with the live zero-stride data.
    if (ReadyVertexPolicy::HasActiveZeroStrideLoader(regs.pipeline)) {
        if (++ready_vertex_zero_stride_rejections <= 4) {
            // CodexAstraUlt-2 Log Line: First four per rasterizer lifetime;
            // CodexAstraUlt-2: This records fallback coverage, not a crash diagnosis.
            LOG_INFO(Render_Vulkan,
                     "Uberhar GPU input fallback: reason=zero_stride title={:016X} "
                     "ordinal={} vertices={} action=cpu limit=4",
                     pipeline_cache.GetProgramID(), ready_vertex_zero_stride_rejections,
                     regs.pipeline.num_vertices);
        }
        return false;
    }
    // CodexAstraUlt: Extend the CodexAstraUlt-2 early input-parity fallback without
    // replacing its zero-stride accounting. Reject reproduced default, alias and
    // copied-tail differences before any speculative reads, pipelines or uploads.
    const auto input_issue = ReadyVertexPolicy::ClassifyInputLayout(regs.pipeline, regs.vs);
    if (input_issue != ReadyVertexPolicy::InputLayoutIssue::None) {
        auto& rejections = ready_vertex_layout_rejections[static_cast<std::size_t>(input_issue)];
        if (++rejections <= 4) {
            // CodexAstraUlt Log Line: Four records per reason per rasterizer;
            // coverage evidence only, not proof of a title's corruption or crash cause.
            LOG_INFO(Render_Vulkan,
                     "Uberhar GPU input fallback: reason={} title={:016X} "
                     "ordinal={} vertices={} action=cpu limit=4",
                     ReadyVertexPolicy::InputLayoutIssueName(input_issue),
                     pipeline_cache.GetProgramID(), rejections, regs.pipeline.num_vertices);
        }
        return false;
    }
    // CodexAstraUlt: The inherited Android accelerator disables geometry shaders
    // even when that loses quaternion sign correction. Optional Combo promotion
    // must retain the CPU triangle result when neither correction path is enabled;
    // keep the Custom policy and driver workarounds unchanged.
    if (!ReadyVertexPolicy::CanPreserveQuaternionInterpolation(
            !regs.lighting.disable, instance.UseGeometryShaders(),
            instance.IsFragmentShaderBarycentricSupported())) {
        if (++ready_vertex_quaternion_rejections <= 4) {
            // CodexAstraUlt Log Line: Bounded coverage evidence for the missing
            // correction path, not a claim that a specific title's image is fixed.
            LOG_INFO(Render_Vulkan,
                     "Uberhar GPU input fallback: reason=quaternion_interpolation title={:016X} "
                     "ordinal={} vertices={} action=cpu limit=4",
                     pipeline_cache.GetProgramID(), ready_vertex_quaternion_rejections,
                     regs.pipeline.num_vertices);
        }
        return false;
    }
    // CodexAstraUlt: Contain the reproduced mapped-output W=0/W=1 difference
    // before any uploads. A never-written consumed W stays zero throughout the
    // CPU batch; retaining that whole batch also preserves register carry/FIFO.
    // This leaves generated defaults and Native/Custom execution unchanged.
    // CodexAstraLocal: LoadVertices constructs a zeroed ShaderUnit for each draw,
    // then keeps it for that draw's FIFO misses. Reject before index reads and
    // GPU setup; any possible W write leaves conditional/carry parity unresolved.
    ++ready_vertex_output_checks;
    const u16 consumed_w = ReadyVertexPolicy::ConsumedOutputW(regs.vs, regs.rasterizer);
    if (consumed_w != 0) {
        ++ready_vertex_output_w_checks;
        const u64 program_hash = pica.vs_setup.GetProgramCodeHash();
        const u64 swizzle_hash = pica.vs_setup.GetSwizzleDataHash();
        const u16 written_w = ready_vertex_output_writes.Get(
            pica.vs_setup.GetProgramCode(), pica.vs_setup.GetSwizzleData(), program_hash,
            swizzle_hash);
        const u16 missing_w = consumed_w & ~written_w;
        if (missing_w != 0) {
            if (++ready_vertex_output_rejections <= 8) {
                // CodexAstraUlt Log Line: Eight details per rasterizer, no guest
                // words/vertices and no attribution to a particular visible fault.
                // CodexAstraLocal Log Line: Revalidate this early-return coverage
                // independently of the moon flashing and ghost-corruption results.
                LOG_INFO(Render_Vulkan,
                         "Uberhar GPU output fallback: reason=consumed_w_never_written "
                         "title={:016X} ordinal={} program={:016X} swizzle={:016X} entry={} "
                         "consumed_w={:04X} possible_w={:04X} missing_w={:04X} vertices={} "
                         "action=cpu limit=8 proof=missing_write_union_only",
                         pipeline_cache.GetProgramID(), ready_vertex_output_rejections,
                         program_hash, swizzle_hash, regs.vs.main_offset.Value(), consumed_w,
                         written_w, missing_w, regs.pipeline.num_vertices);
            }
            return false;
        }
    }
    // AstraPro: The stock accelerator assumes a valid index range. Validate it
    // before its min/max scan; malformed optional input retains legacy handling.
    if (is_indexed) {
        const u64 address = static_cast<u64>(regs.pipeline.vertex_attributes.GetPhysicalBaseAddress()) +
                            regs.pipeline.index_array.offset;
        if (address > std::numeric_limits<u32>::max())
            return false;
        const auto indices = memory.GetPhysicalRef(static_cast<PAddr>(address));
        const u32 width = regs.pipeline.index_array.format ? 2 : 1;
        if (!indices.GetPtr() || indices.GetSize() / width < regs.pipeline.num_vertices)
            return false;
    } else if (static_cast<u64>(regs.pipeline.vertex_offset) + regs.pipeline.num_vertices - 1 >
               std::numeric_limits<u32>::max()) {
        return false;
    }
    // AstraPro: Reject optional fragment misses before scanning/flushing/uploading
    // vertices. A previous user-config snapshot may conservatively defer a draw;
    // the final config/profile/modules are checked again after full state sync.
    if (!pipeline_cache.ReadyGpuFragmentPreflight(regs, user_config))
        return false;
    ready_vertex_attempt = true;
    ready_vertex_pipeline = nullptr;
    SCOPE_EXIT({
        ready_vertex_attempt = false;
        ready_vertex_pipeline = nullptr;
    });
    return AccelerateDrawBatch(is_indexed);
}

bool RasterizerVulkan::AccelerateDrawBatch(bool is_indexed) {
    // AstraEH: A decision cannot leak into a later batch or an unsupported draw.
    cpu_bridge = {};
    if (regs.pipeline.use_gs != Pica::PipelineRegs::UseGS::No) {
        if (regs.pipeline.gs_config.mode != Pica::PipelineRegs::GSMode::Point) {
            return false;
        }
        if (regs.pipeline.triangle_topology != Pica::PipelineRegs::TriangleTopology::Shader) {
            return false;
        }
    }

    pipeline_info.state.rasterization.topology.Assign(regs.pipeline.triangle_topology);
    if (regs.pipeline.triangle_topology == TriangleTopology::Fan &&
        !instance.IsTriangleFanSupported()) {
        LOG_DEBUG(Render_Vulkan,
                  "Skipping accelerated draw with unsupported triangle fan topology");
        return false;
    }

    // Vertex data setup might involve scheduler flushes so perform it
    // early to avoid invalidating our state in the middle of the draw.
    vertex_info = AnalyzeVertexArray(is_indexed, instance.GetMinVertexStrideAlignment());
    if (ready_vertex_attempt && vertex_info.vs_input_size > ReadyVertexPolicy::MaxUploadBytes)
        return false; // AstraPro: Bound speculative upload cost; CPU still renders the draw.
    if (vertex_info.Invalid()) {
        // AstraPro: An optional promotion must not change the CPU path's handling
        // of an invalid vertex analysis. Ordinary hardware mode retains its behavior.
        return !ready_vertex_attempt;
    }
    if (ready_vertex_attempt) {
        // AstraPro: Do not inherit the ordinary hardware path's log-and-copy
        // behavior for a short source mapping. Validate every upload before any
        // of these ranges is copied. No byte content survives beyond this draw.
        const auto& attributes = regs.pipeline.vertex_attributes;
        const u64 count = static_cast<u64>(vertex_info.vs_input_index_max) -
                          vertex_info.vs_input_index_min + 1;
        for (const auto& loader : attributes.attribute_loaders) {
            if (!loader.component_count || !loader.byte_count)
                continue;
            const u64 address = static_cast<u64>(attributes.GetPhysicalBaseAddress()) +
                                loader.data_offset +
                                static_cast<u64>(vertex_info.vs_input_index_min) * loader.byte_count;
            const u64 size = count * loader.byte_count;
            if (address > std::numeric_limits<u32>::max() ||
                size > (u64{1} << 32) - address)
                return false;
            const auto source = memory.GetPhysicalRef(static_cast<PAddr>(address));
            if (!source.GetPtr() || source.GetSize() < size)
                return false;
        }
    }
    SetupVertexArray();

    if (!SetupVertexShader()) {
        return false;
    }
    if (!SetupGeometryShader()) {
        return false;
    }
    // AstraPro: Decompilation can do foreground work, but shader-module and
    // driver-pipeline compilation are never waited on by this optional route.
    if (ready_vertex_attempt && !pipeline_cache.ReadyVertexShaders())
        return false;

    return Draw(true, is_indexed);
}

bool RasterizerVulkan::AccelerateDrawBatchInternal(bool is_indexed) {
    if (is_indexed) {
        SetupIndexArray();
    }

    const bool wait_built = !async_shaders || regs.pipeline.num_vertices <= 6;
    if (!pipeline_cache.BindPipeline(pipeline_info, wait_built, nullptr, !cpu_bridge.preferred,
                                     ready_vertex_pipeline)) {
        // AstraPro: A ready-key mismatch requires CPU recovery, not a skipped draw.
        return !ready_vertex_attempt;
    }

    const DrawParams params = {
        .vertex_count = regs.pipeline.num_vertices,
        .vertex_offset = -static_cast<s32>(vertex_info.vs_input_index_min),
        .binding_count = pipeline_info.state.vertex_layout.binding_count,
        .bindings = binding_offsets,
        .is_indexed = is_indexed,
    };

    // CodexAstraLocal: Share the unchanged draw body between ordinary
    // recording and the opt-in token published only after that real draw.
    const auto draw = [this, params](vk::CommandBuffer cmdbuf) {
        std::array<vk::DeviceSize, 16> offsets;
        std::transform(params.bindings.begin(), params.bindings.end(), offsets.begin(),
                       [](u32 offset) { return static_cast<vk::DeviceSize>(offset); });
        cmdbuf.bindVertexBuffers(0, params.binding_count, vertex_buffers.data(), offsets.data());
        if (params.is_indexed) {
            cmdbuf.drawIndexed(params.vertex_count, 1, 0, params.vertex_offset, 0);
        } else {
            cmdbuf.draw(params.vertex_count, 1, 0, 0);
        }
    };

    // CodexAstraLocal: Ordinary draws keep the original command owner. Even
    // an empty shared capture token is constructed only for an armed attempt.
    if (ready_vertex_attempt && vertex_capture && vertex_capture->HasAttempt()) {
        const auto bound = pipeline_cache.CaptureVertexBinding(
            regs, pica.vs_setup, pipeline_info, ready_vertex_pipeline->Key());
        // CodexAstraLocal: Final tick comes after every upload/bind that could
        // flush. Commit only stages an owner; Recorded follows the actual draw.
        if (bound) {
            const auto evidence =
                vertex_capture->Commit(*bound, uniform_buffer, scheduler.CurrentTick());
            if (evidence) {
                scheduler.Record([draw, evidence](vk::CommandBuffer cmdbuf) {
                    draw(cmdbuf);
                    evidence.Recorded();
                });
                return true;
            }
        }
    }
    scheduler.Record(draw);

    return true;
}

void RasterizerVulkan::SetupIndexArray() {
    const bool index_u8 = regs.pipeline.index_array.format == 0;
    const bool native_u8 = index_u8 && instance.IsIndexTypeUint8Supported();
    const u32 index_buffer_size = regs.pipeline.num_vertices * (native_u8 ? 1 : 2);
    const vk::IndexType index_type = native_u8 ? vk::IndexType::eUint8EXT : vk::IndexType::eUint16;

    const u8* index_data =
        memory.GetPhysicalPointer(regs.pipeline.vertex_attributes.GetPhysicalBaseAddress() +
                                  regs.pipeline.index_array.offset);

    auto [index_ptr, index_offset, _] = stream_buffer.Map(index_buffer_size, 2);

    if (index_u8 && !native_u8) {
        u16* index_ptr_u16 = reinterpret_cast<u16*>(index_ptr);
        for (u32 i = 0; i < regs.pipeline.num_vertices; i++) {
            index_ptr_u16[i] = index_data[i];
        }
    } else {
        std::memcpy(index_ptr, index_data, index_buffer_size);
    }

    // CodexAstraLocal: Retain original width plus actual widened bytes
    // in submission order; a later guest-memory reread is not evidence.
    if (vertex_capture && vertex_capture->WantsPayload())
        vertex_capture->CopyIndices(index_u8 ? 1 : 2, native_u8 ? 1 : 2,
                                   {index_ptr, index_buffer_size});

    stream_buffer.Commit(index_buffer_size);

    scheduler.Record(
        [this, index_offset = index_offset, index_type = index_type](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindIndexBuffer(stream_buffer.Handle(), index_offset, index_type);
        });
}

void RasterizerVulkan::DrawTriangles() {
    if (vertex_batch.empty()) {
        // AstraEH: Invalid/empty CPU output must not leave a prepared handle latched.
        cpu_bridge = {};
        return;
    }

    if (cpu_bridge.ready) {
        // AstraEH: Measure only CPU preparation; exclude this function's GPU setup/submission.
        const auto elapsed = std::chrono::steady_clock::now() - cpu_bridge_start;
        pipeline_cache.RecordCpuBridgeVertices(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
            static_cast<u32>(vertex_batch.size()));
    }

    pipeline_info.state.rasterization.topology.Assign(Pica::PipelineRegs::TriangleTopology::List);
    pipeline_info.state.vertex_layout = software_layout;

    pipeline_cache.UseTrivialVertexShader();
    pipeline_cache.UseTrivialGeometryShader();

    Draw(false, false);
    // AstraEH: The prepared generic pipeline is valid for exactly this guest batch.
    cpu_bridge = {};
}

bool RasterizerVulkan::Draw(bool accelerate, bool is_indexed) {
    MICROPROFILE_SCOPE(Vulkan_Drawing);
    SyncDrawState();

    const bool shadow_rendering = regs.framebuffer.IsShadowRendering();
    const bool has_stencil = regs.framebuffer.HasStencil();

    const bool write_color_fb = shadow_rendering || pipeline_info.GetFinalColorWriteMask(instance);
    const bool write_depth_fb = pipeline_info.IsDepthWriteEnabled();
    const bool using_color_fb =
        regs.framebuffer.framebuffer.GetColorBufferPhysicalAddress() != 0 && write_color_fb;
    const bool using_depth_fb =
        !shadow_rendering && regs.framebuffer.framebuffer.GetDepthBufferPhysicalAddress() != 0 &&
        (write_depth_fb || regs.framebuffer.output_merger.depth_test_enable != 0 ||
         (has_stencil && pipeline_info.state.depth_stencil.stencil_test_enable));

    const auto fb_helper = res_cache.GetFramebufferSurfaces(using_color_fb, using_depth_fb);
    const Framebuffer* framebuffer = fb_helper.Framebuffer();
    if (!framebuffer->Handle()) {
        return true;
    }

    pipeline_info.state.attachments.color = framebuffer->Format(SurfaceType::Color);
    pipeline_info.state.attachments.depth = framebuffer->Format(SurfaceType::Depth);

    // AstraEH: Admit a complete compute operation before texture/fragment setup.
    // Unsupported draws retain the native route, and the framebuffer helper still
    // publishes correct cache invalidation for either route when this scope exits.
    std::optional<ComputeRectPacket> compute_packet;
    int timing_slot = -1;
    if (compute_rect && !accelerate) {
        ++compute_rect->considered;
        if (const auto reasons = ComputeRectStateRejections(regs); reasons != 0) {
            compute_rect->RejectState(reasons);
        } else if (!framebuffer->color_id || framebuffer->color_level != 0 ||
                   framebuffer->Format(SurfaceType::Color) != VideoCore::PixelFormat::RGBA8) {
            ++compute_rect->format_rejected;
        } else {
            auto& surface = res_cache.GetSurface(framebuffer->color_id);
            const auto viewport = fb_helper.Viewport();
            const auto rect = fb_helper.DrawRect();
            if (surface.traits.native != vk::Format::eR8G8B8A8Unorm ||
                !surface.traits.storage_support || surface.Image() != framebuffer->Images()[0]) {
                ++compute_rect->format_rejected;
            } else {
                compute_packet =
                    MakeComputeRect(regs, std::span<const HardwareVertex>{vertex_batch},
                                    {viewport.x, viewport.y, viewport.width, viewport.height},
                                    {static_cast<s32>(rect.left), static_cast<s32>(rect.bottom),
                                     static_cast<s32>(rect.right), static_cast<s32>(rect.top)});
                if (!compute_packet) {
                    ++compute_rect->geometry_rejected;
                } else if (compute_rect->Choose(*compute_packet)) {
                    renderpass_cache.EndRendering();
                    timing_slot = compute_rect->ReserveSample(true, compute_packet->PixelCount());
                    compute_rect->BeginSample(timing_slot);
                    compute_rect->Draw(surface, *compute_packet);
                    compute_rect->EndSample(timing_slot);
                    vertex_batch.clear();
                    return true;
                }
            }
        }
        ++compute_rect->native_draws;
    }

    // Update scissor uniforms
    const auto [scissor_x1, scissor_y2, scissor_x2, scissor_y1] = fb_helper.Scissor();
    if (fs_data.scissor_x1 != scissor_x1 || fs_data.scissor_x2 != scissor_x2 ||
        fs_data.scissor_y1 != scissor_y1 || fs_data.scissor_y2 != scissor_y2) {

        fs_data.scissor_x1 = scissor_x1;
        fs_data.scissor_x2 = scissor_x2;
        fs_data.scissor_y1 = scissor_y1;
        fs_data.scissor_y2 = scissor_y2;
        fs_data_dirty = true;
    }

    // Sync and bind the texture surfaces
    SyncTextureUnits(framebuffer);
    SyncUtilityTextures(framebuffer);

    // Sync and bind the shader
    pipeline_cache.UseFragmentShader(regs, user_config, ready_vertex_attempt);

    if (ready_vertex_attempt) {
        ready_vertex_pipeline = pipeline_cache.PrepareReadyGpuVertex(pipeline_info);
        if (!ready_vertex_pipeline) {
            // AstraPro: Preparation did not write pixels. Do not publish a false
            // framebuffer owner before CPU recovery re-enters the normal path.
            fb_helper.CancelInvalidation();
            return false;
        }
    }

    if (accelerate && !ready_vertex_attempt) {
        // AstraEH: No draw, index binding, uniforms or render pass has been submitted
        // for this batch yet. Returning false invokes PicaCore's existing CPU path.
        cpu_bridge = pipeline_cache.PrepareCpuFallback(pipeline_info, software_layout,
                                                       regs.pipeline.num_vertices);
        if (cpu_bridge.ready) {
            cpu_bridge_start = std::chrono::steady_clock::now();
            return false;
        }
    }

    // Sync the LUTs within the texture buffer
    SyncAndUploadLUTs();
    SyncAndUploadLUTsLF();
    UploadUniforms(accelerate);

    const auto draw_rect = fb_helper.DrawRect();
    // Configure viewport and scissor
    const auto viewport = fb_helper.Viewport();
    pipeline_info.dynamic_info.viewport = Common::Rectangle<s32>{
        viewport.x,
        viewport.y,
        viewport.x + viewport.width,
        viewport.y + viewport.height,
    };
    pipeline_info.dynamic_info.scissor = draw_rect;

    // AstraEH: Prepare/bind the native pipeline before recording its timestamp.
    // This prevents an initial compilation stall being mistaken for slow GPU math.
    // Queries must be reset outside a render pass; only measured test routes split it.
    if (compute_rect && compute_packet)
        timing_slot = compute_rect->ReserveSample(false, compute_packet->PixelCount());
    const bool prebound = timing_slot >= 0;
    if (prebound) {
        renderpass_cache.EndRendering();
        // CodexAstraLocal: Only an actual CPU draw carries this software ABI token;
        // hardware prebinding cannot request the independent CPU-fragment bank.
        pipeline_cache.BindPipeline(pipeline_info, true, cpu_bridge.ready, true, nullptr,
                                    accelerate ? nullptr : &software_layout);
        compute_rect->BeginSample(timing_slot);
    }
    renderpass_cache.BeginRendering(framebuffer, draw_rect);

    // AstraPro: Sample synchronized state, not a preflight's stale dynamic info.
    // Only every 4096th completed preparation reads a clock, with a five-second
    // ceiling. No queue flush, GPU readback, render-pass split or per-draw text.
    if (((++diagnostic_draws) & 4095U) == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_draw_snapshot) {
            next_draw_snapshot = now + std::chrono::seconds{5};
            // AstraPro Log Line: Sparse state evidence, not a GPU timing/sample
            // of a confirmed bad frame. Guest addresses identify render targets.
            LOG_INFO(Render_Vulkan,
                     "Uberhar draw snapshot: schema=1 title={:016X} ordinal={} "
                     "accelerated={} ready_attempt={} pipeline={:016X} stages={} "
                     "topology={} vertices={} lighting={} shadow={} clip={} flip={} "
                     "viewport=[{},{},{},{}] scissor=[{},{},{},{}] "
                     "color_addr={:08X} depth_addr={:08X} output_mask={:04X} "
                     "input_count={} fixed_max_bytes={} sample=host_state_not_gpu_output",
                     pipeline_cache.GetProgramID(), diagnostic_draws, accelerate,
                     ready_vertex_attempt, ready_vertex_pipeline ? ready_vertex_pipeline->Key() : 0,
                     ready_vertex_pipeline ? ready_vertex_pipeline->ShaderStageMask() : 0,
                     static_cast<u32>(regs.pipeline.triangle_topology.Value()),
                     regs.pipeline.num_vertices, !regs.lighting.disable, shadow_rendering,
                     vs_data.enable_clip1, vs_data.flip_viewport,
                     viewport.x, viewport.y, viewport.width, viewport.height,
                     scissor_x1, scissor_y1, scissor_x2, scissor_y2,
                     regs.framebuffer.framebuffer.GetColorBufferPhysicalAddress(),
                     regs.framebuffer.framebuffer.GetDepthBufferPhysicalAddress(),
                     regs.vs.output_mask.Value(), regs.vs.max_input_attribute_index.Value() + 1,
                     fixed_attribute_max_bytes);
        }
    }

    // Draw the vertex batch
    bool succeeded = true;
    if (accelerate) {
        succeeded = AccelerateDrawBatchInternal(is_indexed);
    } else {
        // AstraEH: A ready bridge bypasses creation of an unnecessary CPU-specialized PSO.
        if (!prebound)
            // CodexAstraLocal: CPU assembly has already produced HardwareVertex;
            // optional ready fragment selection keeps this exact upload/layout.
            pipeline_cache.BindPipeline(pipeline_info, true, cpu_bridge.ready, true, nullptr,
                                        &software_layout);

        const u32 vertex_count = static_cast<u32>(vertex_batch.size());
        const u32 vertex_size = vertex_count * sizeof(HardwareVertex);
        const auto [buffer, offset, _] = stream_buffer.Map(vertex_size, sizeof(HardwareVertex));

        std::memcpy(buffer, vertex_batch.data(), vertex_size);
        stream_buffer.Commit(vertex_size);

        scheduler.Record([this, offset = offset, vertex_count](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindVertexBuffers(0, stream_buffer.Handle(), offset);
            cmdbuf.draw(vertex_count, 1, 0, 0);
        });
    }

    // AstraEH: This sample covers the native draw's GPU work, not its compilation wait.
    if (compute_rect)
        compute_rect->EndSample(timing_slot);
    // AstraPro: No draw occurred on a rejected ready-pipeline binding. Keep the
    // CPU retry's original framebuffer ownership; previously queued uploads are inert.
    if (!succeeded && ready_vertex_attempt)
        fb_helper.CancelInvalidation();
    vertex_batch.clear();
    return succeeded;
}

void RasterizerVulkan::SyncTextureUnits(const Framebuffer* framebuffer) {
    using TextureType = Pica::TexturingRegs::TextureConfig::TextureType;

    const auto pica_textures = regs.texturing.GetTextures();
    const bool use_cube_heap =
        pica_textures[0].enabled && pica_textures[0].config.type == TextureType::ShadowCube;
    const auto texture_set = pipeline_cache.Acquire(use_cube_heap ? DescriptorHeapType::Texture
                                                                  : DescriptorHeapType::Texture);

    for (u32 texture_index = 0; texture_index < pica_textures.size(); ++texture_index) {
        const auto& texture = pica_textures[texture_index];

        // If the texture unit is disabled bind a null surface to it
        if (!texture.enabled) {
            switch (texture.config.type.Value()) {
            case TextureType::TextureCube:
            case TextureType::ShadowCube: {
                Surface& null_surface = res_cache.GetSurface(VideoCore::NULL_SURFACE_CUBE_ID);
                const Sampler& null_sampler = res_cache.GetSampler(VideoCore::NULL_SURFACE_CUBE_ID);
                update_queue.AddImageSampler(texture_set, texture_index, 0,
                                             null_surface.ImageView(), null_sampler.Handle());
                break;
            }
            default: {
                Surface& null_surface = res_cache.GetSurface(VideoCore::NULL_SURFACE_ID);
                const Sampler& null_sampler = res_cache.GetSampler(VideoCore::NULL_SURFACE_ID);
                update_queue.AddImageSampler(texture_set, texture_index, 0,
                                             null_surface.ImageView(), null_sampler.Handle());
                break;
            }
            }
            continue;
        }

        // Handle special tex0 configurations
        if (texture_index == 0) {
            switch (texture.config.type.Value()) {
            case TextureType::Shadow2D: {
                Surface& surface = res_cache.GetTextureSurface(texture);
                Sampler& sampler = res_cache.GetSampler(texture.config);
                surface.flags |= VideoCore::SurfaceFlagBits::ShadowSource;
                update_queue.AddImageSampler(texture_set, texture_index, 0, surface.StorageView(),
                                             sampler.Handle());
                continue;
            }
            case TextureType::ShadowCube: {
                BindShadowCube(texture, texture_set);
                continue;
            }
            case TextureType::TextureCube: {
                BindTextureCube(texture, texture_set);
                continue;
            }
            default:
                break;
            }
        }

        // Bind the texture provided by the rasterizer cache
        Surface& surface = res_cache.GetTextureSurface(texture);
        Sampler& sampler = res_cache.GetSampler(texture.config);
        const vk::ImageView color_view = framebuffer->ImageView(SurfaceType::Color);
        const bool is_feedback_loop = color_view == surface.FramebufferView();
        const vk::ImageView texture_view =
            is_feedback_loop ? surface.CopyImageView() : surface.ImageView();
        update_queue.AddImageSampler(texture_set, texture_index, 0, texture_view, sampler.Handle());
    }
}

void RasterizerVulkan::SyncUtilityTextures(const Framebuffer* framebuffer) {
    const bool shadow_writing = regs.framebuffer.IsShadowRendering();
    bool shadow_reading = regs.lighting.config0.enable_shadow;
    // Ensure the shadow-texture slot is actually enabled
    if (shadow_reading) {
        const u32 shadow_texture_unit = regs.lighting.config0.shadow_selector.Value();
        const auto shadow_texture = regs.texturing.GetTextures()[shadow_texture_unit];
        shadow_reading &= shadow_texture.enabled;
    }

    const auto utility_set = pipeline_cache.Acquire(DescriptorHeapType::Utility);

    // Reading and writing are mutually exclusive
    assert(!(shadow_writing && shadow_reading));

    if (shadow_writing) {
        update_queue.AddStorageImage(utility_set, 0, framebuffer->ImageView(SurfaceType::Color));
    } else if (shadow_reading) {
        const u32 shadow_texture_unit = regs.lighting.config0.shadow_selector.Value();
        const auto shadow_texture = regs.texturing.GetTextures()[shadow_texture_unit];
        Surface& shadow_surface = res_cache.GetTextureSurface(shadow_texture);
        update_queue.AddStorageImage(utility_set, 0, shadow_surface.StorageView());
    } else {
        Surface& null_surface = res_cache.GetSurface(VideoCore::NULL_SURFACE_ID);
        update_queue.AddStorageImage(utility_set, 0, null_surface.StorageView());
    }
}

void RasterizerVulkan::BindShadowCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                                      vk::DescriptorSet texture_set) {
    using CubeFace = Pica::TexturingRegs::CubeFace;
    auto info = Pica::Texture::TextureInfo::FromPicaRegister(texture.config, texture.format);
    constexpr std::array faces = {
        CubeFace::PositiveX, CubeFace::NegativeX, CubeFace::PositiveY,
        CubeFace::NegativeY, CubeFace::PositiveZ, CubeFace::NegativeZ,
    };

    Sampler& sampler = res_cache.GetSampler(texture.config);

    for (CubeFace face : faces) {
        const u32 binding = static_cast<u32>(face);
        info.physical_address = regs.texturing.GetCubePhysicalAddress(face);

        const VideoCore::SurfaceId surface_id = res_cache.GetTextureSurface(info);
        Surface& surface = res_cache.GetSurface(surface_id);
        surface.flags |= VideoCore::SurfaceFlagBits::ShadowSource;
        update_queue.AddImageSampler(texture_set, 0, binding, surface.StorageView(),
                                     sampler.Handle());
    }
}

void RasterizerVulkan::BindTextureCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                                       vk::DescriptorSet texture_set) {
    using CubeFace = Pica::TexturingRegs::CubeFace;
    const VideoCore::TextureCubeConfig config = {
        .px = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveX),
        .nx = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeX),
        .py = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveY),
        .ny = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeY),
        .pz = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveZ),
        .nz = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeZ),
        .width = texture.config.width,
        .levels = texture.config.lod.max_level + 1,
        .format = texture.format,
    };

    Surface& surface = res_cache.GetTextureCube(config);
    Sampler& sampler = res_cache.GetSampler(texture.config);
    update_queue.AddImageSampler(texture_set, 0, 0, surface.ImageView(), sampler.Handle());
}

void RasterizerVulkan::FlushAll() {
    res_cache.FlushAll();
}

void RasterizerVulkan::FlushRegion(PAddr addr, u32 size) {
    res_cache.FlushRegion(addr, size);
}

void RasterizerVulkan::InvalidateRegion(PAddr addr, u32 size) {
    res_cache.InvalidateRegion(addr, size);
}

void RasterizerVulkan::FlushAndInvalidateRegion(PAddr addr, u32 size) {
    res_cache.FlushRegion(addr, size);
    res_cache.InvalidateRegion(addr, size);
}

void RasterizerVulkan::ClearAll(bool flush) {
    res_cache.ClearAll(flush);
}

bool RasterizerVulkan::AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) {
    return res_cache.AccelerateDisplayTransfer(config);
}

bool RasterizerVulkan::AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) {
    return res_cache.AccelerateTextureCopy(config);
}

bool RasterizerVulkan::AccelerateFill(const Pica::MemoryFillConfig& config) {
    return res_cache.AccelerateFill(config);
}

bool RasterizerVulkan::AccelerateDisplay(const Pica::FramebufferConfig& config,
                                         PAddr framebuffer_addr, u32 pixel_stride,
                                         ScreenInfo& screen_info) {
    if (framebuffer_addr == 0) [[unlikely]] {
        return false;
    }

    VideoCore::SurfaceParams src_params;
    src_params.addr = framebuffer_addr;
    src_params.width = std::min(config.width.Value(), pixel_stride);
    src_params.height = config.height;
    src_params.stride = pixel_stride;
    src_params.is_tiled = false;
    src_params.pixel_format = VideoCore::PixelFormatFromGPUPixelFormat(config.color_format);
    src_params.UpdateParams();

    const auto [src_surface_id, src_rect] =
        res_cache.GetSurfaceSubRect(src_params, VideoCore::ScaleMatch::Ignore, true);

    if (!src_surface_id) {
        return false;
    }

    Surface& src_surface = res_cache.GetSurface(src_surface_id);
    const u32 scaled_width = src_surface.GetScaledWidth();
    const u32 scaled_height = src_surface.GetScaledHeight();

    screen_info.texcoords = Common::Rectangle<f32>(
        (float)src_rect.bottom / (float)scaled_height, (float)src_rect.left / (float)scaled_width,
        (float)src_rect.top / (float)scaled_height, (float)src_rect.right / (float)scaled_width);

    screen_info.image_view = src_surface.ImageView();

    return true;
}

void RasterizerVulkan::MakeSoftwareVertexLayout() {
    constexpr std::array sizes = {4, 4, 2, 2, 2, 1, 4, 3};

    software_layout = VertexLayout{
        .binding_count = 1,
        .attribute_count = 8,
    };

    for (u32 i = 0; i < software_layout.binding_count; i++) {
        VertexBinding& binding = software_layout.bindings[i];
        binding.binding.Assign(i);
        binding.fixed.Assign(0);
        binding.byte_count.Assign(sizeof(HardwareVertex));
    }

    u32 offset = 0;
    for (u32 i = 0; i < 8; i++) {
        VertexAttribute& attribute = software_layout.attributes[i];
        attribute.binding.Assign(0);
        attribute.location.Assign(i);
        attribute.offset.Assign(offset);
        attribute.type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
        attribute.size.Assign(sizes[i]);
        offset += sizes[i] * sizeof(float);
    }
}

void RasterizerVulkan::SyncAndUploadLUTsLF() {
    constexpr std::size_t max_size =
        sizeof(Common::Vec2f) * 256 * Pica::LightingRegs::NumLightingSampler +
        sizeof(Common::Vec2f) * 128; // fog

    if (!pica.lighting.lut_dirty && !pica.fog.lut_dirty) {
        return;
    }

    std::size_t bytes_used = 0;
    auto [buffer, offset, invalidate] = texture_lf_buffer.Map(max_size, sizeof(Common::Vec4f));

    if (invalidate) {
        pica.lighting.lut_dirty = pica.lighting.LutAllDirty;
        pica.fog.lut_dirty = true;
    }

    // Sync the lighting luts
    while (pica.lighting.lut_dirty) {
        u32 index = std::countr_zero(pica.lighting.lut_dirty);
        pica.lighting.lut_dirty &= ~(1 << index);

        Common::Vec2f* new_data = reinterpret_cast<Common::Vec2f*>(buffer + bytes_used);
        const auto& source_lut = pica.lighting.luts[index];
        for (u32 i = 0; i < source_lut.size(); i++) {
            new_data[i] = {source_lut[i].ToFloat(), source_lut[i].DiffToFloat()};
        }
        fs_data.lighting_lut_offset[index / 4][index % 4] =
            static_cast<int>((offset + bytes_used) / sizeof(Common::Vec2f));
        fs_data_dirty = true;
        bytes_used += source_lut.size() * sizeof(Common::Vec2f);
    }

    // Sync the fog lut
    if (pica.fog.lut_dirty) {
        Common::Vec2f* new_data = reinterpret_cast<Common::Vec2f*>(buffer + bytes_used);
        for (u32 i = 0; i < pica.fog.lut.size(); i++) {
            new_data[i] = {pica.fog.lut[i].ToFloat(), pica.fog.lut[i].DiffToFloat()};
        }
        fs_data.fog_lut_offset = static_cast<int>((offset + bytes_used) / sizeof(Common::Vec2f));
        fs_data_dirty = true;
        bytes_used += pica.fog.lut.size() * sizeof(Common::Vec2f);
        pica.fog.lut_dirty = false;
    }

    texture_lf_buffer.Commit(static_cast<u32>(bytes_used));
}

void RasterizerVulkan::SyncAndUploadLUTs() {
    const auto& proctex = pica.proctex;
    constexpr std::size_t max_size =
        sizeof(Common::Vec2f) * 128 * 3 + // proctex: noise + color + alpha
        sizeof(Common::Vec4f) * 256 +     // proctex
        sizeof(Common::Vec4f) * 256;      // proctex diff

    if (!pica.proctex.lut_dirty) {
        return;
    }

    std::size_t bytes_used = 0;
    auto [buffer, offset, invalidate] = texture_buffer.Map(max_size, sizeof(Common::Vec4f));

    if (invalidate) {
        pica.proctex.table_dirty = pica.proctex.TableAllDirty;
    }

    // helper function for SyncProcTexNoiseLUT/ColorMap/AlphaMap
    const auto sync_proctex_value_lut =
        [&](const std::array<Pica::PicaCore::ProcTex::ValueEntry, 128>& lut, int& lut_offset) {
            Common::Vec2f* new_data = reinterpret_cast<Common::Vec2f*>(buffer + bytes_used);
            for (u32 i = 0; i < lut.size(); i++) {
                new_data[i] = {lut[i].ToFloat(), lut[i].DiffToFloat()};
            }
            lut_offset = static_cast<int>((offset + bytes_used) / sizeof(Common::Vec2f));
            fs_data_dirty = true;
            bytes_used += lut.size() * sizeof(Common::Vec2f);
        };

    // Sync the proctex noise lut
    if (pica.proctex.noise_lut_dirty) {
        sync_proctex_value_lut(proctex.noise_table, fs_data.proctex_noise_lut_offset);
    }

    // Sync the proctex color map
    if (pica.proctex.color_map_dirty) {
        sync_proctex_value_lut(proctex.color_map_table, fs_data.proctex_color_map_offset);
    }

    // Sync the proctex alpha map
    if (pica.proctex.alpha_map_dirty) {
        sync_proctex_value_lut(proctex.alpha_map_table, fs_data.proctex_alpha_map_offset);
    }

    // Sync the proctex lut
    if (pica.proctex.lut_dirty) {
        Common::Vec4f* new_data = reinterpret_cast<Common::Vec4f*>(buffer + bytes_used);
        for (u32 i = 0; i < proctex.color_table.size(); i++) {
            new_data[i] = proctex.color_table[i].ToVector() / 255.0f;
        }
        fs_data.proctex_lut_offset =
            static_cast<int>((offset + bytes_used) / sizeof(Common::Vec4f));
        fs_data_dirty = true;
        bytes_used += proctex.color_table.size() * sizeof(Common::Vec4f);
    }

    // Sync the proctex difference lut
    if (pica.proctex.diff_lut_dirty) {
        Common::Vec4f* new_data = reinterpret_cast<Common::Vec4f*>(buffer + bytes_used);
        for (u32 i = 0; i < proctex.color_diff_table.size(); i++) {
            new_data[i] = proctex.color_diff_table[i].ToVector() / 255.0f;
        }
        fs_data.proctex_diff_lut_offset =
            static_cast<int>((offset + bytes_used) / sizeof(Common::Vec4f));
        fs_data_dirty = true;
        bytes_used += proctex.color_diff_table.size() * sizeof(Common::Vec4f);
    }

    pica.proctex.table_dirty = 0;

    texture_buffer.Commit(static_cast<u32>(bytes_used));
}

void RasterizerVulkan::UploadUniforms(bool accelerate_draw) {
    const bool sync_vs_pica = accelerate_draw && pica.vs_setup.uniforms_dirty;
    if (!sync_vs_pica && !vs_data_dirty && !fs_data_dirty) {
        return;
    }

    const u32 uniform_size =
        uniform_size_aligned_vs_pica + uniform_size_aligned_vs + uniform_size_aligned_fs;
    auto [uniforms, offset, invalidate] =
        uniform_buffer.Map(uniform_size, uniform_buffer_alignment);

    u32 used_bytes = 0;

    if (vs_data_dirty || invalidate) {
        std::memcpy(uniforms + used_bytes, &vs_data, sizeof(vs_data));
        pipeline_cache.UpdateRange(1, offset + used_bytes);
        vs_data_dirty = false;
        used_bytes += uniform_size_aligned_vs;
    }

    if (fs_data_dirty || invalidate) {
        std::memcpy(uniforms + used_bytes, &fs_data, sizeof(fs_data));
        pipeline_cache.UpdateRange(2, offset + used_bytes);
        fs_data_dirty = false;
        used_bytes += uniform_size_aligned_fs;
    }

    if (sync_vs_pica || invalidate) {
        VSPicaUniformData vs_uniforms;
        vs_uniforms.SetFromRegs(pica.vs_setup);
        std::memcpy(uniforms + used_bytes, &vs_uniforms, sizeof(vs_uniforms));
        pipeline_cache.UpdateRange(0, offset + used_bytes);
        pica.vs_setup.uniforms_dirty = false;
        used_bytes += uniform_size_aligned_vs_pica;
    }

    uniform_buffer.Commit(used_bytes);
}

void RasterizerVulkan::SwitchDiskResources(u64 title_id) {
    // CodexAstraLocal: A rights switch closes stale title identity before
    // another draw, while the initializer refuses to reread or rearm.
    InitializeVertexCapture(title_id);
    std::atomic_bool stop_loading = false;

    if (switch_disk_resources_callback) {
        switch_disk_resources_callback(VideoCore::LoadCallbackStage::Prepare, 0, 0, "");
    }

    pipeline_cache.SetAccurateMul(accurate_mul);
    pipeline_cache.SwitchCache(title_id, stop_loading, switch_disk_resources_callback);

    if (switch_disk_resources_callback) {
        switch_disk_resources_callback(VideoCore::LoadCallbackStage::Complete, 0, 0, "");
    }
}

void RasterizerVulkan::InitializeVertexCapture(u64 title) noexcept {
    // CodexAstraLocal: Disabled modes do no diagnostic filesystem work. Cold
    // load and later GSP switches cannot reread/rearm this renderer's request.
    // CodexAstraLocal: This immutable owner also guarantees the existing
    // destructor drain runs before capture publication; capture adds no wait.
    if (vertex_capture) {
        const auto activity = Common::UberharActivity::Capture();
        vertex_capture->ObserveIdentity(title, activity.run);
    }
    if (vertex_capture_initialized || !title || !compute_rect ||
        !Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue()))
        return;
    vertex_capture_initialized = true;
    const auto activity = Common::UberharActivity::Capture();
    vertex_capture = VertexCapture::Session::Load(title, activity.run, activity.token & 3U);
}

} // namespace Vulkan
