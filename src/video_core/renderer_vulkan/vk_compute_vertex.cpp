// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <fmt/format.h>
#include "common/alignment.h"
#include "common/logging/log.h"
#include "core/memory.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/vertex_loader.h"
#include "video_core/renderer_vulkan/uberhar_compute_pipeline.h"
#include "video_core/renderer_vulkan/uberhar_compute_lowering.h" // CodexAstraLocal: Structural retry only.
#include "video_core/renderer_vulkan/vk_compute_vertex.h"
// CodexAstraLocal: Independent feature policy and sampled real-path attribution.
#include "video_core/renderer_vulkan/compute_feature_profile.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_compute_vertex_shader_gen.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader_recovery_error.h"
#include <vk_mem_alloc.h>

namespace Vulkan {
namespace {
[[noreturn]] void Refuse(const char* reason) {
    throw VideoCore::ShaderRecoveryError(reason);
}
constexpr u32 StatusBytes = ComputeVertexInput::HeaderWords * sizeof(u32);

// CodexAstraLocal: Report the actual rejected condition instead of calling every
// clipping failure a capacity overflow. These are permanent terminal errors,
// not optional timing/diagnostic instrumentation or permission to retry a draw.
std::string_view ClipFailureName(u32 code) {
    using Failure = Pica::Shader::Generator::GLSL::ComputeClipFailure;
    switch (static_cast<Failure>(code)) {
    case Failure::PlaneDistance: return "nonfinite_plane_distance";
    case Failure::PolygonCapacity: return "polygon_capacity";
    case Failure::IntersectionParameter: return "intersection_parameter";
    case Failure::InterpolatedField: return "nonfinite_interpolated_field";
    case Failure::CustomDistance: return "nonfinite_custom_distance";
    case Failure::NonpositiveW: return "nonpositive_w";
    case Failure::Reciprocal: return "nonfinite_reciprocal_w";
    case Failure::OutsidePlane: return "outside_processed_plane";
    case Failure::OutputCapacity: return "output_capacity";
    case Failure::LiveSemantic: return "nonfinite_live_semantic";
    default: return "invalid_failure_record";
    }
}
}

std::shared_ptr<ComputeFailurePacket> BeginComputeFailurePacket(
    std::shared_ptr<const ComputeDeviceEvidence> device, const Pica::PicaCore& pica, bool indexed,
    const Pica::ComputeAssemblyState& assembly, const Pica::AttributeBuffer* immediate) {
    auto packet = MakeComputeFailurePacket(std::move(device));
    if (!packet) return {};
    // CodexAstraLocal TEST-ONLY LOG: Preserve only live semantic slots and exact
    // original register/default/uniform bytes before any capture can refuse.
    // Remove after actual producer failures have complete corrected regressions.
    packet->Value("pica.registers.u32", pica.regs.internal.reg_array);
    packet->Value("input.defaults.f32bits", pica.input_default_attributes);
    packet->Value("input.float_uniforms.f32bits", pica.vs_setup.uniforms.f);
    packet->Value("input.integer_uniforms.u8", pica.vs_setup.uniforms.i);
    std::array<u8,16> booleans{};
    for (u32 i=0;i<booleans.size();++i) booleans[i]=pica.vs_setup.uniforms.b[i] ? 1 : 0;
    packet->Value("input.boolean_uniforms.u8", booleans);
    std::array<u32,53> entry{};
    entry[0]=assembly.topology;entry[1]=assembly.index;
    entry[2]=assembly.ready;entry[3]=assembly.winding;
    entry[4]=assembly.Valid() ? assembly.LiveMask() : 0;
    for(u32 slot=0;slot<2;++slot) if(entry[4]&(1U<<slot))
        std::copy_n(assembly.words.begin()+slot*24,24,entry.begin()+5+slot*24);
    packet->Value("assembly.entry.u32",entry);
    if(immediate) packet->Value("input.immediate.f32bits",*immediate);
    packet->Label("input_route",immediate ? "immediate" : indexed ? "indexed_array" : "array");
    packet->Number("mode",2);
    try {
        auto& system=Core::System::GetInstance();
        u64 title{};
        if(system.IsPoweredOn()) (void)system.GetAppLoader().ReadProgramId(title);
        packet->Number("title",title);
    } catch (...) { packet->Unavailable("title","title_loader_unavailable"); }
    return packet;
}

ComputeVertexInput CaptureComputeVertexInput(
    Memory::MemorySystem& memory, const Pica::PicaCore& pica, bool indexed,
    bool sanitize_mul, const Pica::ComputeAssemblyState& assembly,
    const std::function<void(PAddr, u32)>& flush, const Pica::AttributeBuffer* immediate,
    const std::shared_ptr<ComputeFailurePacket>& failure) {
    const auto& regs = pica.regs.internal;
    const auto& pipeline = regs.pipeline;
    const u32 n = immediate ? 1 : pipeline.num_vertices;
    ComputeVertexInput input{};
    input.program = pica.vs_setup.GetProgramCode();
    input.swizzles = pica.vs_setup.GetSwizzleData();
    input.sanitize_mul = sanitize_mul;
    try {
    if (!n || n > ComputeVertexInput::MaximumInputs || !assembly.Valid() ||
        assembly.topology != static_cast<u32>(pipeline.triangle_topology.Value()) ||
        (immediate && indexed))
        Refuse("Calculated original input count or persistent topology is invalid");
    auto& m = input.metadata;
    m[0] = 0x43565231; m[1] = n; m[5] = indexed;
    m[14] = static_cast<u32>(pipeline.triangle_topology.Value());
    m[15] = regs.vs.main_offset;
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
        if(failure) {
            failure->Number("last_capture_address",address);
            failure->Number("last_capture_bytes",bytes);
            failure->Number("last_capture_raw_offset",input.raw.size());
        }
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
    m[193] = Pica::Shader::Generator::GLSL::ComputeLiveSemanticMask(Pica::Shader::FSConfig{regs});
    input.raw.resize(Common::AlignUp(input.raw.size(), std::size_t{4}), 0);
    return input;
    } catch (...) {
        // CodexAstraLocal: Failed capture retains exactly the copied prefix;
        // never reread an unmapped/concurrently changing span during unwinding.
        if(failure) {
            try { failure->Input(std::make_shared<ComputeVertexInput>(std::move(input)),false); }
            catch (...) { failure->Unavailable("input","partial_capture_retention_failed"); }
        }
        throw;
    }
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
    using Cost=ComputeFeatureProfile::Cost;
    struct Program {
        Pica::ProgramCode words;
        Pica::SwizzleData swizzles;
        u32 entry;
        bool sanitize, disable_optimizer;
        vk::UniqueShaderModule module;
        vk::UniquePipeline pipeline;
        std::shared_ptr<const ComputeModuleEvidence> evidence;
        // CodexAstraLocal: A successful structural retry retains its rejected
        // original too; a later cached draw may be the first terminal incident.
        std::shared_ptr<const ComputeModuleEvidence> rejected_evidence;
        int rejected_result{};
        // CodexAstraLocal: No disk cache is used by this owner; in-memory reuse
        // still keys every source/diagnostic revision and effective policy.
        u64 feature_key{};
    };
    Impl(const Instance& instance_, Scheduler& scheduler_, DescriptorUpdateQueue& updates_)
        : instance{instance_}, scheduler{scheduler_}, updates{updates_},
          device_evidence{CaptureComputeDeviceEvidence(instance_)} {
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
                p.swizzles == input.swizzles && p.feature_key==costs.config.Key();
        });
        if (found != programs.end()) {
            ++costs.guest_hits;
            if(found->rejected_evidence)++costs.retry_cache_hits;
            if(failure) {
                failure->Module(found->evidence);
                // CodexAstraLocal: Republish both immutable attempts and their
                // exact policy/result when this program is reused by a new draw.
                if (found->rejected_evidence) {
                    failure->Module(found->rejected_evidence);
                    failure->Number("guest_initial_result_signed_bits", static_cast<u32>(found->rejected_result));
                    failure->Label("guest_structural_retry", "created_cached");
                    failure->Label("guest_lowering_policy", "structural_v2_local_only");
                    failure->Number("guest_requested_optimizer_disabled", disable_optimizer);
                    failure->Number("guest_effective_optimizer_disabled", found->evidence->optimizer_disabled);
                    failure->Number("pipeline_result_signed_bits", 0);
                }
            }
            return *found;
        }
        ++costs.guest_misses;
        auto compile_timer=costs.Measure(Cost::GuestCompile,true);
        if(failure) failure->Stage(ComputeFailureStage::GuestTranslate);
        const auto source = Pica::Shader::Generator::GLSL::GenerateComputeGuestVertex(
            input.program, input.swizzles, input.metadata[15], input.sanitize_mul);
        auto evidence=std::make_shared<ComputeModuleEvidence>();
        evidence->role="guest_vertex";evidence->source=source;
        evidence->optimizer_disabled=disable_optimizer;
        evidence->calculated_feature_key=costs.config.Key();
        if(failure) failure->Module(evidence);
        if (source.empty()) Refuse("Calculated guest program cannot be translated to compute");
        if(failure) failure->Stage(ComputeFailureStage::GuestCompile);
        const auto code = CompileGLSL(source, vk::ShaderStageFlagBits::eCompute, "", disable_optimizer);
        // CodexAstraLocal: A queued failure may already hold the source-only
        // snapshot. Replace it atomically; never mutate published module vectors.
        evidence=std::make_shared<ComputeModuleEvidence>(*evidence);
        evidence->spirv=code;
        if(failure)failure->Module(evidence);
        compile_timer.End();
        if (code.empty()) Refuse("Calculated guest compute program compilation failed");
        // CodexAstraLocal: Produce has already retired the previous use. Bound
        // shader storage without optional readiness, dropped draws or native retry.
        if (programs.size() == 32) programs.erase(programs.begin());
        // CodexAstraLocal: Preserve the exact module on rejection and contain
        // its Vulkan error before returning through guest-generated code.
        if(failure) failure->Stage(ComputeFailureStage::GuestPipeline);
        ComputePipelineObjects objects;
        ComputePipelineFailureInfo outcome;
        std::shared_ptr<const ComputeModuleEvidence> rejected_evidence;
        try {
            objects = costs.Time(Cost::GuestLink,[&] { return CreateComputePipeline(
                instance,*layout,source,code,disable_optimizer,ComputePipelineRole::GuestVertex,
                reported_creation_failure,failure.get(),&outcome); },true);
        } catch (const VideoCore::ShaderRecoveryError&) {
            // CodexAstraLocal: The captured Dark Moon module validates but the
            // driver cannot link it. Reuse pinned structural SPIR-V passes
            // once, only for that rejection class; never the size optimizer.
            // No guest operation, precise qualifier or NaN guard is changed.
            if (!costs.config.Has(CalculatedFeatures::StructuralRetry) ||
                !outcome.AllowsStructuralRetry(disable_optimizer)) throw;
            ++costs.retry_attempts;
            ComputeLoweringReport lowering;
            const auto alternate = LowerComputePrivateState(code, &lowering);
            // CodexAstraLocal TEST-ONLY LOG: Failure-path transform accounting
            // distinguishes refusal from success; remove with compatibility qualification.
            if (failure) {
                failure->Label("guest_lowering_status", ComputeLoweringStatusName(lowering.status));
                failure->Number("guest_lowering_input_bytes", lowering.input_bytes);
                failure->Number("guest_lowering_output_bytes", lowering.output_bytes);
                failure->Number("guest_lowering_host_ns", lowering.host_ns);
            }
            costs.transform_ns+=lowering.host_ns;
            costs.transform_input_bytes+=lowering.input_bytes;
            costs.transform_output_bytes+=lowering.output_bytes;
            if (alternate.empty()) {++costs.retry_no_alternate;throw;}
            // CodexAstraLocal: Same-role replacement would discard the rejected
            // module. Keep both exact attempts in the original failure packet.
            auto rejected = std::make_shared<ComputeModuleEvidence>(*evidence);
            rejected->role = "guest_vertex_initial";
            rejected_evidence = rejected;
            if (failure) failure->Module(rejected);
            evidence = std::make_shared<ComputeModuleEvidence>(*evidence);
            evidence->spirv = alternate;
            evidence->structural_lowering_revision = 2;
            if (failure) {
                failure->Number("guest_initial_result_signed_bits", static_cast<u32>(outcome.result));
                failure->Label("guest_structural_retry", "attempted_once");
                failure->Number("guest_requested_optimizer_disabled", disable_optimizer);
                failure->Number("guest_effective_optimizer_disabled", disable_optimizer);
                failure->Label("guest_lowering_policy", "structural_v2_local_only");
                failure->Module(evidence);
            }
            try {
                objects = costs.Time(Cost::GuestLink,[&] { return CreateComputePipeline(
                    instance,*layout,source,alternate,disable_optimizer,ComputePipelineRole::GuestVertex,
                    reported_creation_failure,failure.get()); },true);
                ++costs.retry_created;
            } catch(...) { ++costs.retry_failed;throw; }
            if (failure) {
                failure->Label("guest_structural_retry", "created");
                failure->Number("pipeline_result_signed_bits", 0);
            }
        }
        // CodexAstraLocal: Lookup remains keyed by the requested immutable
        // policy; the cached evidence records the actual accepted module policy.
        Program p{input.program, input.swizzles, input.metadata[15], input.sanitize_mul,
                  disable_optimizer, {}, {}, evidence, rejected_evidence, outcome.result,costs.config.Key()};
        p.module = std::move(objects.module);
        p.pipeline = std::move(objects.pipeline);
        programs.push_back(std::move(p));
        return programs.back();
    }
    // CodexAstraLocal: The shared ordered assembly/clip kernel is independent of
    // guest code. Produce has retired last_use before replacing either handle.
    // Resolve both stages before dispatch, so a second-stage creation failure
    // cannot leave a half-executed draw or publish a semantic tail.
    void PrepareAssembly(bool disable_optimizer) {
        if (assembly_pipeline && assembly_disable_optimizer == disable_optimizer &&
            assembly_feature_key==costs.config.Key()) {
            ++costs.assembly_hits;
            if(failure) failure->Module(assembly_evidence);
            return;
        }
        ++costs.assembly_misses;
        auto compile_timer=costs.Measure(Cost::AssemblyCompile,true);
        if(failure) failure->Stage(ComputeFailureStage::AssemblyCompile);
        const auto source = Pica::Shader::Generator::GLSL::GenerateComputeAssembly(
            costs.config.effective,costs.config.diagnostics);
        auto evidence=std::make_shared<ComputeModuleEvidence>();
        evidence->role="assembly_clip";evidence->source=source;
        evidence->optimizer_disabled=disable_optimizer;
        evidence->calculated_feature_key=costs.config.Key();
        if(failure) failure->Module(evidence);
        const auto code = CompileGLSL(source, vk::ShaderStageFlagBits::eCompute, "", disable_optimizer);
        // CodexAstraLocal: A queued failure may already hold the source-only
        // snapshot. Replace it atomically; never mutate published module vectors.
        evidence=std::make_shared<ComputeModuleEvidence>(*evidence);
        evidence->spirv=code;
        if(failure)failure->Module(evidence);
        compile_timer.End();
        if (code.empty()) Refuse("Calculated assembly compute compilation failed");
        if(failure) failure->Stage(ComputeFailureStage::AssemblyPipeline);
        auto objects = costs.Time(Cost::AssemblyLink,[&] { return CreateComputePipeline(
            instance,*layout,source,code,disable_optimizer,ComputePipelineRole::AssemblyClip,
            reported_creation_failure,failure.get()); },true);
        assembly_module = std::move(objects.module);
        assembly_pipeline = std::move(objects.pipeline);
        assembly_disable_optimizer = disable_optimizer;
        assembly_feature_key=costs.config.Key();
        assembly_evidence=std::move(evidence);
    }
    void RetirePrevious(const std::shared_ptr<ComputeFailurePacket>& next_failure = {}) {
        // CodexAstraLocal: Retirement belongs to the previous draw. Keep its
        // immutable context until this wait succeeds, including raster failures.
        // CodexAstraLocal TEST-ONLY LOG: Existing owner wait, never GPU execution time.
        // Remove sampled cost with hardware qualification; no extra wait is added.
        try { costs.Time(Cost::Wait,[&]{scheduler.Wait(last_use);}); }
        catch(const std::exception& error) {
            // CodexAstraLocal: B has not executed. Only prior packet A owns
            // this retirement failure; outer B unwind cannot create an incident.
            if(next_failure) next_failure->CancelDependency();
            if(failure) {
                failure->Unavailable("retirement_status","previous_final_use_wait_failed");
                failure->Failure(ComputeFailureStage::PreviousUseWait,error.what());
            }
            throw;
        }
    }
    Batch Produce(const ComputeVertexInput& input, bool disable_optimizer,
                  std::shared_ptr<ComputeFailurePacket> next_failure) {
        RetirePrevious(next_failure);
        failure=std::move(next_failure); completed_readback=false; output_bytes=0;
        retained_failure_output=false;upload_bytes=0;
        costs.Begin(failure);costs.inputs+=input.metadata[1];costs.raw_bytes+=input.raw.size();
        auto admission_timer=costs.Measure(Cost::Prepare);
        const auto recording=scheduler.RecordComputeFailure(failure);
        if(failure) failure->Stage(ComputeFailureStage::Admission);
        try {
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
        if(failure) {
            const std::array<u32,11> contract{1,0,vertex_capacity,count,count,first_word,
                before.topology,expected.state.index,
                u32{expected.state.ready}|(u32{expected.state.winding}<<1),expected.written_mask,
                Pica::Shader::Generator::GLSL::ComputeGuestDiagnosticWords};
            failure->Expected(contract);
            failure->Label("expected_schema","success,reason,max_vertices,input_count,misses_plus_hits,first_word,topology,index,flags,written_mask,diagnostic_words");
            failure->Number("optimizer_disabled",disable_optimizer);
        }
        admission_timer.End();
        auto& program = Pipeline(input, disable_optimizer);
        PrepareAssembly(disable_optimizer);
        auto prepare_timer=costs.Measure(Cost::Prepare);
        const auto limits = instance.GetPhysicalDevice().getProperties().limits;
        const auto meta_offset = Common::AlignUp<vk::DeviceSize>(input.raw.size(),
            std::max<vk::DeviceSize>(4, limits.minStorageBufferOffsetAlignment));
        const vk::DeviceSize result_bytes = 4ULL * (first_word + u64{vertex_capacity} * 22 +
            Pica::Shader::Generator::GLSL::ComputeGuestDiagnosticWords);
        if (result_bytes > limits.maxStorageBufferRange || input.raw.size() > limits.maxStorageBufferRange)
            Refuse("Calculated compute input/output exceeds storage-buffer limits");
        if(failure) failure->Stage(ComputeFailureStage::Allocation);
        Ensure(upload, meta_offset + sizeof(input.metadata), vk::BufferUsageFlagBits::eStorageBuffer |
                   vk::BufferUsageFlagBits::eTransferSrc,
               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        Ensure(vertices, result_bytes, vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst, 0);
        Ensure(readback, StatusBytes, vk::BufferUsageFlagBits::eTransferDst,
               VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        if (!upload->mapped || !readback->mapped) Refuse("Calculated host staging mapping unavailable");
        output_bytes=result_bytes;
        upload_bytes=meta_offset+sizeof(input.metadata);
        if(failure) {
            failure->Stage(ComputeFailureStage::Upload);failure->Number("output_buffer_bytes",result_bytes);
            failure->Number("upload_metadata_offset",meta_offset);
            failure->Number("upload_used_bytes",upload_bytes);
        }
        std::memcpy(upload->mapped, input.raw.data(), input.raw.size());
        std::memcpy(static_cast<u8*>(upload->mapped) + meta_offset, input.metadata.data(), sizeof(input.metadata));
        if (vmaFlushAllocation(upload->allocator, upload->allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS)
            Refuse("Calculated original input flush failed");
        if(failure) failure->Stage(ComputeFailureStage::Descriptors);
        const auto set = descriptors->Commit();
        updates.AddBuffer(set, 0, vk::Buffer{upload->buffer}, 0, input.raw.size(), vk::DescriptorType::eStorageBuffer);
        updates.AddBuffer(set, 1, vk::Buffer{upload->buffer}, meta_offset, sizeof(input.metadata), vk::DescriptorType::eStorageBuffer);
        updates.AddBuffer(set, 2, vk::Buffer{vertices->buffer}, 0, result_bytes, vk::DescriptorType::eStorageBuffer);
        prepare_timer.End();
        auto enqueue_timer=costs.Measure(Cost::Enqueue);
        // CodexAstraLocal TEST-ONLY LOG: Worker-owned timestamp slots bracket the
        // actual dispatches. No new scheduler nodes, submissions, waits or reads.
        // Root's independent profiler policy decides counts/queries; Calculated diagnostics never enables them.
        // Remove with Calculated hardware qualification.
        scheduler.Record([scheduler=&scheduler,
                          input_bytes=input.raw.size(),set, pipeline = *program.pipeline, clip_pipeline = *assembly_pipeline,
                          pipeline_layout = *layout,
                          output = vk::Buffer{vertices->buffer}, status = vk::Buffer{readback->buffer},
                          failure=failure,result_bytes]
                          (vk::CommandBuffer cmd) {
            auto stage=ComputeFailureStage::GuestDispatch;
            try {
            // CodexAstraLocal: Explicitly unpublish stale success before dispatch;
            // the only host copy is the small final completion/tail header.
            cmd.fillBuffer(output, 0, StatusBytes, 0);
            // CodexAstraLocal TEST-ONLY LOG: Clear the fixed sideband so a failed
            // or missing writer cannot appear to be a previous invocation. Remove
            // with nonfinite capture; success header/semantic/88B offsets do not move.
            constexpr u32 diagnostic_bytes=Pica::Shader::Generator::GLSL::ComputeGuestDiagnosticWords*4;
            cmd.fillBuffer(output,result_bytes-diagnostic_bytes,diagnostic_bytes,0);
            const vk::MemoryBarrier input_ready{
                .srcAccessMask = vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost | vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader, {}, input_ready, {}, {});
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, set, {});
            const int guest_sample=scheduler->BeginGpuWork(cmd,GpuWork::CalculatedGuest,input_bytes);
            cmd.dispatch(1, 1, 1);
            scheduler->EndGpuWork(cmd,guest_sample);
            // CodexAstraLocal: Stage one owns FIFO/carry and writes complete
            // semantic scratch plus a transient token. Stage two sees both only
            // after this buffer-wide dependency; it alone publishes final state.
            const vk::MemoryBarrier semantic_ready{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader, {}, semantic_ready, {}, {});
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, clip_pipeline);
            stage=ComputeFailureStage::AssemblyDispatch;
            const int assembly_sample=scheduler->BeginGpuWork(cmd,GpuWork::CalculatedAssembly,result_bytes);
            cmd.dispatch(1, 1, 1);
            scheduler->EndGpuWork(cmd,assembly_sample);
            const vk::MemoryBarrier complete{.srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader,
                {}, complete, {}, {});
            stage=ComputeFailureStage::ReadbackCopy;
            cmd.copyBuffer(output, status, vk::BufferCopy{0, 0, StatusBytes});
            const vk::MemoryBarrier host{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                        .dstAccessMask = vk::AccessFlagBits::eHostRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                                {}, host, {}, {});
            } catch(const vk::SystemError& error) {
                if(failure) { failure->Unavailable("actual.status.u32","recording_failed");failure->Notice(stage,error.what(),error.code().value()); }
                throw VideoCore::ShaderRecoveryError(error.what(),error.code().value());
            } catch(const std::exception& error) {
                if(failure) failure->Notice(stage,error.what());
                throw;
            }
        });
        enqueue_timer.End();
        last_use = scheduler.CurrentTick();
        if(failure) { failure->Stage(ComputeFailureStage::ProducerWait);failure->Number("producer_tick",last_use); }
        costs.Time(Cost::Wait,[&]{scheduler.Wait(last_use);});
        if(failure) failure->Stage(ComputeFailureStage::Invalidate);
        if (vmaInvalidateAllocation(readback->allocator, readback->allocation, 0, VK_WHOLE_SIZE) != VK_SUCCESS)
            Refuse("Calculated completion invalidation failed");
        std::array<u32, ComputeVertexInput::HeaderWords> status;
        std::memcpy(status.data(), readback->mapped, StatusBytes);
        completed_readback=true;
        if(failure) { failure->Status(status);failure->Stage(ComputeFailureStage::Completion); }
        if (status[0] != 1 || status[1] != 0) {
            if (status[1] == 2) Refuse("Calculated guest shader produced nonfinite vertex fields");
            if (status[1] == 3) {
                const auto reason = status[60] == 0x43465031 ? ClipFailureName(status[7]) :
                                                            "invalid_failure_record";
                // CodexAstraLocal TEST-ONLY LOG: Keep one fixed raw operand
                // record for the current hardware first fault. Remove this log
                // and the GPU operand fields once that fault has a retained
                // regression; preserve the permanent reason/location exception
                // below. Success adds no logging/readback; accidental reuse
                // after a caught terminal error cannot flood the log.
                if (!reported_clip_failure) {
                    reported_clip_failure = true;
                    std::string words;
                    words.reserve((ComputeVertexInput::HeaderWords - 7) * 9);
                    for (u32 i = 7; i < ComputeVertexInput::HeaderWords; ++i)
                        fmt::format_to(std::back_inserter(words), "{:08X}{}", status[i],
                                       i + 1 == ComputeVertexInput::HeaderWords ? "" : ",");
                    LOG_ERROR(Render_Vulkan,
                        "Calculated clipping failure: schema=1 reason={} code={} triangle={} "
                        "plane={} polygon_vertices={} output_cursor={} output_capacity={} "
                        "topology={} input_vertices={} entry={} custom_plane={} "
                        "record_scope=header_words_7_to_63 words={}",
                        reason, status[7], status[8], status[9], status[10], status[11], status[12],
                        input.metadata[14], count, input.metadata[15], input.metadata[188], words);
                }
                const auto message = fmt::format(
                    "Calculated clipping refused: {} (triangle {}, plane {})", reason, status[8], status[9]);
                Refuse(message.c_str());
            }
            Refuse("Calculated guest input or compute completion was rejected");
        }
        if(failure) failure->Stage(ComputeFailureStage::AssemblyState);
        // CodexAstraLocal: Preserve the exact original comparisons, and retain
        // which one failed instead of only their shared terminal message.
        const u32 mismatch = (u32{status[2] > vertex_capacity} << 0) |
            (u32{status[2] % 3 != 0} << 1) |
            (u32{status[3] != input.metadata[1]} << 2) |
            (u32{status[4] + status[5] != input.metadata[1]} << 3) |
            (u32{status[6] != first_word} << 4) |
            (u32{status[56] != before.topology} << 5) |
            (u32{status[57] != expected.state.index} << 6) |
            (u32{status[58] != (u32{expected.state.ready} | (u32{expected.state.winding} << 1))} << 7) |
            (u32{status[59] != expected.written_mask} << 8);
        if(failure) failure->Number("completion_mismatch_mask",mismatch);
        if (mismatch)
            Refuse("Calculated completed guest stream metadata is inconsistent");
        // CodexAstraLocal TEST-ONLY LOG: Consume success-only CDS1 words after
        // unchanged mandatory status/tail checks. Unknown counters never repair
        // output; report unavailable. Remove with the paired shader counters.
        costs.outputs+=status[2];costs.output_bytes+=u64{status[2]}*88;
        costs.zero_outputs+=status[2]==0;
        if(costs.config.diagnostics) {
            if(status[60]==0x43445331u) {
                costs.inside+=status[61];costs.polygon+=status[62];costs.intersections+=status[63];
            } else ++costs.counter_unavailable;
        }
        costs.Complete(true);
        Batch batch{{vk::Buffer{vertices->buffer}, result_bytes, status[6],failure}, status[2], {}};
        batch.assembly = expected;
        for (u32 slot = 0; slot < 2; ++slot)
            if (expected.written_mask & (1U << slot))
                std::copy_n(status.begin() + 8 + slot * 24, 24,
                            batch.assembly.state.words.begin() + slot * 24);
        return batch;
        } catch(const vk::SystemError& error) {
            costs.Complete(false);
            if(failure) {
                failure->Unavailable("gpu.output","Vulkan operation failed; no diagnostic GPU work attempted");
                failure->Failure(error.what(),error.code().value());
            }
            throw VideoCore::ShaderRecoveryError(error.what(),error.code().value());
        } catch(const VideoCore::ShaderRecoveryError& error) {
            costs.Complete(false);
            // CodexAstraLocal: Pipeline normalization must not turn -13 into a
            // successful-looking zero in the first terminal capsule header.
            RetainFailedOutput();
            if(failure) failure->Failure(error.what(),error.DriverResult());
            throw;
        } catch(const std::exception& error) {
            costs.Complete(false);
            RetainFailedOutput();
            if(failure) failure->Failure(error.what());
            throw;
        } catch(...) {
            costs.Complete(false);
            if(failure) failure->Failure("unknown exception; original failure preserved");
            throw;
        }
    }
    void RetainFailedOutput() noexcept;
    const Instance& instance;
    std::shared_ptr<const ComputeDeviceEvidence> device_evidence;
    Scheduler& scheduler;
    DescriptorUpdateQueue& updates;
    std::unique_ptr<DescriptorHeap> descriptors;
    vk::UniquePipelineLayout layout;
    vk::UniqueShaderModule assembly_module;
    vk::UniquePipeline assembly_pipeline;
    std::shared_ptr<const ComputeModuleEvidence> assembly_evidence;
    bool assembly_disable_optimizer{};
    // CodexAstraLocal: Per-owner snapshot; cache-hit metadata uses actual policy.
    u64 assembly_feature_key{};
    ComputeFeatureProfile costs{"producer"};
    std::vector<Program> programs;
    std::unique_ptr<Buffer> upload, vertices, readback;
    std::unique_ptr<Buffer> diagnostic_readback;
    u64 last_use{};
    bool reported_clip_failure{};
    bool reported_creation_failure{};
    std::shared_ptr<ComputeFailurePacket> failure;
    vk::DeviceSize output_bytes{},upload_bytes{};
    bool completed_readback{};
    bool retained_failure_output{};
};

void ComputeVertexProducer::Impl::RetainFailedOutput() noexcept {
    if(!failure||retained_failure_output)return;
    retained_failure_output=true;
    if(!completed_readback||!output_bytes) {
        failure->Unavailable("gpu.result.raw.u32","current_draw_status_not_successfully_read_back");
        return;
    }
    try {
        // CodexAstraLocal TEST-ONLY LOG: Only a completed current producer may
        // copy its unpublished scratch/upload for diagnosis. No guest replay or
        // attachment mutation is attempted. Keep staging owned through renderer
        // drain even if this diagnostic wait fails. Remove after exact correction.
        Ensure(diagnostic_readback,output_bytes+upload_bytes,vk::BufferUsageFlagBits::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT);
        if(!diagnostic_readback->mapped) throw std::runtime_error{"diagnostic mapping unavailable"};
        scheduler.Record([source=vk::Buffer{vertices->buffer},raw=vk::Buffer{upload->buffer},
                          target=vk::Buffer{diagnostic_readback->buffer},
                          count=output_bytes,raw_count=upload_bytes](vk::CommandBuffer cmd) {
            const vk::MemoryBarrier ready{.srcAccessMask=vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask=vk::AccessFlagBits::eTransferRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,vk::PipelineStageFlagBits::eTransfer,
                                {},ready,{},{});
            cmd.copyBuffer(source,target,vk::BufferCopy{0,0,count});
            cmd.copyBuffer(raw,target,vk::BufferCopy{0,count,raw_count});
            const vk::MemoryBarrier host{.srcAccessMask=vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask=vk::AccessFlagBits::eHostRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eHost,
                                {},host,{},{});
        });
        const auto tick=scheduler.CurrentTick();
        last_use=tick;
        scheduler.Wait(tick);
        if(vmaInvalidateAllocation(diagnostic_readback->allocator,diagnostic_readback->allocation,
                                  0,VK_WHOLE_SIZE)!=VK_SUCCESS)
            throw std::runtime_error{"diagnostic invalidation failed"};
        const auto* bytes=static_cast<const u8*>(diagnostic_readback->mapped);
        failure->Bytes("gpu.result.raw.u32",{bytes,static_cast<std::size_t>(output_bytes)});
        failure->Bytes("gpu.upload.raw",{bytes+output_bytes,static_cast<std::size_t>(upload_bytes)});
        failure->Label("gpu.result.scope","raw_buffer_current_readback; only header, declared prefix and written outputs are valid");
        constexpr u32 diagnostic_words=Pica::Shader::Generator::GLSL::ComputeGuestDiagnosticWords;
        std::array<u32,diagnostic_words> words{};
        std::memcpy(words.data(),bytes+output_bytes-sizeof(words),sizeof(words));
        failure->Value("gpu.nonfinite.sideband.u32",words);
        if(words[0]==0x434E4631u&&words[1]==1) {
            failure->Number("nonfinite_ordinal",words[2]);failure->Number("nonfinite_vertex_index",words[3]);
            failure->Number("nonfinite_semantic",words[4]);failure->Number("nonfinite_source_lane",words[5]);
            failure->Number("nonfinite_f32bits",words[6]);
            failure->Label("nonfinite_instruction","unknown; replay original program through failing invocation");
        }
    } catch(const std::exception& error) {
        failure->Unavailable("gpu.result.raw.u32",error.what());
        failure->Unavailable("gpu.upload.raw",error.what());
    } catch(...) {
        failure->Unavailable("gpu.result.raw.u32","unknown_failure_during_optional_readback");
    }
}

ComputeVertexProducer::ComputeVertexProducer(const Instance& instance, Scheduler& scheduler,
                                           DescriptorUpdateQueue& updates)
    : impl{std::make_unique<Impl>(instance, scheduler, updates)} {}
ComputeVertexProducer::~ComputeVertexProducer() = default;
ComputeVertexProducer::Batch ComputeVertexProducer::Produce(const ComputeVertexInput& input,
    bool disable_optimizer,std::shared_ptr<ComputeFailurePacket> failure) {
    if(!failure) {
        failure=MakeComputeFailurePacket(impl->device_evidence);
        if(failure) {
            try { failure->Input(std::make_shared<ComputeVertexInput>(input),true); }
            catch(...) { failure->Unavailable("input","retention_allocation_failed"); }
            failure->Label("entry_source","direct_producer_api_no_live_PICA_snapshot");
        }
    }
    return impl->Produce(input, disable_optimizer,std::move(failure));
}
void ComputeVertexProducer::MarkRasterUse() {
    impl->last_use = impl->scheduler.CurrentTick();
    if (impl->failure) impl->failure->Number("final_use_tick", impl->last_use);
}
std::shared_ptr<ComputeFailurePacket> ComputeVertexProducer::FailurePacket() const { return impl->failure; }
void ComputeVertexProducer::RetainFailedOutput() noexcept { impl->RetainFailedOutput(); }
void ComputeVertexProducer::RetireBeforeCapture() { impl->RetirePrevious(); }
std::shared_ptr<ComputeFailurePacket> ComputeVertexProducer::BeginFailurePacket(
    const Pica::PicaCore& pica, bool indexed, const Pica::ComputeAssemblyState& assembly,
    const Pica::AttributeBuffer* immediate) {
    return BeginComputeFailurePacket(impl->device_evidence, pica, indexed, assembly, immediate);
}
} // namespace Vulkan
