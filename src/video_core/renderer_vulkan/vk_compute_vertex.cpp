// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include "common/alignment.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/vertex_loader.h"
#include "video_core/renderer_vulkan/vk_compute_vertex.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_compute_vertex_shader_gen.h"
#include "video_core/shader_recovery_error.h"
#include <vk_mem_alloc.h>

namespace Vulkan {
namespace {
[[noreturn]] void Refuse(const char* reason) {
    throw VideoCore::ShaderRecoveryError(reason);
}
constexpr u32 StatusBytes = ComputeVertexInput::HeaderWords * sizeof(u32);
}

ComputeVertexInput CaptureComputeVertexInput(
    Memory::MemorySystem& memory, const Pica::PicaCore& pica, bool indexed,
    bool sanitize_mul, const Pica::ComputeAssemblyState& assembly,
    const std::function<void(PAddr, u32)>& flush, const Pica::AttributeBuffer* immediate) {
    const auto& regs = pica.regs.internal;
    const auto& pipeline = regs.pipeline;
    const u32 n = immediate ? 1 : pipeline.num_vertices;
    if (!n || n > ComputeVertexInput::MaximumInputs || !assembly.Valid() ||
        assembly.topology != static_cast<u32>(pipeline.triangle_topology.Value()) ||
        (immediate && indexed))
        Refuse("Calculated original input count or persistent topology is invalid");
    ComputeVertexInput input{};
    auto& m = input.metadata;
    m[0] = 0x43565231; m[1] = n; m[5] = indexed;
    m[14] = static_cast<u32>(pipeline.triangle_topology.Value());
    m[15] = regs.vs.main_offset;
    input.program = pica.vs_setup.GetProgramCode();
    input.swizzles = pica.vs_setup.GetSwizzleData();
    input.sanitize_mul = sanitize_mul;
    const auto append = [&](const void* bytes, u64 count) -> u32 {
        if (count > ComputeVertexInput::MaximumRawBytes - input.raw.size())
            Refuse("Calculated original input upload exceeds 16 MiB");
        const auto at = static_cast<u32>(input.raw.size());
        input.raw.resize(at + count);
        if (count) std::memcpy(input.raw.data() + at, bytes, count);
        return at;
    };
    // CodexAstraLocal: Flush the actual cache owner before copying a guest span.
    // Pinning alone does not expose GPU-written RAM; the PICA caller separately
    // excludes concurrent DSP/RPC/async writers throughout this synchronous draw.
    const auto capture = [&](u64 address, u64 bytes) -> u32 {
        if (address > std::numeric_limits<u32>::max() ||
            bytes > (u64{1} << 32) - address ||
            bytes > ComputeVertexInput::MaximumRawBytes - input.raw.size())
            Refuse("Calculated original input address/range exceeds capture bounds");
        flush(static_cast<PAddr>(address), static_cast<u32>(bytes));
        const auto ref = memory.GetPhysicalRef(static_cast<PAddr>(address));
        if (!ref.GetPtr() || ref.GetSize() < bytes)
            Refuse("Calculated original input is not completely mapped");
        return append(ref.GetPtr(), bytes);
    };
    const u64 base = pipeline.vertex_attributes.GetPhysicalBaseAddress();
    if (immediate) {
        // CodexAstraLocal: Immediate command attributes are already original
        // f24-backed input words. They do not reference a vertex loader or RAM.
        m[2] = m[3] = 0;
    } else if (indexed) {
        m[8] = pipeline.index_array.format ? 2 : 1;
        m[7] = capture(base + pipeline.index_array.offset, u64{n} * m[8]);
        m[2] = 65535; m[3] = 0;
        for (u32 i = 0; i < n; ++i) {
            const u32 at = m[7] + i * m[8];
            const u32 v = input.raw[at] | (m[8] == 2 ? u32{input.raw[at + 1]} << 8 : 0);
            m[2] = std::min(m[2], v); m[3] = std::max(m[3], v);
        }
    } else {
        const u64 last = u64{pipeline.vertex_offset} + n - 1;
        if (last > 65535) Refuse("Calculated first input domain requires 16-bit vertex indices");
        m[2] = pipeline.vertex_offset; m[3] = static_cast<u32>(last);
    }
    // CodexAstraLocal: Representation copies preserve the original float32-backed
    // f24 words. Neither conversion to HardwareVertex nor a guest CPU shader runs.
    static_assert(sizeof(Pica::f24) == sizeof(float));
    m[9] = append(immediate ? immediate->data() : pica.input_default_attributes.data(), 256);
    m[10] = append(pica.vs_setup.uniforms.f.data(), 1536);
    std::array<u8, 16> bools;
    for (u32 i = 0; i < bools.size(); ++i) bools[i] = pica.vs_setup.uniforms.b[i] ? 1 : 0;
    m[11] = append(bools.data(), bools.size());
    m[12] = append(pica.vs_setup.uniforms.i.data(), 16);
    m[4] = regs.vs.max_input_attribute_index + 1;
    if (m[4] > 16)
        Refuse("Calculated guest shader requests unavailable attributes");
    if (immediate) {
        for (u32 a = 0; a < m[4]; ++a) {
            m[16 + a * 6] = regs.vs.GetRegisterForAttribute(a);
            m[21 + a * 6] = 1;
        }
    } else {
        Pica::VertexLoader loader{memory, pipeline};
        if (m[4] > static_cast<u32>(loader.GetNumTotalAttributes()))
            Refuse("Calculated guest shader requests unavailable attributes");
        for (u32 a = 0; a < m[4]; ++a) {
            const auto desc = loader.DescribeNativeInput(a);
            const u32 at = 16 + a * 6;
            m[at] = regs.vs.GetRegisterForAttribute(a);
            m[at + 1] = static_cast<u32>(desc.format);
            m[at + 2] = desc.elements; m[at + 3] = desc.stride;
            m[at + 5] = desc.is_default;
            if (desc.is_default) continue;
            if (!desc.elements || desc.elements > 4 || m[at + 1] > 3 || desc.stride > 255)
                Refuse("Calculated original attribute is unconfigured or unsupported");
            const u32 width = desc.elements * (m[at + 1] == 3 ? 4 : m[at + 1] == 2 ? 2 : 1);
            const u64 bytes = u64{desc.stride} * (m[3] - m[2]) + width;
            m[at + 4] = capture(base + desc.offset + u64{desc.stride} * m[2], bytes);
        }
    }
    m[13] = regs.rasterizer.vs_output_total;
    if (m[13] > 7) Refuse("Calculated output mapping count exceeds semantic registers");
    std::fill(m.begin() + 112, m.begin() + 136, 64);
    u32 mask = regs.vs.output_mask;
    for (u32 a = 0; a < m[13]; ++a) {
        if (!mask) Refuse("Calculated output mapping requires an unavailable shader register");
        const u32 reg = std::countr_zero(mask); mask &= mask - 1;
        if (reg >= 16) Refuse("Calculated output register exceeds the guest bank");
        const auto mapping = regs.rasterizer.vs_output_attributes[a];
        const std::array<u32, 4> slots{mapping.map_x, mapping.map_y, mapping.map_z, mapping.map_w};
        for (u32 c = 0; c < 4; ++c) {
            if (slots[c] < 24) m[112 + slots[c]] = reg * 4 + c;
            else if (slots[c] != 31) Refuse("Calculated output contains an invalid semantic");
        }
    }
    // CodexAstraLocal: Seed only live original semantic slots. CPU transport
    // carries no converted/clipped vertices and never reruns a retained tail.
    m[136] = assembly.index; m[137] = assembly.ready; m[138] = assembly.winding;
    m[139] = assembly.LiveMask();
    for (u32 slot = 0; slot < 2; ++slot)
        if (m[139] & (1U << slot))
            std::copy_n(assembly.words.begin() + slot * 24, 24, m.begin() + 140 + slot * 24);
    // CodexAstraLocal: Capture the completing draw's actual custom plane before
    // SyncDrawState. Native evaluates it after viewport Y flip but before Z
    // inversion, so pre-flip compute coordinates negate the Y coefficient.
    m[188] = regs.rasterizer.clip_enable != 0;
    if (m[188]) {
        const auto coefficients = regs.rasterizer.GetClipCoef();
        for (u32 c = 0; c < 4; ++c) {
            float value = coefficients[c].ToFloat32();
            if (c == 1 && regs.framebuffer.framebuffer.IsFlipped()) value = -value;
            if (!std::isfinite(value)) Refuse("Calculated custom clip coefficient is nonfinite");
            m[189 + c] = std::bit_cast<u32>(value);
        }
    }
    m[6] = static_cast<u32>(input.raw.size());
    input.raw.resize(Common::AlignUp(input.raw.size(), std::size_t{4}), 0);
    return input;
}

struct ComputeVertexProducer::Impl {
    // CodexAstraLocal: Reuse three bounded allocations only after their actual
    // raster consumer has retired. Mapping a status buffer never maps vertices.
    struct Buffer {
        explicit Buffer(VmaAllocator allocator_) : allocator{allocator_} {}
        ~Buffer() { if (buffer) vmaDestroyBuffer(allocator, buffer, allocation); }
        VmaAllocator allocator;
        VkBuffer buffer{};
        VmaAllocation allocation{};
        vk::DeviceSize size{};
        void* mapped{};
    };
    struct Program {
        Pica::ProgramCode words;
        Pica::SwizzleData swizzles;
        u32 entry;
        bool sanitize, disable_optimizer;
        vk::UniqueShaderModule module;
        vk::UniquePipeline pipeline;
    };
    Impl(const Instance& instance_, Scheduler& scheduler_, DescriptorUpdateQueue& updates_)
        : instance{instance_}, scheduler{scheduler_}, updates{updates_} {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute}};
        descriptors = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(), bindings, 4);
        const auto set_layout = descriptors->Layout();
        layout = instance.GetDevice().createPipelineLayoutUnique(
            {.setLayoutCount = 1, .pSetLayouts = &set_layout});
    }
    void Ensure(std::unique_ptr<Buffer>& slot, vk::DeviceSize bytes,
                vk::BufferUsageFlags usage, VmaAllocationCreateFlags flags) {
        if (slot && slot->size >= bytes) return;
        auto next = std::make_unique<Buffer>(instance.GetAllocator());
        const vk::BufferCreateInfo create{.size = bytes, .usage = usage};
        const auto raw = static_cast<VkBufferCreateInfo>(create);
        const VmaAllocationCreateInfo allocation{.flags = flags | VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
                                                .usage = VMA_MEMORY_USAGE_AUTO};
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(next->allocator, &raw, &allocation, &next->buffer,
                            &next->allocation, &info) != VK_SUCCESS)
            Refuse("Calculated original vertex allocation failed");
        next->size = bytes; next->mapped = info.pMappedData;
        slot = std::move(next);
    }
    Program& Pipeline(const ComputeVertexInput& input, bool disable_optimizer) {
        const auto found = std::find_if(programs.begin(), programs.end(), [&](const Program& p) {
            return p.entry == input.metadata[15] && p.sanitize == input.sanitize_mul &&
                p.disable_optimizer == disable_optimizer && p.words == input.program &&
                p.swizzles == input.swizzles;
        });
        if (found != programs.end()) return *found;
        const auto source = Pica::Shader::Generator::GLSL::GenerateComputeGuestVertex(
            input.program, input.swizzles, input.metadata[15], input.sanitize_mul);
        if (source.empty()) Refuse("Calculated guest program cannot be translated to compute");
        const auto code = CompileGLSL(source, vk::ShaderStageFlagBits::eCompute, "", disable_optimizer);
        if (code.empty()) Refuse("Calculated guest compute program compilation failed");
        // CodexAstraLocal: Produce has already retired the previous use. Bound
        // shader storage without optional readiness, dropped draws or native retry.
        if (programs.size() == 32) programs.erase(programs.begin());
        Program p{input.program, input.swizzles, input.metadata[15], input.sanitize_mul,
                  disable_optimizer, {}, {}};
        const auto device = instance.GetDevice();
        p.module = device.createShaderModuleUnique({.codeSize = code.size() * 4, .pCode = code.data()});
        p.pipeline = device.createComputePipelineUnique({},
            {.stage{.stage = vk::ShaderStageFlagBits::eCompute, .module = *p.module, .pName = "main"},
             .layout = *layout}).value;
        programs.push_back(std::move(p));
        return programs.back();
    }
    Batch Produce(const ComputeVertexInput& input, bool disable_optimizer) {
        // CodexAstraLocal: This explicit first-version wait protects reuse and
        // yields a whole-batch admission result before any framebuffer mutation.
        Pica::ComputeAssemblyState before{};
        before.topology = input.metadata[14]; before.index = input.metadata[136];
        before.ready = input.metadata[137] != 0; before.winding = input.metadata[138] != 0;
        const u32 count = input.metadata[1];
        if (!count || count > ComputeVertexInput::MaximumInputs || !before.Valid() ||
            input.metadata[137] > 1 || input.metadata[138] > 1 ||
            input.metadata[139] != before.LiveMask())
            Refuse("Calculated assembly input metadata is inconsistent");
        const auto expected = Pica::ExpectedComputeAssembly(before, count);
        const u32 vertex_capacity = before.Triangles(count) * 24;
        const u32 first_word = ComputeVertexInput::HeaderWords + (count + 2) * 24;
        scheduler.Wait(last_use);
        auto& program = Pipeline(input, disable_optimizer);
        const auto limits = instance.GetPhysicalDevice().getProperties().limits;
        const auto meta_offset = Common::AlignUp<vk::DeviceSize>(input.raw.size(),
            std::max<vk::DeviceSize>(4, limits.minStorageBufferOffsetAlignment));
        const vk::DeviceSize result_bytes = 4ULL * (first_word + u64{vertex_capacity} * 22);
        if (result_bytes > limits.maxStorageBufferRange || input.raw.size() > limits.maxStorageBufferRange)
            Refuse("Calculated compute input/output exceeds storage-buffer limits");
        Ensure(upload, meta_offset + sizeof(input.metadata), vk::BufferUsageFlagBits::eStorageBuffer,
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        Ensure(vertices, result_bytes, vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, 0);
        Ensure(readback, StatusBytes, vk::BufferUsageFlagBits::eTransferDst,
               VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        if (!upload->mapped || !readback->mapped) Refuse("Calculated host staging mapping unavailable");
        std::memcpy(upload->mapped, input.raw.data(), input.raw.size());
        std::memcpy(static_cast<u8*>(upload->mapped) + meta_offset, input.metadata.data(), sizeof(input.metadata));
        if (vmaFlushAllocation(upload->allocator, upload->allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS)
            Refuse("Calculated original input flush failed");
        const auto set = descriptors->Commit();
        updates.AddBuffer(set, 0, vk::Buffer{upload->buffer}, 0, input.raw.size(), vk::DescriptorType::eStorageBuffer);
        updates.AddBuffer(set, 1, vk::Buffer{upload->buffer}, meta_offset, sizeof(input.metadata), vk::DescriptorType::eStorageBuffer);
        updates.AddBuffer(set, 2, vk::Buffer{vertices->buffer}, 0, result_bytes, vk::DescriptorType::eStorageBuffer);
        scheduler.Record([set, pipeline = *program.pipeline, pipeline_layout = *layout,
                          output = vk::Buffer{vertices->buffer}, status = vk::Buffer{readback->buffer}]
                          (vk::CommandBuffer cmd) {
            // CodexAstraLocal: Explicitly unpublish stale success before dispatch;
            // the only host copy is the small final completion/tail header.
            cmd.fillBuffer(output, 0, StatusBytes, 0);
            const vk::MemoryBarrier input_ready{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost | vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader, {}, input_ready, {}, {});
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, set, {});
            cmd.dispatch(1, 1, 1);
            const vk::MemoryBarrier complete{.srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader,
                {}, complete, {}, {});
            cmd.copyBuffer(output, status, vk::BufferCopy{0, 0, StatusBytes});
            const vk::MemoryBarrier host{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                        .dstAccessMask = vk::AccessFlagBits::eHostRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                                {}, host, {}, {});
        });
        last_use = scheduler.CurrentTick();
        scheduler.Wait(last_use);
        if (vmaInvalidateAllocation(readback->allocator, readback->allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS)
            Refuse("Calculated completion invalidation failed");
        std::array<u32, ComputeVertexInput::HeaderWords> status;
        std::memcpy(status.data(), readback->mapped, StatusBytes);
        if (status[0] != 1 || status[1] != 0) {
            if (status[1] == 2) Refuse("Calculated guest shader produced nonfinite vertex fields");
            if (status[1] == 3) Refuse("Calculated homogeneous clipping exceeded the finite domain");
            Refuse("Calculated guest input or compute completion was rejected");
        }
        if (status[2] > vertex_capacity || status[2] % 3 ||
            status[3] != input.metadata[1] || status[4] + status[5] != input.metadata[1] ||
            status[6] != first_word || status[56] != before.topology ||
            status[57] != expected.state.index ||
            status[58] != (u32{expected.state.ready} | (u32{expected.state.winding} << 1)) ||
            status[59] != expected.written_mask)
            Refuse("Calculated completed guest stream metadata is inconsistent");
        Batch batch{{vk::Buffer{vertices->buffer}, result_bytes, status[6]}, status[2], {}};
        batch.assembly = expected;
        for (u32 slot = 0; slot < 2; ++slot)
            if (expected.written_mask & (1U << slot))
                std::copy_n(status.begin() + 8 + slot * 24, 24,
                            batch.assembly.state.words.begin() + slot * 24);
        return batch;
    }
    const Instance& instance;
    Scheduler& scheduler;
    DescriptorUpdateQueue& updates;
    std::unique_ptr<DescriptorHeap> descriptors;
    vk::UniquePipelineLayout layout;
    std::vector<Program> programs;
    std::unique_ptr<Buffer> upload, vertices, readback;
    u64 last_use{};
};

ComputeVertexProducer::ComputeVertexProducer(const Instance& instance, Scheduler& scheduler,
                                           DescriptorUpdateQueue& updates)
    : impl{std::make_unique<Impl>(instance, scheduler, updates)} {}
ComputeVertexProducer::~ComputeVertexProducer() = default;
ComputeVertexProducer::Batch ComputeVertexProducer::Produce(const ComputeVertexInput& input,
                                                           bool disable_optimizer) {
    return impl->Produce(input, disable_optimizer);
}
void ComputeVertexProducer::MarkRasterUse() { impl->last_use = impl->scheduler.CurrentTick(); }
} // namespace Vulkan
