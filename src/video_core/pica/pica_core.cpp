// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring> // CodexAstraUlt: Classify periodic delivery without allocating strings.
#include <limits> // AstraPro: Checked index address arithmetic.
#include <chrono> // AstraEH: Bounded virtual-PICA stage timing.
#include <system_error> // CodexAstraLocal: Optional coordinator startup resource refusal.
#include "common/arch.h"
#include "common/archives.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/thread.h" // CodexAstraLocal: Identify real CPU vertex workers in activity samples.
#include "common/uberhar_test_profile.h" // CodexAstraUlt: Shared Combo admission policy.
#include "core/core.h"
#include "core/memory.h"
#include "core/loader/loader.h" // CodexAstraLocal: Bind the opt-in sidecar to this title.
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/uberhar_vertex_cache.h" // AstraEH: Exact FIFO with indexed lookup.
#include "video_core/pica/vertex_loader.h"
#include "video_core/pica/uberhar_parallel_vertex.h" // CodexAstraLocal: Prove invocation independence.
#include "video_core/pica/uberhar_vertex_parallel_batch.h" // CodexAstraLocal: Ordered parallel FIFO misses.
#include "video_core/pica/uberhar_index_bounds.h" // AstraPro: Exact bounded retry.
#include "video_core/pica/uberhar_vertex_timing_batch.h" // CodexAstraLocal: Separate diagnostic loop.
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h" // AstraPro: Pure admission.
#include "video_core/rasterizer_interface.h"
#include "video_core/shader/shader.h"
#include "video_core/pica/uberhar_cpu_draw_queue.h" // CodexAstraLocal: Bounded independent CPU draws.
#include "video_core/shader_recovery_error.h" // CodexAstraLocal: Shared terminal completion channel.

namespace Pica {

// CodexAstraLocal: Capacity is fixed in bytes/packets, while actual worker count
// follows the process's logical CPUs. Reserving owner bookkeeping before dispatch
// prevents an allocation failure from orphaning a published task.
struct PicaCore::DeferredVertexState {
    static constexpr std::size_t PacketLimit = 64;
    CpuDrawExecutor executor;
    std::vector<std::shared_ptr<CpuDrawPacket>> pending;
    std::size_t reconciled{}; // CodexAstraLocal: Terminal retries never recount a completed prefix.
    u64 submitted{};
    u64 completed{}, inputs{}, invocations{}, hits{}, capacity_drains{}, synchronous_boundaries{};
    u64 capture_submit_ns{}, capture_submit_max_ns{};
    // CodexAstraLocal TEST-ONLY LOG: Remove these diagnostic counters after the
    // redundant-reset experiment is qualified; they do not schedule any work.
    u64 deferred_topology_resets{}, deferred_primitive_resets{};
    explicit DeferredVertexState(unsigned processors)
        : executor{processors, PacketLimit, 8 * 1024 * 1024,
                   +[] { Common::SetCurrentThreadName("PICA draw pool"); }} {
        pending.reserve(PacketLimit);
    }
};

MICROPROFILE_DEFINE(GPU_Drawing, "GPU", "Drawing", MP_RGB(50, 50, 240));

using namespace DebugUtils;

// Class representing implementation details of each internal register
// The set/get pattern is used instead of a bitfield union to allow
// constexpr evaluation.
class RegImplInfo {
private:
    using NeedsSpecialHandlingBF = BitField<0, 1, u16>;
    using SupportsBatchBF = BitField<1, 1, u16>;
    using RegsUntilSpecialBF = BitField<2, 14, u16>;

    u16 raw{};

public:
    constexpr bool NeedsSpecialHandling() const {
        return NeedsSpecialHandlingBF::ExtractValue(raw) != 0;
    }

    constexpr void SetNeedsSpecialHandling() {
        raw = (raw & ~NeedsSpecialHandlingBF::mask) | NeedsSpecialHandlingBF::FormatValue(1);
    }

    constexpr bool SupportsBatch() const {
        return SupportsBatchBF::ExtractValue(raw) != 0;
    }

    constexpr void SetSupportsBatch() {
        raw = (raw & ~SupportsBatchBF::mask) | SupportsBatchBF::FormatValue(1);
    }

    constexpr u16 RegsUntilSpecial() const {
        return RegsUntilSpecialBF::ExtractValue(raw);
    }

    constexpr void SetRegsUntilSpecial(u16 value) {
        raw = (raw & ~RegsUntilSpecialBF::mask) | RegsUntilSpecialBF::FormatValue(value);
    }
};

union CommandHeader {
    u32 hex;
    BitField<0, 16, u32> cmd_id;
    BitField<16, 4, u32> parameter_mask;
    BitField<20, 8, u32> extra_data_length;
    BitField<31, 1, u32> group_commands;
};
static_assert(sizeof(CommandHeader) == sizeof(u32), "CommandHeader has incorrect size!");

// CodexAstraLocal: Keep proof storage bounded and separate from guest state.
// Ordinary draws reuse their current code identity without hashing or comparing
// shader arrays. On a source revision, hashes only shortlist candidates: exact
// bytes establish identity, so collisions cannot authorize unsafe parallel work.
struct PicaCore::ParallelVertexState {
    struct Key {
        u32 entry{}, outputs{};
        u16 booleans{};
        // CodexAstraLocal: Identical source may have different proofs when the
        // caller's observable-state contract changes without a code revision.
        ParallelVertexContract contract{ParallelVertexContract::FullArithmeticReads};
        bool operator==(const Key&) const = default;
    };
    struct Proof {
        bool valid{};
        Key key;
        ParallelVertexCertificate result;
    };
    struct Program {
        bool valid{};
        u64 program_hash{}, swizzle_hash{};
        ProgramCode code{};
        SwizzleData swizzles{};
        std::array<Proof, 16> proofs{};
        u32 cursor{};
    };
    NativeParallelBatch batch{Common::Uberhar::AvailableProcessors(), +[] {
        Common::SetCurrentThreadName("UberharVertex");
    }};
    std::array<Program, 8> programs{};
    Program* current{};
    u64 revision{};
    u32 cursor{};
    u64 proof_hits{}, proof_builds{}, code_compares{}, checks{}, parallel_batches{};
    u64 small{}, observer{}, context_missing{}, input_bounds{}, single_core{}, allocation_failures{};
    u64 float_traps{}; // CodexAstraLocal: Preserve owner-thread exception delivery.
    u64 concurrent_memory{}; // CodexAstraLocal: Retain serial live-memory observers.
    // CodexAstraLocal: Distinguish sinks that can change later input bytes during
    // ordered submission from independent background guest-memory writers.
    u64 submission_writes{};
    u64 owner_invocations{}, worker_invocations{}, chunks{};
    // CodexAstraLocal: One in 257 completed admitted draws measures phase wall
    // time. The prime stride avoids always sampling the same power-of-two slot;
    // these are sampled costs, never scaled into an asserted whole-run budget.
    u64 sampled_draws{}, sampled_inputs{}, sampled_invocations{}, sampled_chunks{};
    u64 plan_ns{}, pool_ns{}, owner_process_ns{}, join_ns{}, submit_ns{};
    // CodexAstraLocal: Attribute useful work to the explicitly narrower A64
    // contract; total-minus-observable remains the original contract population.
    u64 observable_batches{}, observable_owner_invocations{}, observable_worker_invocations{};
    // CodexAstraLocal: Weight serial refusals by actual completed FIFO misses,
    // not submitted indices or draw frequency; no per-vertex observer is needed.
    u64 serial_small_invocations{}, serial_carry_invocations{}, serial_other_invocations{};
    unsigned peak_threads{};
    std::array<u64, static_cast<std::size_t>(ParallelVertexStatus::Count)> admissions{};

    const ParallelVertexCertificate& Get(ShaderSetup& setup, u32 output_mask,
        ParallelVertexContract contract = ParallelVertexContract::FullArithmeticReads) {
        if (!current || revision != setup.GetCodeRevision()) {
            const u64 program_hash = setup.GetProgramCodeHash();
            const u64 swizzle_hash = setup.GetSwizzleDataHash();
            current = nullptr;
            for (auto& candidate : programs) {
                if (!candidate.valid || candidate.program_hash != program_hash ||
                    candidate.swizzle_hash != swizzle_hash)
                    continue;
                ++code_compares;
                if (candidate.code == setup.GetProgramCode() &&
                    candidate.swizzles == setup.GetSwizzleData()) {
                    current = &candidate;
                    break;
                }
            }
            if (!current) {
                current = &programs[cursor++ % programs.size()];
                current->valid = true;
                current->program_hash = program_hash;
                current->swizzle_hash = swizzle_hash;
                current->code = setup.GetProgramCode();
                current->swizzles = setup.GetSwizzleData();
                current->proofs = {};
                current->cursor = 0;
            }
            revision = setup.GetCodeRevision();
        }
        const Key key{setup.entry_point, output_mask,
                      ParallelVertexBooleanUniforms(setup.uniforms), contract};
        for (const auto& proof : current->proofs) {
            if (proof.valid && proof.key == key) {
                ++proof_hits;
                return proof.result;
            }
        }
        auto& proof = current->proofs[current->cursor++ % current->proofs.size()];
        proof.valid = false;
        proof.key = key;
        proof.result = AnalyzeParallelVertex(current->code, current->swizzles,
                                             key.entry, key.booleans, key.outputs, key.contract);
        proof.valid = true;
        ++proof_builds;
        return proof.result;
    }
};

PicaCore::PicaCore(Memory::MemorySystem& memory_, std::shared_ptr<DebugContext> debug_context_)
    : memory{memory_}, debug_context{std::move(debug_context_)},
      geometry_pipeline{regs.internal, gs_unit, gs_setup},
      shader_engine{CreateEngine(Settings::values.use_shader_jit.GetValue())} {
    InitializeRegs();
    dirty_regs.SetAllDirty();

    const auto submit_vertex = [this](const AttributeBuffer& buffer) {
        const auto add_triangle = [this](const OutputVertex& v0, const OutputVertex& v1,
                                         const OutputVertex& v2) {
            rasterizer->AddTriangle(v0, v1, v2);
        };
        const auto vertex = OutputVertex(regs.internal.rasterizer, buffer);
        primitive_assembler.SubmitVertex(vertex, add_triangle);
    };

    gs_unit.SetVertexHandlers(submit_vertex, [this]() { primitive_assembler.SetWinding(); });
    geometry_pipeline.SetVertexHandler(submit_vertex);

    primitive_assembler.Reconfigure(PipelineRegs::TriangleTopology::List);

    // CodexAstraLocal: Native/Custom never read this sidecar or construct clocks.
    // PerfStats starts later, so actual run/thread identity binds at the first draw.
    if (Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue())) {
        try {
            u64 title{};
            if (Core::System::GetInstance().GetAppLoader().ReadProgramId(title) ==
                Loader::ResultStatus::Success)
                vertex_timing = VertexTiming::Session::Load(title, shader_engine->EngineName());
        } catch (...) {
            // CodexAstraLocal: Optional title lookup/provider failure cannot
            // introduce a new failure of ordinary emulation initialization.
        }
    }
}

PicaCore::~PicaCore() {
    // CodexAstraLocal: Join published work before any shader/guest owner dies.
    // A previously reported terminal error must not escape this destructor.
    try { ReconcileDeferredVertices(); } catch (const VideoCore::ShaderRecoveryError&) {}
    if (Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom)
        ReportVirtualVertices("totals", std::chrono::steady_clock::now());
}

// AstraEH: Windowed CPU work identifies sustained geometry cost despite unequal run lengths.
// Human absence is not inferred: a game can continue rendering while its player steps away.
void PicaCore::ReportVirtualVertices(const char* kind, std::chrono::steady_clock::time_point now) {
    // CodexAstraUlt: Only repeated progress is optional; every final report stays reliable.
    const auto delivery = std::strcmp(kind, "progress") == 0
                              ? Common::Log::Delivery::Diagnostic
                              : Common::Log::Delivery::Reliable;
    if (deferred_vertices) {
        const auto& deferred = *deferred_vertices;
        const auto statistics = deferred.executor.GetStatistics();
        // CodexAstraLocal TEST-ONLY LOG: Remove reset counters after the queue
        // experiment is qualified. Reuse bounded progress/final reports.
        // Owner capture/submit wall is not total CPU work or GPU execution time.
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
            "Uberhar deferred CPU draws {}: schema=1 completed={} inputs={} invocations={} "
            "hits={} queued={} worker_completed={} failed={} waves={} max_wave={} peak_threads={} "
            "coordinator_packets={} auxiliary_packets={} resident_packets={} resident_bytes={} "
            "capacity_drains={} boundaries={} capture_attempts={} refused={} work_refused={} "
            "capture_submit_ms={:.3f} capture_submit_max_ms={:.3f} "
            "deferred_topology_resets={} deferred_primitive_resets={} "
            "scope=complete_list_or_shader_no_gs min_misses=96 max_vertices=255 min_arithmetic=35 "
            "max_copy_bytes_per_miss=70 timing=owner_capture_submit_not_frame_time",
            kind, deferred.completed, deferred.inputs, deferred.invocations, deferred.hits,
            statistics.submitted, statistics.completed, statistics.failed, statistics.waves,
            statistics.maximum_wave, statistics.peak_threads, statistics.coordinator_packets,
            statistics.auxiliary_packets, statistics.resident_packets, statistics.resident_bytes,
            deferred.capacity_drains, deferred.synchronous_boundaries, statistics.captures,
            statistics.refused, statistics.work_refused, deferred.capture_submit_ns / 1e6,
            deferred.capture_submit_max_ns / 1e6, deferred.deferred_topology_resets,
            deferred.deferred_primitive_resets);
    }
    // AstraPro: Existing five-second/final reporting gate. Periodic
    // samples can alias recurring draw patterns; never extrapolate a GPU budget.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar GPU host attempts {}: schema=1 success_samples={} fallback_samples={} "
             "success_ms={:.3f} fallback_ms={:.3f} sample_max_ms={:.3f} period_attempts=1024 "
             "scope=sampled_host_acceleration_prepare_submit timing=host_wall_gpu_execution_unknown",
             kind, ready_gpu_host_success_samples, ready_gpu_host_fallback_samples,
             ready_gpu_host_success_ns / 1e6, ready_gpu_host_fallback_ns / 1e6,
             ready_gpu_host_max_ns / 1e6);

    const double window_ms =
        virtual_window_start == std::chrono::steady_clock::time_point{}
            ? 0.0
            : std::chrono::duration<double, std::milli>(now - virtual_window_start).count();
    // AstraEH: At most once per five seconds plus shutdown; never per vertex.
    // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar virtual vertices {}: batches={} input_vertices={} stage_wall_ms={:.3f} "
             "stage_max_wall_ms={:.3f} engine={} shader_invocations={} cache_hits={} "
             "window_wall_ms={:.3f} window_stage_ms={:.3f} window_inputs={} window_invocations={} "
             "scope=synchronous_cpu_vertex_path deferred_report=separate",
             kind, virtual_vertex_batches, virtual_vertex_inputs, virtual_vertex_ns / 1e6,
             virtual_vertex_max_ns / 1e6, shader_engine->EngineName(), virtual_vertex_invocations,
             virtual_vertex_hits, window_ms, (virtual_vertex_ns - virtual_last_ns) / 1e6,
             virtual_vertex_inputs - virtual_last_inputs,
             virtual_vertex_invocations - virtual_last_invocations);
    // AstraEH: Same five-second cadence/shutdown as the existing vertex summary.
    // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar native vertices {}: schema=2 batches={} inputs={} conversions={} "
             "conversion_reuses={} mapping_fallbacks={} geometry_fallbacks={} debug_fallbacks={} "
             "sample_misses={} sample_hits={} sample_input_ms={:.6f} sample_shader_ms={:.6f} "
             "sample_output_ms={:.6f} sample_submit_ms={:.6f} sample_period_ms=50 "
             "sample_batches={} sample_batch_inputs={} sample_batch_invocations={} "
             "sample_setup_ms={:.6f} sample_vertex_ms={:.6f} sample_draw_ms={:.6f} "
             "sample_draw_max_ms={:.6f}",
             kind, native_vertex_batches, native_vertex_inputs, native_vertex_conversions,
             native_vertex_reuses, native_mapping_fallbacks, native_geometry_fallbacks,
             native_debug_fallbacks, native_samples.misses, native_samples.hits,
             native_samples.input_ns / 1e6, native_samples.shader_ns / 1e6,
             native_samples.output_ns / 1e6, native_samples.submit_ns / 1e6, native_samples.batches,
             native_samples.batch_inputs, native_samples.batch_invocations,
             native_samples.setup_ns / 1e6, native_samples.vertex_ns / 1e6,
             native_samples.draw_ns / 1e6, native_samples.draw_max_ns / 1e6);
    // AstraEH: Reuse the existing five-second/shutdown cadence; no per-vertex logs.
    // CodexAstraUlt Log Line: Replace AstraEH's blocking progress enqueue; totals stay reliable.
    // CodexAstraLocal Log Line: The five actual-invocation buckets partition
    // fused_vertices within this PICA owner; optional progress loss is not zero
    // coverage. Tags 0/1/2/3/4 mean generic/5-attribute/11-attribute/2-attribute/
    // 4-attribute complete recipes, not actor or elapsed-time attribution.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar vertex input {}: schema=2 ready_batches={} missing_attribute={} "
             "unconfigured={} address_wrap={} short_mapping={} mapped_attributes={} "
             "fused_vertices={} legacy_vertices={} recipe0_vertices={} recipe1_vertices={} "
             "recipe2_vertices={} recipe3_vertices={} recipe4_vertices={} "
             "scope=no_gs_native_transport memory_reuse=within_batch_only",
             kind, native_input_results[0], native_input_results[1], native_input_results[2],
             native_input_results[3], native_input_results[4], native_input_maps,
             native_input_fused_vertices, native_input_legacy_vertices,
             native_input_recipe_invocations[0], native_input_recipe_invocations[1],
             native_input_recipe_invocations[2], native_input_recipe_invocations[3],
             native_input_recipe_invocations[4]);
    // CodexAstraLocal Log Line: Reuse the existing report cadence. Count actual
    // completed worker invocations and refusals, not created threads as useful
    // work. Sparse vertex observers remain serial and cannot time worker kernels.
    if (parallel_vertices) {
        const auto& parallel = *parallel_vertices;
        // CodexAstraLocal: Close refusal accounting without per-draw formatting;
        // detailed carry reasons remain separate from other unsupported contracts.
        u64 other_refusals = 0;
        for (std::size_t i = 0; i < parallel.admissions.size(); ++i) {
            const auto status = static_cast<ParallelVertexStatus>(i);
            if (status != ParallelVertexStatus::Independent &&
                status != ParallelVertexStatus::TemporaryCarry &&
                status != ParallelVertexStatus::AddressCarry &&
                status != ParallelVertexStatus::ConditionCarry &&
                status != ParallelVertexStatus::OutputCarry)
                other_refusals += parallel.admissions[i];
        }
        LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
            "Uberhar CPU parallel {}: schema=3 available={} created_workers={} peak_threads={} "
            "checks={} batches={} chunks={} owner_invocations={} worker_invocations={} "
            "observable_batches={} observable_owner_invocations={} observable_worker_invocations={} "
            "small={} observer={} context_missing={} input_bounds={} single_core={} float_traps={} concurrent_memory={} submission_writes={} "
            "allocation_failures={} startup_failures={} proof_hits={} proof_builds={} code_compares={} "
            "independent={} temporary_carry={} address_carry={} condition_carry={} output_carry={} other_refusals={} "
            "serial_small_invocations={} serial_carry_invocations={} serial_other_invocations={} "
            "sampled_draws={} sampled_inputs={} sampled_invocations={} sampled_chunks={} "
            "plan_ns={} pool_ns={} owner_process_ns={} join_ns={} submit_ns={} timing_stride=257 wait_transport={} "
            "scope=certified_no_gs_fifo_misses serial_scope=completed_attempted_draws submit=ordered_owner observer_policy=serial",
            kind, parallel.batch.Available(), parallel.batch.CreatedWorkers(), parallel.peak_threads,
            parallel.checks, parallel.parallel_batches, parallel.chunks, parallel.owner_invocations,
            parallel.worker_invocations, parallel.observable_batches,
            parallel.observable_owner_invocations, parallel.observable_worker_invocations,
            parallel.small, parallel.observer, parallel.context_missing,
            parallel.input_bounds, parallel.single_core, parallel.float_traps,
            parallel.concurrent_memory, parallel.submission_writes, parallel.allocation_failures,
            parallel.batch.StartupFailures(), parallel.proof_hits, parallel.proof_builds,
            parallel.code_compares,
            parallel.admissions[static_cast<std::size_t>(ParallelVertexStatus::Independent)],
            parallel.admissions[static_cast<std::size_t>(ParallelVertexStatus::TemporaryCarry)],
            parallel.admissions[static_cast<std::size_t>(ParallelVertexStatus::AddressCarry)],
            parallel.admissions[static_cast<std::size_t>(ParallelVertexStatus::ConditionCarry)],
            parallel.admissions[static_cast<std::size_t>(ParallelVertexStatus::OutputCarry)],
            other_refusals, parallel.serial_small_invocations,
            parallel.serial_carry_invocations, parallel.serial_other_invocations,
            parallel.sampled_draws, parallel.sampled_inputs, parallel.sampled_invocations,
            parallel.sampled_chunks, parallel.plan_ns, parallel.pool_ns,
            parallel.owner_process_ns, parallel.join_ns, parallel.submit_ns,
            parallel.batch.WaitTransport());
    }

    // AstraPro: Existing five-second/final cadence; no per-index clocks.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar index bounds {}: schema=1 retries={} scanned_indices={} rescued_batches={} "
             "rescued_vertices={} escaped_vertices={} scan_cap=262144 memory_reuse=within_batch_only",
             kind, native_index_retries, native_scanned_indices, native_index_rescues,
             native_rescued_vertices, native_index_escapes);
    // AstraPro: Existing cadence/final; GPU inputs are submitted indices,
    // not a count of actual driver shader invocations or measured GPU time.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar PICA routes {}: schema=2 cpu_batches={} gpu_batches={} gpu_inputs={} "
             "gpu_attempts={} auto_topologies=[{},{},{},{},{}] gpu_invocations=unknown "
             "cpu_synchronous_batches={} cpu_deferred_completed={}",
             kind, virtual_vertex_batches + (deferred_vertices ? deferred_vertices->completed : 0),
             ready_gpu_vertex_batches, ready_gpu_vertex_inputs,
             ready_gpu_vertex_attempts, ready_gpu_topologies[0], ready_gpu_topologies[1],
             ready_gpu_topologies[2], ready_gpu_topologies[3], ready_gpu_topologies[4],
             virtual_vertex_batches, deferred_vertices ? deferred_vertices->completed : 0);
    // AstraPro: Same 4096-batch/five-second cadence, plus final totals.
    // Draw-weighted admission is not vertex, pixel or GPU-time coverage.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar GPU admission {}: schema=1 scope=pica_title eligible_list={} "
             "eligible_shader_list={} disabled={} debugger={} geometry={} assembly={} "
             "winding={} topology={} small={} large={} incomplete={} "
             "selected_topologies=[{},{},{},{},{}] weighting=draws exclusive=true",
             kind, ready_gpu_admissions[0], ready_gpu_admissions[1], ready_gpu_admissions[2],
             ready_gpu_admissions[3], ready_gpu_admissions[4], ready_gpu_admissions[5],
             ready_gpu_admissions[6], ready_gpu_admissions[7], ready_gpu_admissions[8],
             ready_gpu_admissions[9], ready_gpu_admissions[10], ready_gpu_selected_topologies[0],
             ready_gpu_selected_topologies[1], ready_gpu_selected_topologies[2],
             ready_gpu_selected_topologies[3], ready_gpu_selected_topologies[4]);
    // AstraPro: Same five-second/final gate; hits are preparation reuse,
    // not vertex-cache hits or a measured speedup. No game data is retained.
    // CodexAstraUlt Log Line: Replace AstraPro's blocking progress enqueue; totals stay reliable.
    LOG_INFO_WITH_DELIVERY(Render_Vulkan, delivery,
             "Uberhar native plan {}: schema=1 hits={} builds={} capacity=1 "
             "scope=pica_lifetime reuse=register_semantics_only",
             kind, native_plan_cache.Hits(), native_plan_cache.Builds());
    virtual_window_start = now;
    virtual_last_ns = virtual_vertex_ns;
    virtual_last_inputs = virtual_vertex_inputs;
    virtual_last_invocations = virtual_vertex_invocations;
}

void PicaCore::InitializeRegs() {
    // Values initialized by GSP
    regs.internal.irq_autostop = 1;
    regs.internal.irq_mask = 0xFFFFFFF0;
    // Older versions of libctru didn't initialize this, initialize it here to avoid endless black
    // screen. Not needed on actual hardware due to previous software already having set it up
    regs.internal.irq_compare = 0x12345678;

    auto& framebuffer_top = regs.framebuffer_config[0];
    auto& framebuffer_sub = regs.framebuffer_config[1];

    // Set framebuffer defaults from nn::gx::Initialize
    framebuffer_top.address_left1 = 0x181E6000;
    framebuffer_top.address_left2 = 0x1822C800;
    framebuffer_top.address_right1 = 0x18273000;
    framebuffer_top.address_right2 = 0x182B9800;
    framebuffer_sub.address_left1 = 0x1848F000;
    framebuffer_sub.address_left2 = 0x184C7800;

    framebuffer_top.width.Assign(240);
    framebuffer_top.height.Assign(400);
    framebuffer_top.stride = 3 * 240;
    framebuffer_top.color_format.Assign(PixelFormat::RGB8);
    framebuffer_top.active_fb = 0;

    framebuffer_sub.width.Assign(240);
    framebuffer_sub.height.Assign(320);
    framebuffer_sub.stride = 3 * 240;
    framebuffer_sub.color_format.Assign(PixelFormat::RGB8);
    framebuffer_sub.active_fb = 0;

    // Tales of Abyss expects this register to have the following default values.
    auto& gs = regs.internal.gs;
    gs.max_input_attribute_index.Assign(1);
    gs.shader_mode.Assign(ShaderRegs::ShaderMode::VS);
}

void PicaCore::BindRasterizer(VideoCore::RasterizerInterface* rasterizer) {
    // CodexAstraLocal: No queued CPU result belongs to a newly rebound sink.
    ReconcileDeferredVertices();
    this->rasterizer = rasterizer;
}

void PicaCore::SetInterruptHandler(Service::GSP::InterruptHandler& signal_interrupt) {
    this->signal_interrupt = signal_interrupt;
}

static consteval std::array<RegImplInfo, RegsInternal::NUM_REGS> BuildRegImplFlagsLUT() {
    std::array<RegImplInfo, RegsInternal::NUM_REGS> table{};

    // Marks the register as needing special handling.
    const auto mark_special = [&table](u32 index) { table[index].SetNeedsSpecialHandling(); };

    // Marks the register as supporting batch writes.
    const auto mark_batch = [&table](u32 index) { table[index].SetSupportsBatch(); };

    // Single registers
    mark_special(PICA_REG_INDEX(irq_request));
    mark_special(PICA_REG_INDEX(pipeline.triangle_topology));
    mark_special(PICA_REG_INDEX(pipeline.restart_primitive));
    mark_special(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.index));
    mark_special(PICA_REG_INDEX(pipeline.trigger_draw));
    mark_special(PICA_REG_INDEX(pipeline.trigger_draw_indexed));
    mark_special(PICA_REG_INDEX(gs.bool_uniforms));
    mark_special(PICA_REG_INDEX(vs.output_mask));
    mark_special(PICA_REG_INDEX(vs.bool_uniforms));

    // Array based registers
    for (u32 i = 0; i < 2; ++i) {
        mark_special(PICA_REG_INDEX(pipeline.command_buffer.trigger[0]) + i);
    }
    for (u32 i = 0; i < 3; ++i) {
        mark_special(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]) + i);
        mark_batch(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]) + i);
    }
    for (u32 i = 0; i < 4; ++i) {
        mark_special(PICA_REG_INDEX(vs.int_uniforms[0]) + i);
        mark_special(PICA_REG_INDEX(gs.int_uniforms[0]) + i);
    }
    for (u32 i = 0; i < 8; ++i) {
        mark_special(PICA_REG_INDEX(gs.uniform_setup.set_value[0]) + i);
        mark_batch(PICA_REG_INDEX(gs.uniform_setup.set_value[0]) + i);
        mark_special(PICA_REG_INDEX(vs.uniform_setup.set_value[0]) + i);
        mark_batch(PICA_REG_INDEX(vs.uniform_setup.set_value[0]) + i);
        mark_special(PICA_REG_INDEX(gs.program.set_word[0]) + i);
        mark_batch(PICA_REG_INDEX(gs.program.set_word[0]) + i);
        mark_special(PICA_REG_INDEX(vs.program.set_word[0]) + i);
        mark_batch(PICA_REG_INDEX(vs.program.set_word[0]) + i);
        mark_special(PICA_REG_INDEX(gs.swizzle_patterns.set_word[0]) + i);
        mark_batch(PICA_REG_INDEX(gs.swizzle_patterns.set_word[0]) + i);
        mark_special(PICA_REG_INDEX(vs.swizzle_patterns.set_word[0]) + i);
        mark_batch(PICA_REG_INDEX(vs.swizzle_patterns.set_word[0]) + i);
        mark_special(PICA_REG_INDEX(lighting.lut_data[0]) + i);
        mark_batch(PICA_REG_INDEX(lighting.lut_data[0]) + i);
        mark_special(PICA_REG_INDEX(texturing.fog_lut_data[0]) + i);
        mark_batch(PICA_REG_INDEX(texturing.fog_lut_data[0]) + i);
        mark_special(PICA_REG_INDEX(texturing.proctex_lut_data[0]) + i);
        mark_batch(PICA_REG_INDEX(texturing.proctex_lut_data[0]) + i);
    }

    // Build distances to next special register.
    u16 regs_since_special = std::numeric_limits<u16>::max();
    for (size_t i = RegsInternal::NUM_REGS; i-- > 0;) {
        if (table[i].NeedsSpecialHandling()) {
            regs_since_special = 0;
        }
        table[i].SetRegsUntilSpecial(regs_since_special);
        if (regs_since_special != std::numeric_limits<u16>::max()) {
            regs_since_special++;
        }
    }

    return table;
}

static constexpr std::array<RegImplInfo, RegsInternal::NUM_REGS> reg_impl_flags_lut =
    BuildRegImplFlagsLUT();

// Expand a 4-bit mask to 4-byte mask, e.g. 0b0101 -> 0x00FF00FF
static constexpr std::array<u32, 16> ExpandBitsToBytes = {
    0x00000000, 0x000000ff, 0x0000ff00, 0x0000ffff, 0x00ff0000, 0x00ff00ff, 0x00ffff00, 0x00ffffff,
    0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff, 0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff,
};

/**
 * This is the main loop for processing GPU command lists. On Azahar, it's the most
 * CPU expensive function (excluding the inner Draw calls) due to applications submitting
 * 10-50 command lists per frame, each with hundreds of commands in them. For this reason,
 * it is important that this function is well optimized to reduce the load on the CPU.
 *
 * Each command in the list has the following properties:
 *  - Commands come in [value (32 bit), header (32 bit)] pairs, most of the time.
 *  - The register ID that the 32 bit value should be written to is stored in the header.
 *  - The mask of bits that should be written comes in the header
 *    (to be able to write individual bytes of the 4-byte register)
 *  - Commands can have an extra length N, which means that N extra words follow
 *    after the header word.
 *    - If group_command is set in the header, N sequential registers are
 *      written to starting from the ID + 1 specified in the header.
 *    - If group_command is not set, the same register is written
 *      to with the N extra words. This is used for things like shader uploads
 *      which has a single register ID.
 *
 * Regarding implementation details, we store all register values in an array,
 * as well as a dirty array to indicate which registers have changed since
 * the last draw. Some registers need special handling, as they are "trigger"
 * registers that start the draw, or store the data in the shader units.
 *
 * To be able to determine if a register is special, we use a lookup table
 * generated by BuildRegImplFlagsLUT(). This allows determining if a register
 * is special or not in O(1). This LUT also determines if a register has
 * support for batch handling (extra_data_length != 0 && group_command == 0)
 * and the amount of commands away from the next special register, useful for
 * sequential writes (extra_data_length != 0 && group_command == 1).
 *
 * As much as possible, we want to target the following optimizations:
 *  - We should prevent branches and jumps to functions if they are not needed.
 *  - We should clearly separate special command handling from normal commands
 *    that are much cheaper to handle.
 *  - Commands with extra length should be processed in batch if possible.
 *  - Vectorization should be used as much as possible.
 *
 * On the other hand, if PICA debugging is enabled we should avoid optimizations
 * that would make debugging more complicated.
 */
void PicaCore::ProcessCmdList(PAddr list, u32 size, bool ignore_list) [[hot]] {
    if (ignore_list) {
        signal_interrupt(Service::GSP::InterruptId::P3D, delay_generator.CalculateAndResetDelay());
        return;
    }

    const u8* head = memory.GetPhysicalPointer(list);
    cmd_list.Reset(list, head, size);

    bool stop_requested = false;
    bool skip_fast_path = false;
    while (cmd_list.current_index < cmd_list.length) {
        if (stop_requested) [[unlikely]] {
            break;
        }
        if (cmd_list.current_index % 2 != 0) {
            cmd_list.current_index++;
        }

        // Early path that processes commands in batches of 4. If any of the commands
        // needs special handling or has extra length it stops and falls back to the
        // slower path. This pattern allows the compiler to auto-vectorize the function
        // if the current ISA allows it (that's why we process in batches of 4
        // as most SIMD operations work with 128 bit registers). MSVC is not able to
        // auto-vectorize this part with SSE4.2, due to the LUT read, instead it just
        // unrolls the loop. Other ISAs and/or compilers may be able to do it,
        // that's why it was decided to keep the structure like this.
        if (!debug_context) [[likely]] {
            if (!skip_fast_path) {
                constexpr u32 batch_size = 4;
                u32 index = cmd_list.current_index;
                u32 ids[batch_size], values[batch_size], masks[batch_size];
                u32 run = 0;

                while (run < batch_size && index + 1 < cmd_list.length) {
                    const u32 value = cmd_list.head[index];
                    const CommandHeader header{cmd_list.head[index + 1]};

                    // If extra handling is needed stop and fallback to slower path.
                    if (header.extra_data_length != 0 || header.cmd_id >= RegsInternal::NUM_REGS ||
                        reg_impl_flags_lut[header.cmd_id].NeedsSpecialHandling()) {
                        skip_fast_path = true;
                        break;
                    }

                    ids[run] = header.cmd_id;
                    values[run] = value;
                    masks[run] = header.parameter_mask;
                    ++run;
                    index += 2;
                }

                // Process the commands that we have read so far (up to 4).
                if (run > 0) {
                    delay_generator.AddCommands(run);
                    for (u32 i = 0; i < run; ++i) {
                        const u32 id = ids[i];
                        const u32 write_mask = ExpandBitsToBytes[masks[i]];
                        regs.internal.reg_array[id] =
                            (regs.internal.reg_array[id] & ~write_mask) | (values[i] & write_mask);
                        dirty_regs.Set(id);
                    }
                    cmd_list.current_index = index;

                    // Continue from the while loop in case we reached the end of the list.
                    continue;
                }
            }
        }
        // Slow path, command needs special handling.

        skip_fast_path = false;

        // Read the header and the value to write.
        const u32 value = cmd_list.head[cmd_list.current_index++];
        const CommandHeader header{cmd_list.head[cmd_list.current_index++]};

        // Write to the requested PICA register.
        WriteInternalReg(header.cmd_id, value, header.parameter_mask, stop_requested);

        // Write any extra paramters as well.
        const u32 count = header.extra_data_length;
        if (count == 0)
            continue;

        if (debug_context) [[unlikely]] {
            // Fallback to per word register writes if debugging is
            // enabled.
            for (u32 i = 0; i < count; ++i) {
                if (stop_requested) [[unlikely]] {
                    break;
                }
                const u32 cmd = header.cmd_id + (header.group_commands ? i + 1 : 0);
                const u32 extra_value = cmd_list.head[cmd_list.current_index++];
                WriteInternalReg(cmd, extra_value, header.parameter_mask, stop_requested);
            }
        } else {
            // Handle commands with extra length.
            const u32* extra = &cmd_list.head[cmd_list.current_index];
            cmd_list.current_index += count;

            if (!header.group_commands) {
                // Same register written count times in a row (program/swizzle upload, LUT, etc.).
                WriteInternalRegBatch(header.cmd_id, extra, count, header.parameter_mask,
                                      stop_requested);
            } else {
                // Sequential registers written from header.cmd_id+1 to header.cmd_id+count.
                WriteInternalRegSequential(header.cmd_id + 1, extra, count, header.parameter_mask,
                                           stop_requested);
            }
        }
    }
    // CodexAstraLocal: Return to guest scheduling only after completed CPU draw
    // state and errors are reconciled. Vulkan retains its normal asynchronous work.
    ReconcileDeferredVertices();
}

static bool any_byte_match(u32 a, u32 b) {
    return ((a & 0xFF) == (b & 0xFF)) || (((a >> 8) & 0xFF) == ((b >> 8) & 0xFF)) ||
           (((a >> 16) & 0xFF) == ((b >> 16) & 0xFF)) || (((a >> 24) & 0xFF) == ((b >> 24) & 0xFF));
}

// Handle registers which our backend support batch writes.
void PicaCore::HandleSpecialRegBatch(u32 id, const u32* values, u32 count) {
    switch (id) {
    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]):
    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[1]):
    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[2]): {
        for (u32 i = 0; i < count; i++) {
            SubmitImmediate(values[i]);
        }
        break;
    }
    case PICA_REG_INDEX(gs.uniform_setup.set_value[0]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[1]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[2]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[3]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[4]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[5]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[6]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[7]): {
        gs_setup.WriteUniformFloatRegRange(regs.internal.gs, values, count);
        break;
    }
    case PICA_REG_INDEX(gs.program.set_word[0]):
    case PICA_REG_INDEX(gs.program.set_word[1]):
    case PICA_REG_INDEX(gs.program.set_word[2]):
    case PICA_REG_INDEX(gs.program.set_word[3]):
    case PICA_REG_INDEX(gs.program.set_word[4]):
    case PICA_REG_INDEX(gs.program.set_word[5]):
    case PICA_REG_INDEX(gs.program.set_word[6]):
    case PICA_REG_INDEX(gs.program.set_word[7]): {
        u32& offset = regs.internal.gs.program.offset;
        if (offset + count > 4096) {
            LOG_ERROR(HW_GPU, "Invalid GS program offset {} count {}", offset, count);
        } else {
            gs_setup.UpdateProgramCodeRange(offset, values, count);
            offset += count;
        }
        break;
    }
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[0]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[1]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[2]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[3]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[4]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[5]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[6]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[7]): {
        u32& offset = regs.internal.gs.swizzle_patterns.offset;
        if (offset + count > gs_setup.GetSwizzleData().size()) {
            LOG_ERROR(HW_GPU, "Invalid GS swizzle pattern offset {} count {}", offset, count);
        } else {
            gs_setup.UpdateSwizzleDataRange(offset, values, count);
            offset += count;
        }
        break;
    }
    case PICA_REG_INDEX(vs.uniform_setup.set_value[0]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[1]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[2]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[3]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[4]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[5]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[6]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[7]): {
        const auto range = vs_setup.WriteUniformFloatRegRange(regs.internal.vs, values, count);
        if (range && !regs.internal.pipeline.gs_unit_exclusive_configuration &&
            regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
            for (u32 i = 0; i < range->count; ++i) {
                const u32 idx = range->first_index + i;
                gs_setup.uniforms.f[idx] = vs_setup.uniforms.f[idx];
            }
        }
        break;
    }
    case PICA_REG_INDEX(vs.program.set_word[0]):
    case PICA_REG_INDEX(vs.program.set_word[1]):
    case PICA_REG_INDEX(vs.program.set_word[2]):
    case PICA_REG_INDEX(vs.program.set_word[3]):
    case PICA_REG_INDEX(vs.program.set_word[4]):
    case PICA_REG_INDEX(vs.program.set_word[5]):
    case PICA_REG_INDEX(vs.program.set_word[6]):
    case PICA_REG_INDEX(vs.program.set_word[7]): {
        u32& offset = regs.internal.vs.program.offset;
        if (offset + count > 512) {
            LOG_ERROR(HW_GPU, "Invalid VS program offset {} count {}", offset, count);
        } else {
            vs_setup.UpdateProgramCodeRange(offset, values, count);
            if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
                regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
                gs_setup.UpdateProgramCodeRange(offset, values, count);
            }
            offset += count;
        }
        break;
    }
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[0]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[1]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[2]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[3]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[4]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[5]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[6]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[7]): {
        u32& offset = regs.internal.vs.swizzle_patterns.offset;
        if (offset + count > vs_setup.GetSwizzleData().size()) {
            LOG_ERROR(HW_GPU, "Invalid VS swizzle pattern offset {} count {}", offset, count);
        } else {
            vs_setup.UpdateSwizzleDataRange(offset, values, count);
            if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
                regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
                gs_setup.UpdateSwizzleDataRange(offset, values, count);
            }
            offset += count;
        }
        break;
    }
    case PICA_REG_INDEX(lighting.lut_data[0]):
    case PICA_REG_INDEX(lighting.lut_data[1]):
    case PICA_REG_INDEX(lighting.lut_data[2]):
    case PICA_REG_INDEX(lighting.lut_data[3]):
    case PICA_REG_INDEX(lighting.lut_data[4]):
    case PICA_REG_INDEX(lighting.lut_data[5]):
    case PICA_REG_INDEX(lighting.lut_data[6]):
    case PICA_REG_INDEX(lighting.lut_data[7]): {
        auto& lut_config = regs.internal.lighting.lut_config;

        for (u32 i = 0; i < count; i++) {
            const u32 prev =
                std::exchange(lighting
                                  .luts[lut_config.type][(lut_config.index + i) %
                                                         lighting.luts[lut_config.type].size()]
                                  .raw,
                              values[i]);
            lighting.lut_dirty |= (prev != values[i]) << lut_config.type;
        }
        lut_config.index.Assign(lut_config.index + count);
        break;
    }
    case PICA_REG_INDEX(texturing.fog_lut_data[0]):
    case PICA_REG_INDEX(texturing.fog_lut_data[1]):
    case PICA_REG_INDEX(texturing.fog_lut_data[2]):
    case PICA_REG_INDEX(texturing.fog_lut_data[3]):
    case PICA_REG_INDEX(texturing.fog_lut_data[4]):
    case PICA_REG_INDEX(texturing.fog_lut_data[5]):
    case PICA_REG_INDEX(texturing.fog_lut_data[6]):
    case PICA_REG_INDEX(texturing.fog_lut_data[7]): {
        for (u32 i = 0; i < count; i++) {
            const u32 prev = std::exchange(
                fog.lut[(regs.internal.texturing.fog_lut_offset + i) % 128].raw, values[i]);
            fog.lut_dirty |= prev != values[i];
        }
        regs.internal.texturing.fog_lut_offset.Assign(regs.internal.texturing.fog_lut_offset +
                                                      count);
        break;
    }
    case PICA_REG_INDEX(texturing.proctex_lut_data[0]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[1]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[2]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[3]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[4]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[5]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[6]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[7]): {
        auto& index = regs.internal.texturing.proctex_lut_config.index;
        const auto lut_table = regs.internal.texturing.proctex_lut_config.ref_table.Value();

        for (u32 i = 0; i < count; i++) {

            const auto sync_lut = [&](auto& proctex_table) {
                const u32 prev =
                    std::exchange(proctex_table[(index + i) % proctex_table.size()].raw, values[i]);
                proctex.table_dirty |= (prev != values[i]) << u32(lut_table);
            };

            switch (lut_table) {
            case TexturingRegs::ProcTexLutTable::Noise:
                sync_lut(proctex.noise_table);
                break;
            case TexturingRegs::ProcTexLutTable::ColorMap:
                sync_lut(proctex.color_map_table);
                break;
            case TexturingRegs::ProcTexLutTable::AlphaMap:
                sync_lut(proctex.alpha_map_table);
                break;
            case TexturingRegs::ProcTexLutTable::Color:
                sync_lut(proctex.color_table);
                break;
            case TexturingRegs::ProcTexLutTable::ColorDiff:
                sync_lut(proctex.color_diff_table);
                break;
            }
        }
        index.Assign(index + count);
        break;
    }
    }
}

// Handle special registers. This function should also include the
// registers that support batch processing can be submitted
// individually.
void PicaCore::HandleSpecialReg(u32 id, u32 value, bool& stop_requested) {
    switch (id) {
    // Trigger IRQ
    case PICA_REG_INDEX(irq_request):
        // TODO(PabloMK7): This logic is not fully accurate, but close enough:
        // https://problemkaputt.de/gbatek-3ds-gpu-internal-registers-finalize-interrupt-registers.htm
        if (any_byte_match(regs.internal.reg_array[id], regs.internal.irq_compare)) [[likely]] {
            // CodexAstraLocal: P3D completion cannot precede pending CPU results.
            ReconcileDeferredVertices();
            signal_interrupt(Service::GSP::InterruptId::P3D,
                             delay_generator.CalculateAndResetDelay());
            if (regs.internal.irq_autostop) [[likely]] {
                stop_requested = true;
            }
        }
        break;

    case PICA_REG_INDEX(pipeline.triangle_topology):
        // CodexAstraLocal: The register is already written; compare the incoming
        // value with the assembler's old topology. Keep complete packets queued
        // only across a logical no-op; every actual topology change still joins.
        if (!primitive_assembler.CanResetDeferredTriangles(regs.internal.pipeline.triangle_topology)) {
            ReconcileDeferredVertices();
        } else if (deferred_vertices && !deferred_vertices->pending.empty()) {
            ++deferred_vertices->deferred_topology_resets;
        }
        primitive_assembler.Reconfigure(regs.internal.pipeline.triangle_topology);
        break;

    case PICA_REG_INDEX(pipeline.restart_primitive):
        // CodexAstraLocal: Reset retains buffer[], and pending complete packets
        // retain its latest pair. Real observers still reconcile before reading.
        if (!primitive_assembler.CanResetDeferredTriangles(primitive_assembler.GetTopology())) {
            ReconcileDeferredVertices();
        } else if (deferred_vertices && !deferred_vertices->pending.empty()) {
            ++deferred_vertices->deferred_primitive_resets;
        }
        primitive_assembler.Reset();
        break;

    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.index):
        immediate.Reset();
        break;

    // Load default vertex input attributes
    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]):
    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[1]):
    case PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[2]):
        SubmitImmediate(value);
        break;

    case PICA_REG_INDEX(pipeline.gpu_mode):
        // This register likely just enables vertex processing and doesn't need any special handling
        break;

    case PICA_REG_INDEX(pipeline.command_buffer.trigger[0]):
    case PICA_REG_INDEX(pipeline.command_buffer.trigger[1]): {
        const u32 index = static_cast<u32>(id - PICA_REG_INDEX(pipeline.command_buffer.trigger[0]));
        const PAddr addr = regs.internal.pipeline.command_buffer.GetPhysicalAddress(index);
        const u32 size = regs.internal.pipeline.command_buffer.GetSize(index);
        const u8* head = memory.GetPhysicalPointer(addr);
        cmd_list.Reset(addr, head, size);
        break;
    }

    // It seems like these trigger vertex rendering
    case PICA_REG_INDEX(pipeline.trigger_draw):
    case PICA_REG_INDEX(pipeline.trigger_draw_indexed): {
        const bool is_indexed = (id == PICA_REG_INDEX(pipeline.trigger_draw_indexed));
        DrawArrays(is_indexed);
        break;
    }

    case PICA_REG_INDEX(gs.bool_uniforms):
        gs_setup.WriteUniformBoolReg(regs.internal.gs.bool_uniforms.Value());
        break;

    case PICA_REG_INDEX(gs.int_uniforms[0]):
    case PICA_REG_INDEX(gs.int_uniforms[1]):
    case PICA_REG_INDEX(gs.int_uniforms[2]):
    case PICA_REG_INDEX(gs.int_uniforms[3]): {
        const u32 index = (id - PICA_REG_INDEX(gs.int_uniforms[0]));
        gs_setup.WriteUniformIntReg(index, regs.internal.gs.GetIntUniform(index));
        break;
    }

    case PICA_REG_INDEX(gs.uniform_setup.set_value[0]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[1]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[2]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[3]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[4]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[5]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[6]):
    case PICA_REG_INDEX(gs.uniform_setup.set_value[7]): {
        gs_setup.WriteUniformFloatReg(regs.internal.gs, value);
        break;
    }

    case PICA_REG_INDEX(gs.program.set_word[0]):
    case PICA_REG_INDEX(gs.program.set_word[1]):
    case PICA_REG_INDEX(gs.program.set_word[2]):
    case PICA_REG_INDEX(gs.program.set_word[3]):
    case PICA_REG_INDEX(gs.program.set_word[4]):
    case PICA_REG_INDEX(gs.program.set_word[5]):
    case PICA_REG_INDEX(gs.program.set_word[6]):
    case PICA_REG_INDEX(gs.program.set_word[7]): {
        u32& offset = regs.internal.gs.program.offset;
        if (offset >= 4096) {
            LOG_ERROR(HW_GPU, "Invalid GS program offset {}", offset);
        } else {
            gs_setup.UpdateProgramCode(offset, value);
            offset++;
        }
        break;
    }

    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[0]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[1]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[2]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[3]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[4]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[5]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[6]):
    case PICA_REG_INDEX(gs.swizzle_patterns.set_word[7]): {
        u32& offset = regs.internal.gs.swizzle_patterns.offset;
        if (offset >= gs_setup.GetSwizzleData().size()) {
            LOG_ERROR(HW_GPU, "Invalid GS swizzle pattern offset {}", offset);
        } else {
            gs_setup.UpdateSwizzleData(offset, value);
            offset++;
        }
        break;
    }

    case PICA_REG_INDEX(vs.output_mask):
        if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
            regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
            regs.internal.gs.output_mask.Assign(value);
        }
        break;

    case PICA_REG_INDEX(vs.bool_uniforms):
        vs_setup.WriteUniformBoolReg(regs.internal.vs.bool_uniforms.Value());
        if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
            regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
            gs_setup.WriteUniformBoolReg(regs.internal.vs.bool_uniforms.Value());
        }
        break;

    case PICA_REG_INDEX(vs.int_uniforms[0]):
    case PICA_REG_INDEX(vs.int_uniforms[1]):
    case PICA_REG_INDEX(vs.int_uniforms[2]):
    case PICA_REG_INDEX(vs.int_uniforms[3]): {
        const u32 index = (id - PICA_REG_INDEX(vs.int_uniforms[0]));
        vs_setup.WriteUniformIntReg(index, regs.internal.vs.GetIntUniform(index));
        if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
            regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
            gs_setup.WriteUniformIntReg(index, regs.internal.vs.GetIntUniform(index));
        }
        break;
    }

    case PICA_REG_INDEX(vs.uniform_setup.set_value[0]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[1]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[2]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[3]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[4]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[5]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[6]):
    case PICA_REG_INDEX(vs.uniform_setup.set_value[7]): {
        const auto index = vs_setup.WriteUniformFloatReg(regs.internal.vs, value);
        if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
            regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No && index) {
            gs_setup.uniforms.f[index.value()] = vs_setup.uniforms.f[index.value()];
        }
        break;
    }

    case PICA_REG_INDEX(vs.program.set_word[0]):
    case PICA_REG_INDEX(vs.program.set_word[1]):
    case PICA_REG_INDEX(vs.program.set_word[2]):
    case PICA_REG_INDEX(vs.program.set_word[3]):
    case PICA_REG_INDEX(vs.program.set_word[4]):
    case PICA_REG_INDEX(vs.program.set_word[5]):
    case PICA_REG_INDEX(vs.program.set_word[6]):
    case PICA_REG_INDEX(vs.program.set_word[7]): {
        u32& offset = regs.internal.vs.program.offset;
        if (offset >= 512) {
            LOG_ERROR(HW_GPU, "Invalid VS program offset {}", offset);
        } else {
            vs_setup.UpdateProgramCode(offset, value);
            if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
                regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
                gs_setup.UpdateProgramCode(offset, value);
            }
            offset++;
        }
        break;
    }

    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[0]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[1]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[2]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[3]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[4]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[5]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[6]):
    case PICA_REG_INDEX(vs.swizzle_patterns.set_word[7]): {
        u32& offset = regs.internal.vs.swizzle_patterns.offset;
        if (offset >= vs_setup.GetSwizzleData().size()) {
            LOG_ERROR(HW_GPU, "Invalid VS swizzle pattern offset {}", offset);
        } else {
            vs_setup.UpdateSwizzleData(offset, value);
            if (!regs.internal.pipeline.gs_unit_exclusive_configuration &&
                regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No) {
                gs_setup.UpdateSwizzleData(offset, value);
            }
            offset++;
        }
        break;
    }

    case PICA_REG_INDEX(lighting.lut_data[0]):
    case PICA_REG_INDEX(lighting.lut_data[1]):
    case PICA_REG_INDEX(lighting.lut_data[2]):
    case PICA_REG_INDEX(lighting.lut_data[3]):
    case PICA_REG_INDEX(lighting.lut_data[4]):
    case PICA_REG_INDEX(lighting.lut_data[5]):
    case PICA_REG_INDEX(lighting.lut_data[6]):
    case PICA_REG_INDEX(lighting.lut_data[7]): {
        auto& lut_config = regs.internal.lighting.lut_config;

        const u32 prev = std::exchange(lighting.luts[lut_config.type][lut_config.index].raw, value);
        lighting.lut_dirty |= (prev != value) << lut_config.type;
        lut_config.index.Assign(lut_config.index + 1);
        break;
    }

    case PICA_REG_INDEX(texturing.fog_lut_data[0]):
    case PICA_REG_INDEX(texturing.fog_lut_data[1]):
    case PICA_REG_INDEX(texturing.fog_lut_data[2]):
    case PICA_REG_INDEX(texturing.fog_lut_data[3]):
    case PICA_REG_INDEX(texturing.fog_lut_data[4]):
    case PICA_REG_INDEX(texturing.fog_lut_data[5]):
    case PICA_REG_INDEX(texturing.fog_lut_data[6]):
    case PICA_REG_INDEX(texturing.fog_lut_data[7]): {
        const u32 prev =
            std::exchange(fog.lut[regs.internal.texturing.fog_lut_offset % 128].raw, value);
        fog.lut_dirty |= prev != value;
        regs.internal.texturing.fog_lut_offset.Assign(regs.internal.texturing.fog_lut_offset + 1);
        break;
    }

    case PICA_REG_INDEX(texturing.proctex_lut_data[0]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[1]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[2]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[3]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[4]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[5]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[6]):
    case PICA_REG_INDEX(texturing.proctex_lut_data[7]): {
        auto& index = regs.internal.texturing.proctex_lut_config.index;
        const auto lut_table = regs.internal.texturing.proctex_lut_config.ref_table.Value();

        const auto sync_lut = [&](auto& proctex_table) {
            const u32 prev = std::exchange(proctex_table[index % proctex_table.size()].raw, value);
            proctex.table_dirty |= (prev != value) << u32(lut_table);
        };

        switch (lut_table) {
        case TexturingRegs::ProcTexLutTable::Noise:
            sync_lut(proctex.noise_table);
            break;
        case TexturingRegs::ProcTexLutTable::ColorMap:
            sync_lut(proctex.color_map_table);
            break;
        case TexturingRegs::ProcTexLutTable::AlphaMap:
            sync_lut(proctex.alpha_map_table);
            break;
        case TexturingRegs::ProcTexLutTable::Color:
            sync_lut(proctex.color_table);
            break;
        case TexturingRegs::ProcTexLutTable::ColorDiff:
            sync_lut(proctex.color_diff_table);
            break;
        }
        index.Assign(index + 1);
        break;
    }
    default:
        break;
    }
}

// Handle batch register writes
void PicaCore::WriteInternalRegBatch(u32 id, const u32* values, u32 count, u32 mask,
                                     bool& stop_requested) {
    if (id >= RegsInternal::NUM_REGS) [[unlikely]] {
        // Writes to OOB registers are no-op.
        LOG_DEBUG(HW_GPU,
                  "Commandlist tried to write to invalid register 0x{:03X} repeated 0x{:04X} times"
                  "(mask: {:X})",
                  id, count, mask);
        return;
    }

    delay_generator.AddCommands(count);
    const u32 write_mask = ExpandBitsToBytes[mask];
    // Only write the last value to the register array.
    // Batch handlers should take this in mind.
    regs.internal.reg_array[id] =
        (regs.internal.reg_array[id] & ~write_mask) | (values[count - 1] & write_mask);
    dirty_regs.Set(id);

    if (reg_impl_flags_lut[id].SupportsBatch()) {
        // If the register supports batch then call the handler.
        HandleSpecialRegBatch(id, values, count);
    } else if (reg_impl_flags_lut[id].NeedsSpecialHandling()) [[unlikely]] {
        // Unlikely as all special regs that make sense to use batch mode already
        // support batch handling.
        for (u32 i = 0; i < count && !stop_requested; ++i) {
            HandleSpecialReg(id, values[i], stop_requested);
        }
    }
}

// Handle sequential register writes.
void PicaCore::WriteInternalRegSequential(u32 id, const u32* __restrict values, u32 count, u32 mask,
                                          bool& stop_requested) {
    if (id + count > RegsInternal::NUM_REGS) [[unlikely]] {
        // Writes to OOB registers are no-op.
        LOG_DEBUG(
            HW_GPU,
            "Commandlist tried to write to invalid register range 0x{:03X}-0x{:03X} (mask: {:X})",
            id, id + count, mask);
        if (id >= RegsInternal::NUM_REGS) {
            return;
        } else {
            count = RegsInternal::NUM_REGS - id;
        }
    }
    const u32 write_mask = ExpandBitsToBytes[mask];
    u32* __restrict dst = &regs.internal.reg_array[id];
    u32 offset = 0;

    // This code is structured so that it uses the LUT to get the distance from the
    // register ID to the next special register. Then copies the range of normal registers
    // until it reaches the special register which is handled individually, and so on.
    while (offset < count) {
        if (stop_requested) [[unlikely]] {
            break;
        }
        const u32 reg = id + offset;
        // Distance to the next special register, capped by how many writes
        // are actually left in this write. If this register is special this is 0.
        const u32 batch_count =
            std::min<u32>(reg_impl_flags_lut[reg].RegsUntilSpecial(), count - offset);
        if (batch_count > 0) {
            delay_generator.AddCommands(batch_count);

            // Allows the compiler to auto-vectorize thanks to the __restrict keywords.
            // Verified in MSVC that vectorization is happening.
            u32* __restrict batch_dst = dst + offset;
            const u32* __restrict batch_src = values + offset;
            for (u32 i = 0; i < batch_count; ++i) {
                batch_dst[i] = (batch_dst[i] & ~write_mask) | (batch_src[i] & write_mask);
            }

            dirty_regs.SetRange(reg, batch_count);
            offset += batch_count;
        }
        // Whatever register stopped the batch (if we didn't reach the end)
        // needs individual handling, then resume batching after it.
        if (offset < count) {
            WriteInternalReg(id + offset, values[offset], mask, stop_requested);
            ++offset;
        }
    }
}

// Handle individual command write.
void PicaCore::WriteInternalReg(u32 id, u32 value, u32 mask, bool& stop_requested) {
    if (id >= RegsInternal::NUM_REGS) [[unlikely]] {
        // Writes to OOB registers are no-op.
        LOG_DEBUG(
            HW_GPU,
            "Commandlist tried to write to invalid register 0x{:03X} (value: {:08X}, mask: {:X})",
            id, value, mask);
        return;
    }

    delay_generator.AddCommands(1);

    // TODO: Figure out how register masking acts on e.g. vs.uniform_setup.set_value
    const u32 old_value = regs.internal.reg_array[id];
    const u32 write_mask = ExpandBitsToBytes[mask];
    regs.internal.reg_array[id] = (old_value & ~write_mask) | (value & write_mask);

    if (debug_context) [[unlikely]] {
        // Track register write.
        DebugUtils::OnPicaRegWrite(id, mask, regs.internal.reg_array[id]);
        // Track events.
        debug_context->OnEvent(DebugContext::Event::PicaCommandLoaded, &id);
    }

    if (reg_impl_flags_lut[id].NeedsSpecialHandling()) {
        HandleSpecialReg(id, value, stop_requested);
    }

    dirty_regs.Set(id);

    if (debug_context) [[unlikely]] {
        debug_context->OnEvent(DebugContext::Event::PicaCommandProcessed, &id);
    }
}

void PicaCore::SubmitImmediate(u32 value) {
    // Push to word to the queue. This returns true when a full attribute is formed.
    if (!immediate.queue.Push(value)) {
        return;
    }

    constexpr std::size_t IMMEDIATE_MODE_INDEX = 0xF;

    auto& setup = regs.internal.pipeline.vs_default_attributes_setup;
    if (setup.index > IMMEDIATE_MODE_INDEX) {
        LOG_ERROR(HW_GPU, "Invalid VS default attribute index {}", setup.index);
        return;
    }

    // Retrieve the attribute and place it in the default attribute buffer.
    const auto attribute = immediate.queue.Get();
    if (setup.index < IMMEDIATE_MODE_INDEX) {
        input_default_attributes[setup.index] = attribute;
        setup.index++;
        return;
    }

    // When index is 0xF the attribute is used for immediate mode drawing.
    immediate.input_vertex[immediate.current_attribute] = attribute;
    if (immediate.current_attribute < regs.internal.pipeline.max_input_attrib_index) {
        immediate.current_attribute++;
        return;
    }

    // We formed a vertex, flush.
    DrawImmediate();
}

void PicaCore::DrawImmediate() {
    // CodexAstraLocal: Immediate assembly consumes the same persistent state.
    ReconcileDeferredVertices();
    // CodexAstraLocal: Each immediate input starts its own compute ShaderUnit
    // lifetime, but shares the actual PICA primitive tail with array draws.
    // Only original command attributes cross this entry; no CPU shader runs.
    if (Settings::RequiresComputeOnly(Settings::values.uberhar_test_mode.GetValue())) {
        if (debug_context || regs.internal.pipeline.use_gs != PipelineRegs::UseGS::No ||
            primitive_assembler.GetTopology() != regs.internal.pipeline.triangle_topology)
            throw VideoCore::ShaderRecoveryError(
                "Calculated immediate input requires a matching topology without guest geometry");
        if (Core::System::GetInstance().HasConcurrentGuestMemoryWriters())
            throw VideoCore::ShaderRecoveryError(
                "Calculated immediate snapshot requires no concurrent guest memory writer");
        if (immediate.reset_geometry_pipeline) geometry_pipeline.Reconfigure();
        const auto before = primitive_assembler.ExportComputeState();
        ComputeAssemblyResult completed;
        if (!rasterizer->DrawComputeBatch(false, before, completed, &immediate.input_vertex) ||
            !primitive_assembler.CommitComputeState(completed))
            throw VideoCore::ShaderRecoveryError("Calculated immediate backend/state unavailable");
        immediate.reset_geometry_pipeline = false;
        immediate.current_attribute = 0;
        return;
    }
    // Compile the vertex shader.
    shader_engine->SetupBatch(vs_setup, regs.internal.vs.main_offset);

    // Track vertex in the debug recorder.
    if (debug_context) {
        debug_context->OnEvent(DebugContext::Event::VertexShaderInvocation,
                               std::addressof(immediate.input_vertex));
    }

    ShaderUnit shader_unit;
    AttributeBuffer output{};

    // Invoke the vertex shader for the vertex.
    shader_unit.LoadInput(regs.internal.vs, immediate.input_vertex);
    shader_engine->Run(vs_setup, shader_unit);
    shader_unit.WriteOutput(regs.internal.vs, output);

    // Reconfigure geometry pipeline if needed.
    if (immediate.reset_geometry_pipeline) {
        geometry_pipeline.Reconfigure();
        immediate.reset_geometry_pipeline = false;
    }

    // Send to geometry pipeline.
    ASSERT(!geometry_pipeline.NeedIndexInput());
    geometry_pipeline.Setup(shader_engine.get());
    geometry_pipeline.SubmitVertex(output);

    // Flush the immediate triangle.
    rasterizer->DrawTriangles();
    immediate.current_attribute = 0;

    if (debug_context) {
        debug_context->OnEvent(DebugContext::Event::FinishedPrimitiveBatch, nullptr);
    }
}

// AstraPro: Both entry points use the real assembler state. A topology mismatch,
// retained partial primitive or pending Shader winding always preserves CPU execution.
Vulkan::ReadyVertexPolicy::Admission PicaCore::GetReadyGpuVertexAdmission() const {
    return Vulkan::ReadyVertexPolicy::Classify(
        Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue()),
        static_cast<bool>(debug_context), primitive_assembler.IsEmpty(),
        regs.internal.pipeline.use_gs != PipelineRegs::UseGS::No,
        primitive_assembler.GetTopology(), regs.internal.pipeline.num_vertices,
        primitive_assembler.HasPendingWinding(),
        primitive_assembler.GetTopology() == regs.internal.pipeline.triangle_topology);
}

// AstraPro: Count completed CPU and GPU batches, not CPU batches alone. This
// reads the clock at most once per 4096 completed batches; no per-draw clock call.
void PicaCore::ReportVirtualVerticesIfDue() {
    // CodexAstraLocal: Deferred submissions retain the original bounded report
    // cadence even when they replace most synchronous CPU batches.
    if (((virtual_vertex_batches + ready_gpu_vertex_batches +
          (deferred_vertices ? deferred_vertices->submitted : 0)) & 4095) != 0)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now - virtual_window_start >= std::chrono::seconds{5})
        ReportVirtualVertices("progress", now);
}

void PicaCore::DrawArrays(bool is_indexed) {
    MICROPROFILE_SCOPE(GPU_Drawing);

    // Track vertex in the debug recorder.
    if (debug_context) {
        debug_context->OnEvent(DebugContext::Event::IncomingPrimitiveBatch, nullptr);
    }

    const bool accelerate_draw = [this] {
        // Geometry shaders cannot be accelerated due to register preservation.
        if (regs.internal.pipeline.use_gs == PipelineRegs::UseGS::Yes) {
            return false;
        }

        // TODO (wwylele): for Strip/Fan topology, if the primitive assember is not restarted
        // after this draw call, the buffered vertex from this draw should "leak" to the next
        // draw, in which case we should buffer the vertex into the software primitive assember,
        // or disable accelerate draw completely. However, there is not game found yet that does
        // this, so this is left unimplemented for now. Revisit this when an issue is found in
        // games.

        bool accelerate_draw = Settings::values.use_hw_shader && primitive_assembler.IsEmpty();
        const auto topology = primitive_assembler.GetTopology();
        if (topology == PipelineRegs::TriangleTopology::Shader ||
            topology == PipelineRegs::TriangleTopology::List) {
            accelerate_draw = accelerate_draw && (regs.internal.pipeline.num_vertices % 3) == 0;
        }
        return accelerate_draw;
    }();

    // Add vertices to the delay generator.
    delay_generator.AddVertices(regs.internal.pipeline.num_vertices,
                                regs.internal.pipeline.triangle_topology);

    // CodexAstraLocal: Independent compute owns the whole original-input draw.
    // Persistent List/Strip/Fan/Shader state is snapshotted and committed only
    // after whole-batch success. No refusal reaches LoadVertices or Native.
    if (Settings::RequiresComputeOnly(Settings::values.uberhar_test_mode.GetValue())) {
        ReconcileDeferredVertices();
        const auto topology = primitive_assembler.GetTopology();
        if (debug_context || regs.internal.pipeline.use_gs != PipelineRegs::UseGS::No ||
            topology != regs.internal.pipeline.triangle_topology)
            throw VideoCore::ShaderRecoveryError(
                "Calculated requires a matching persistent topology without guest geometry");
        if (Core::System::GetInstance().HasConcurrentGuestMemoryWriters())
            throw VideoCore::ShaderRecoveryError(
                "Calculated original input snapshot requires no concurrent guest memory writer");
        if (!regs.internal.pipeline.num_vertices) return;
        geometry_pipeline.Reconfigure();
        const auto before = primitive_assembler.ExportComputeState();
        ComputeAssemblyResult completed;
        if (!rasterizer->DrawComputeBatch(is_indexed, before, completed) ||
            !primitive_assembler.CommitComputeState(completed))
            throw VideoCore::ShaderRecoveryError("Calculated original-input backend unavailable");
        return;
    }

    // AstraPro: Combo can promote complete no-GS lists to already-ready GPU
    // vertices. A false return still executes the full CPU batch below. Never
    // pass debugger work, partial assembly, strip/fan tails or excessive uploads.
    // CodexAstraUlt: Replace AstraPro's Automatic-only gate with the same vertex policy for
    // both Combo presets; the diagnostic preset changes fragments, never vertex eligibility.
    const bool ready_gpu_mode =
        Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue());
    if (ready_gpu_mode) {
        // AstraPro: Explain zero coverage without guessing which topologies a game uses.
        const u32 topology =
            std::min<u32>(static_cast<u32>(primitive_assembler.GetTopology()), 4);
        ++ready_gpu_topologies[topology];
        const auto admission = GetReadyGpuVertexAdmission();
        static_assert(std::tuple_size_v<decltype(ready_gpu_admissions)> ==
                      static_cast<u32>(Vulkan::ReadyVertexPolicy::Admission::Count));
        ++ready_gpu_admissions[static_cast<u32>(admission)];
        if (Vulkan::ReadyVertexPolicy::IsEligible(admission)) {
            ++ready_gpu_vertex_attempts;
            // AstraPro: Attribute recurring host preparation separately from CPU
            // vertex execution. No per-draw timing and no inference of GPU time.
            const bool sample_attempt = (ready_gpu_vertex_attempts & 1023U) == 0;
            const auto attempt_start = sample_attempt ? std::chrono::steady_clock::now()
                                                     : std::chrono::steady_clock::time_point{};
            const bool promoted = rasterizer->AccelerateDrawBatchReady(is_indexed);
            if (sample_attempt) {
                const u64 ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - attempt_start).count();
                ++(promoted ? ready_gpu_host_success_samples : ready_gpu_host_fallback_samples);
                (promoted ? ready_gpu_host_success_ns : ready_gpu_host_fallback_ns) += ns;
                ready_gpu_host_max_ns = std::max(ready_gpu_host_max_ns, ns);
            }
            if (promoted) {
                ++ready_gpu_vertex_batches;
                ++ready_gpu_selected_topologies[topology];
                ready_gpu_vertex_inputs += regs.internal.pipeline.num_vertices;
                ReportVirtualVerticesIfDue();
                return;
            }
        }
    }

    // Attempt to use hardware vertex shaders if possible.
    if (accelerate_draw && rasterizer->AccelerateDrawBatch(is_indexed)) {
        return;
    }

    // AstraEH: Custom mode pays no clock-read cost for the virtual-PICA experiment.
    const bool virtual_test =
        Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom;
    const auto virtual_start =
        virtual_test ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    // AstraEH: Only an admitted no-GS batch enables the sparse post-vertex draw measurement.
    native_batch_sampled = false;

    // CodexAstraLocal: Only an independently published owned packet bypasses the
    // ordinary LoadVertices/DrawTriangles pair. Every refusal remains one serial
    // draw after earlier deferred assembler state has been reconciled.
    if (TryDeferredVertices(is_indexed, virtual_start))
        return;
    ReconcileDeferredVertices();

    // AstraEH: A bridge replaces a draw that already met the hardware path's
    // empty-assembler/no-GS contract. Isolate strip/fan expansion to preserve that
    // path's existing state semantics and allow the next ready GPU draw to resume.
    // This does not fix upstream's documented cross-draw strip/fan limitation.
    if (accelerate_draw && rasterizer->HasPreparedCpuVertexBridge()) {
        ASSERT(regs.internal.pipeline.use_gs == PipelineRegs::UseGS::No);
        primitive_assembler.RunIsolatedBatch([&] { LoadVertices(is_indexed, virtual_start); });
    } else {
        // Ordinary CPU rendering retains persistent assembly and partial primitives.
        LoadVertices(is_indexed, virtual_start);
    }

    if (virtual_test) {
        const auto now = std::chrono::steady_clock::now();
        const u64 elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - virtual_start).count();
        if (virtual_window_start == std::chrono::steady_clock::time_point{})
            virtual_window_start = virtual_start;
        ++virtual_vertex_batches;
        virtual_vertex_inputs += regs.internal.pipeline.num_vertices;
        virtual_vertex_ns += elapsed;
        virtual_vertex_max_ns = std::max(virtual_vertex_max_ns, elapsed);
        if (native_batch_sampled)
            native_samples.vertex_ns += elapsed;
    }

    // Draw emitted triangles.
    if (native_batch_sampled) {
        // AstraEH: Includes CPU renderer preparation and any waits reached by DrawTriangles;
        // this is not GPU execution time. Ordinary draws retain the untimed call below.
        const auto start = std::chrono::steady_clock::now();
        rasterizer->DrawTriangles();
        const u64 ns = NativeVertexSamples::Nanoseconds(start, std::chrono::steady_clock::now());
        native_samples.draw_ns += ns;
        native_samples.draw_max_ns = std::max(native_samples.draw_max_ns, ns);
    } else {
        rasterizer->DrawTriangles();
    }

    // AstraEH: Report only after the sampled draw is complete, so all sample denominators
    // match. Reuse the existing 4096-batch/five-second gate; no extra per-draw clock read.
    if (virtual_test)
        ReportVirtualVerticesIfDue();

    if (debug_context) {
        debug_context->OnEvent(DebugContext::Event::FinishedPrimitiveBatch, nullptr);
    }
}

// CodexAstraLocal: Only the owner publishes PICA state. Each packet is complete
// List/Shader work, so adopting its final buffered pair reproduces SubmitOrdered without
// re-emitting triangles. Keep asynchronous counts separate from synchronous stage
// timings rather than claiming the off-thread arithmetic cost disappeared.
void PicaCore::ReconcileDeferredVertices() {
    if (!deferred_vertices || deferred_vertices->pending.empty())
        return;
    auto& state = *deferred_vertices;
    ++state.synchronous_boundaries;
    state.executor.Drain();
    for (; state.reconciled < state.pending.size(); ++state.reconciled) {
        const auto& packet = state.pending[state.reconciled];
        // CodexAstraLocal: One completion lock supplies both owner consumers.
        const auto result = packet->CompletedResult();
        primitive_assembler.AdoptCompletedTriangleTail(result.last_pair);
        const auto counts = result.counts;
        ++state.completed;
        state.inputs += packet->VertexCount();
        state.invocations += counts.invocations;
        state.hits += counts.hits;
    }
    state.pending.clear();
    state.reconciled = 0;
}

bool PicaCore::TryDeferredVertices(bool is_indexed,
                                   std::chrono::steady_clock::time_point batch_start) {
    const auto mode = Settings::values.uberhar_test_mode.GetValue();
    const auto& pipeline = regs.internal.pipeline;
    // CodexAstraLocal: First admission targets small draws that the existing
    // per-draw pool runs serially. Host complete-path controls reject tiny/light
    // packets: require at least 96 inputs and four available logical processors;
    // Capture additionally checks actual FIFO misses, arithmetic and copy volume.
    // Preserve the >=256-input parallel route;
    // do not silently serialize one large draw inside a whole-packet worker.
    // A64 status isolation is required; other backends retain current execution.
    // Complete no-GS Shader topology shares the scalar List branch; a pending
    // winding request or topology mismatch always retains synchronous assembly.
    if ((mode != Settings::UberharTestMode::Native && !Settings::UsesReadyGpuVertices(mode)) ||
        deferred_allocation_failed || debug_context || vertex_timing ||
        pipeline.use_gs != PipelineRegs::UseGS::No || pipeline.num_vertices < 96 ||
        pipeline.num_vertices % 3 || pipeline.num_vertices >= 256 ||
        (primitive_assembler.GetTopology() != PipelineRegs::TriangleTopology::List &&
         primitive_assembler.GetTopology() != PipelineRegs::TriangleTopology::Shader) ||
        primitive_assembler.GetTopology() != pipeline.triangle_topology ||
        !primitive_assembler.IsEmpty() || primitive_assembler.HasPendingWinding() ||
        rasterizer->HasPreparedCpuVertexBridge() ||
        native_sample_budget.Due(batch_start, pipeline.num_vertices) ||
        !shader_engine->SupportsObservableVertexContract() ||
        !Core::System::GetInstance().IsHostFpStatusIsolated() ||
        Core::System::GetInstance().HasConcurrentGuestMemoryWriters() ||
        !Common::Uberhar::ParallelFloatEnvironment::AllowsParallel())
        return false;
    // CodexAstraLocal: Avoid an affinity syscall for every small draw. The pool
    // uses the title's initial process allowance; OS scheduling handles core/SMT
    // placement without a Thor-specific count or affinity modification.
    if (!deferred_processors)
        deferred_processors = Common::Uberhar::AvailableProcessors();
    if (deferred_processors < 4)
        return false;
    const auto writer = rasterizer->PrepareDeferredVertices(pipeline.num_vertices);
    if (!writer)
        return false;
    bool published_current = false; // CodexAstraLocal: Only this draw determines replay safety.
    try {
        if (!deferred_vertices) {
            try {
                deferred_vertices = std::make_unique<DeferredVertexState>(deferred_processors);
            } catch (const std::system_error&) {
                // CodexAstraLocal: No task exists during thread startup failure;
                // disable the optional route and retain ordinary synchronous work.
                deferred_allocation_failed = true;
                return false;
            }
        }
        auto& state = *deferred_vertices;
        if (state.pending.size() == DeferredVertexState::PacketLimit) {
            ++state.capacity_drains;
            ReconcileDeferredVertices();
            rasterizer->DrainDeferredCommands();
        }
        // CodexAstraLocal: Preserve the original invalid index/base early return
        // by declining before capture, including its non-indexed address check.
        const PAddr base = pipeline.vertex_attributes.GetPhysicalBaseAddress();
        const u64 index_address = u64(base) + pipeline.index_array.offset;
        if (index_address > std::numeric_limits<u32>::max())
            return false;
        const auto indices = memory.GetPhysicalRef(static_cast<PAddr>(index_address));
        if (!indices.GetPtr())
            return false;
        const u32 width = pipeline.index_array.format ? 2 : 1;
        if (is_indexed && u64(pipeline.num_vertices) * width > indices.GetSize())
            return false;
        const VertexLoader loader{memory, pipeline};
        const u32 requested = regs.internal.vs.max_input_attribute_index + 1;
        if (requested > 16 || requested > static_cast<u32>(loader.GetNumTotalAttributes()))
            return false;
        regs.internal.rasterizer.ValidateSemantics();
        shader_engine->SetupBatch(vs_setup, regs.internal.vs.main_offset);
        // CodexAstraLocal: Preserve the original no-GS geometry backend reset
        // and its serialized state before choosing the asynchronous CPU route.
        geometry_pipeline.Reconfigure();
        geometry_pipeline.Setup(shader_engine.get());
        auto lease = shader_engine->LeaseForDraw(vs_setup);
        if (!lease)
            return false;
        CpuDrawCapture source;
        source.shader_lease = std::move(lease);
        source.shader = regs.internal.vs;
        source.rasterizer = regs.internal.rasterizer;
        source.uniforms = vs_setup.uniforms;
        source.defaults = input_default_attributes;
        source.available_attributes = loader.GetNumTotalAttributes();
        source.base_address = base;
        source.index_bytes = {indices.GetPtr(), indices.GetSize()};
        source.index_width = width;
        source.count = pipeline.num_vertices;
        source.base_vertex = pipeline.vertex_offset;
        source.indexed = is_indexed;
        // CodexAstraLocal: Memory references last only through bounded raw-byte
        // capture. No worker can later observe a guest write or cache readback.
        std::array<MemoryRef, 16> refs;
        for (u32 attribute = 0; attribute < requested; ++attribute) {
            auto description = loader.DescribeNativeInput(attribute);
            source.attributes[attribute] = description;
            if (description.is_default)
                continue;
            const u64 address = u64(base) + description.offset;
            if (address > std::numeric_limits<u32>::max())
                return false;
            auto& ref = refs[attribute];
            ref = memory.GetPhysicalRef(static_cast<PAddr>(address));
            if (!ref.GetPtr())
                return false;
            source.bindings[source.binding_count++] = {
                static_cast<PAddr>(address), {ref.GetPtr(), ref.GetSize()}};
        }
        auto packet = state.executor.Capture(source, writer);
        if (!packet) {
            // CodexAstraLocal: A refused packet was never published. Drain old
            // command captures only for real capacity pressure; unsupported
            // shader/input shapes must not add an unnecessary Vulkan-worker wait.
            if (state.executor.LastCaptureAtCapacity()) {
                ++state.capacity_drains;
                ReconcileDeferredVertices();
                rasterizer->DrainDeferredCommands();
            }
            return false;
        }
        if (!state.executor.Submit(packet))
            return false; // CodexAstraLocal: No work was published on refusal.
        published_current = true;
        state.pending.push_back(packet); // Reserved fixed capacity before Submit.
        if (!rasterizer->DrawDeferredVertices(packet))
            throw VideoCore::ShaderRecoveryError{};
        const auto elapsed = NativeVertexSamples::Nanoseconds(batch_start,
                                                              std::chrono::steady_clock::now());
        state.capture_submit_ns += elapsed;
        state.capture_submit_max_ns = std::max(state.capture_submit_max_ns, elapsed);
        ++state.submitted;
        if (virtual_window_start == std::chrono::steady_clock::time_point{})
            virtual_window_start = batch_start;
        ReportVirtualVerticesIfDue();
        return true;
    } catch (const std::bad_alloc&) {
        // CodexAstraLocal: Only pre-publication allocation failures may recover
        // by shading serially; published failures must stay terminal, not replay.
        deferred_allocation_failed = true;
        if (published_current)
            throw VideoCore::ShaderRecoveryError{};
        return false;
    }
}

void PicaCore::LoadVertices(bool is_indexed, std::chrono::steady_clock::time_point batch_start) {
    // CodexAstraLocal: Reuse the existing batch wall timestamp; a terminal or
    // absent owner performs no new clock or phase reads on the ordinary path.
    if (vertex_timing && vertex_timing->Polling()) {
        if (Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue())) {
            const u64 now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                batch_start.time_since_epoch()).count();
            vertex_timing->Poll(now, Common::UberharActivity::Capture());
        } else {
            vertex_timing->PolicyChanged();
        }
    }
    // Read and validate vertex information from the loaders
    const auto& pipeline = regs.internal.pipeline;
    const PAddr base_address = pipeline.vertex_attributes.GetPhysicalBaseAddress();
    const auto loader = VertexLoader(memory, pipeline);
    regs.internal.rasterizer.ValidateSemantics();

    // Locate index buffer.
    const auto& index_info = pipeline.index_array;
    const u8* index_address_8 = memory.GetPhysicalPointer(base_address + index_info.offset);
    if (index_address_8 == nullptr) {
        // Mario & Luigi: Superstar Saga sets an invalid base address
        // for the vertex attributes. Return early if that is the case.
        // CodexAstraLocal: Retain this preexisting invalid-input return in
        // diagnostic coverage; it never creates a timed chunk.
        if (vertex_timing) vertex_timing->Unsupported();
        return;
    }
    const u16* index_address_16 = reinterpret_cast<const u16*>(index_address_8);
    const bool index_u16 = index_info.format != 0;

    // Compile the vertex shader for this batch.
    ShaderUnit shader_unit;
    shader_engine->SetupBatch(vs_setup, regs.internal.vs.main_offset);

    // Setup geometry pipeline in case we are using a geometry shader.
    geometry_pipeline.Reconfigure();
    geometry_pipeline.Setup(shader_engine.get());
    ASSERT(!geometry_pipeline.NeedIndexInput() || is_indexed);

    // AstraEH: Experimental no-GS draws can reuse final rasterizer vertices. Keep
    // Custom, debugger and geometry-shader execution on their established path.
    const bool virtual_test =
        Settings::values.uberhar_test_mode.GetValue() != Settings::UberharTestMode::Custom;
    if (virtual_test && !debug_context && pipeline.use_gs == PipelineRegs::UseGS::No) {
        // AstraPro: Refresh on exact mapping changes; live inputs and uniforms
        // are still loaded and shaded for every FIFO miss in this draw.
        const auto& plan = native_plan_cache.Get(regs.internal.vs, regs.internal.rasterizer);
        if (plan.Supported()) {
            // AstraEH: A conservative index-domain bound avoids a second index scan.
            // Pin each guest span for this draw only; range uncertainty retains the
            // original loader. Defaults, geometry draws and partial assembly keep
            // their existing semantics. No transformed vertices persist across draws.
            NativeVertexInputPlan input_plan;
            std::array<MemoryRef, 16> input_refs;
            u32 input_ref_count = 0;
            const u64 maximum_vertex = is_indexed ? (index_u16 ? 65535 : 255)
                : static_cast<u64>(pipeline.vertex_offset) +
                    (pipeline.num_vertices ? pipeline.num_vertices - 1 : 0);
            const auto prepare_input = [&](u64 maximum) {
                input_ref_count = 0;
                return input_plan.Prepare(
                    regs.internal.vs, loader.GetNumTotalAttributes(), base_address, maximum,
                    [&](u32 attribute) { return loader.DescribeNativeInput(attribute); },
                    [&](PAddr address) -> std::span<const u8> {
                        auto& ref = input_refs[input_ref_count++];
                        ref = memory.GetPhysicalRef(address);
                        return {ref.GetPtr(), ref.GetSize()};
                    },
                    // CodexAstraLocal: Whole recipes remain draw-local and are
                    // reselected after each range retry; empty draws avoid selection.
                    pipeline.num_vertices != 0);
            };
            auto input_result = prepare_input(maximum_vertex);
            bool rescued_input = false;
            // AstraPro: A full 65535-index domain need not fit when the draw only
            // references a smaller mesh. Retry only the range-related failures;
            // validate and pin the entire scanned index span before reading it.
            if (is_indexed && (input_result == NativeVertexInputPlan::Result::ShortMapping ||
                               input_result == NativeVertexInputPlan::Result::AddressWrap)) {
                ++native_index_retries;
                const u64 index_base = static_cast<u64>(base_address) + index_info.offset;
                if (index_base <= std::numeric_limits<u32>::max()) {
                    const auto indices = memory.GetPhysicalRef(static_cast<PAddr>(index_base));
                    const auto maximum = NativeIndexMaximum(
                        {indices.GetPtr(), indices.GetSize()}, pipeline.num_vertices, index_u16);
                    if (maximum) {
                        native_scanned_indices += pipeline.num_vertices;
                        input_result = prepare_input(*maximum);
                        rescued_input = input_result == NativeVertexInputPlan::Result::Ready;
                        native_index_rescues += rescued_input;
                    }
                }
            }
            ++native_input_results[static_cast<std::size_t>(input_result)];
            native_input_maps += input_plan.MappedAttributes();
            const auto vertex_at = [&](u32 index) -> u32 {
                return is_indexed ? (index_u16 ? index_address_16[index] : index_address_8[index])
                                  : index + pipeline.vertex_offset;
            };
            // AstraPro: A changed live index outside the rescanned bound retains
            // legacy decoding; it must never escape a pinned attribute span.
            u64 escaped_input_vertices = 0;
            // AstraEH: Compile out per-vertex clocks from ordinary batches. A sampled
            // vertex partitions input loading, shader execution and output conversion.
            // CodexAstraLocal: Share input/output source while specializing only
            // the ordinary call policy. No FIFO, state-carry or transport behavior changes.
            const auto shade_with_run = [&]<bool Sample>(u32 vertex, u32 index,
                                                         const auto& run_shader) {
                using Clock = NativeVertexSamples::Clock;
                Clock::time_point start, loaded, shaded;
                if constexpr (Sample)
                    start = Clock::now();
                if (input_plan.CanLoad(vertex)) {
                    input_plan.Load(shader_unit, input_default_attributes, vertex);
                } else {
                    escaped_input_vertices += input_plan.Ready();
                    AttributeBuffer input;
                    loader.LoadVertex(base_address, index, vertex, input, input_default_attributes);
                    plan.LoadInput(shader_unit, input);
                }
                if constexpr (Sample)
                    loaded = Clock::now();
                run_shader(shader_unit);
                if constexpr (Sample)
                    shaded = Clock::now();
                auto output = plan.Convert(shader_unit);
                if constexpr (Sample) {
                    const auto converted = Clock::now();
                    native_samples.input_ns += NativeVertexSamples::Nanoseconds(start, loaded);
                    native_samples.shader_ns += NativeVertexSamples::Nanoseconds(loaded, shaded);
                    native_samples.output_ns += NativeVertexSamples::Nanoseconds(shaded, converted);
                }
                return output;
            };
            // AstraEH: Construct the callback once per draw, retaining the same assembler
            // and triangle sink used by geometry output, including cross-draw tails.
            const PrimitiveAssembler::TriangleHandler triangle =
                [this](const OutputVertex& a, const OutputVertex& b, const OutputVertex& c) {
                    rasterizer->AddTriangle(a, b, c);
                };
            const auto submit = [&](const OutputVertex& output) {
                primitive_assembler.SubmitVertex(output, triangle);
            };
            // CodexAstraLocal: Resolve only positive, eligible draws after the
            // existing SetupBatch. Contexts borrow code and live uniforms strictly
            // within this synchronous draw; fallback preserves inherited Run.
            const auto context = pipeline.num_vertices
                ? shader_engine->BindForDraw(vs_setup) : ShaderRunContext{};
            const auto run_inherited = [&](ShaderUnit& state) {
                shader_engine->Run(vs_setup, state);
            };
            const auto run_prepared = [&](ShaderUnit& state) { context.Run(state); };
            const auto inherited_shade = [&]<bool Sample>(u32 vertex, u32 index) {
                return shade_with_run.template operator()<Sample>(vertex, index, run_inherited);
            };
            const auto prepared_shade = [&]<bool Sample>(u32 vertex, u32 index) {
                return shade_with_run.template operator()<Sample>(vertex, index, run_prepared);
            };
            NativeVertexCounts counts;
            const u64 samples = native_samples.misses + native_samples.hits;
            native_batch_sampled = native_sample_budget.Admit(batch_start, pipeline.num_vertices);
            std::optional<VertexTiming::Range> timing_range;
            if (vertex_timing && vertex_timing->Polling()) {
                timing_range = vertex_timing->Select({vs_setup.GetProgramCodeHash(),
                    vs_setup.GetSwizzleDataHash(), regs.internal.vs.main_offset,
                    pipeline.num_vertices, static_cast<u32>(regs.internal.vs.max_input_attribute_index) + 1,
                    static_cast<u32>(primitive_assembler.GetTopology()), is_indexed, input_plan.Ready()});
            }
            // CodexAstraLocal: Instrumented paths share one compiled loop via
            // an allocation-free adapter selected once per draw. They execute
            // the same prepared call when available, retaining live state. Their
            // shader wall spans include this extra adapter call; never subtract
            // that cost or treat sparse timings as uninstrumented workload shares.
            using DiagnosticCall = void (*)(const ShaderEngine&, const ShaderSetup&,
                                             const ShaderRunContext&, ShaderUnit&);
            const DiagnosticCall diagnostic_call = context
                ? +[](const ShaderEngine&, const ShaderSetup&, const ShaderRunContext& call,
                      ShaderUnit& state) { call.Run(state); }
                : +[](const ShaderEngine& engine, const ShaderSetup& setup,
                      const ShaderRunContext&, ShaderUnit& state) { engine.Run(setup, state); };
            const auto run_diagnostic = [&](ShaderUnit& state) {
                diagnostic_call(*shader_engine, vs_setup, context, state);
            };
            const auto diagnostic_shade = [&]<bool Sample>(u32 vertex, u32 index) {
                return shade_with_run.template operator()<Sample>(vertex, index, run_diagnostic);
            };
            // CodexAstraLocal: Parallelize only established Native/Combo no-GS
            // prepared-input draws. Calculated, debugger, geometry and timing
            // observers keep their existing paths. Every refusal still runs the
            // complete serial shader; no guest draw or input is dropped.
            // CodexAstraLocal: Retain one reason until the existing serial runner
            // returns its actual miss count. Unattempted modes/allocation-disabled
            // paths stay outside this accounting, and exceptions count no completion.
            enum class SerialReason { Other, Small, Carry };
            SerialReason serial_reason = SerialReason::Other;
            ParallelVertexState* attempted_parallel = nullptr;
            const auto try_parallel = [&]() {
                const auto mode = Settings::values.uberhar_test_mode.GetValue();
                if (mode != Settings::UberharTestMode::Native &&
                    !Settings::UsesReadyGpuVertices(mode))
                    return false;
                if (parallel_allocation_failed)
                    return false;
                try {
                    if (!parallel_vertices)
                        parallel_vertices = std::make_unique<ParallelVertexState>();
                } catch (const std::bad_alloc&) {
                    parallel_allocation_failed = true;
                    return false;
                }
                auto& parallel = *parallel_vertices;
                attempted_parallel = &parallel;
                ++parallel.checks;
                if (timing_range || native_batch_sampled) { ++parallel.observer; return false; }
                if (pipeline.num_vertices < 256) {
                    ++parallel.small;
                    serial_reason = SerialReason::Small;
                    return false;
                }
                if (!context) { ++parallel.context_missing; return false; }
                // CodexAstraLocal: Shader independence alone does not make live
                // vertex memory immutable. Software triangle submission can
                // overwrite later inputs through framebuffer aliasing; query
                // the actual sink before planning/shading ahead of submission.
                if (!rasterizer->DefersGuestMemoryWritesUntilDraw()) {
                    ++parallel.submission_writes;
                    return false;
                }
                if (parallel.batch.Available() < 2) { ++parallel.single_core; return false; }
                // CodexAstraLocal: The guest CPU/GSP command path is synchronous,
                // but threaded DSP/RPC can write guest spans independently. Check
                // constructed engines, not settings that may have since changed.
                if (Core::System::GetInstance().HasConcurrentGuestMemoryWriters()) {
                    ++parallel.concurrent_memory;
                    return false;
                }
                // CodexAstraLocal: Enabled host FP traps retain serial fault
                // ordering; worker sticky-flag merging alone cannot preserve it.
                if (!Common::Uberhar::ParallelFloatEnvironment::AllowsParallel()) {
                    ++parallel.float_traps;
                    return false;
                }
                // CodexAstraLocal: Keep exact-index rescues on their established
                // per-input escape check. This path requires the entire index
                // domain to fit, so no worker may invoke the legacy memory loader.
                if (maximum_vertex > std::numeric_limits<u32>::max() ||
                    !input_plan.CanLoad(static_cast<u32>(maximum_vertex))) {
                    ++parallel.input_bounds;
                    return false;
                }
                // CodexAstraLocal: Validate and pin the full index span before
                // parallel planning. The owner retains every memory reference,
                // code and uniform until synchronous workers have joined.
                MemoryRef parallel_indices;
                if (is_indexed) {
                    const u64 address = static_cast<u64>(base_address) + index_info.offset;
                    if (address > std::numeric_limits<u32>::max()) {
                        ++parallel.input_bounds;
                        return false;
                    }
                    parallel_indices = memory.GetPhysicalRef(static_cast<PAddr>(address));
                    if (!parallel_indices.GetPtr() ||
                        static_cast<u64>(pipeline.num_vertices) * (index_u16 ? 2 : 1) >
                            parallel_indices.GetSize()) {
                        ++parallel.input_bounds;
                        return false;
                    }
                }
                // CodexAstraLocal: Only the constructed, separately audited A64
                // CPU and shader engines opt into selected-output independence.
                // Existing no-GS/observer/trap/memory guards remain mandatory;
                // arbitrary backends keep the original arithmetic-read proof.
                const auto contract = shader_engine->SupportsObservableVertexContract() &&
                                      Core::System::GetInstance().IsHostFpStatusIsolated()
                    ? ParallelVertexContract::SelectedOutputValues
                    : ParallelVertexContract::FullArithmeticReads;
                try {
                    const auto& proof = parallel.Get(vs_setup, regs.internal.vs.output_mask, contract);
                    ++parallel.admissions[static_cast<std::size_t>(proof.status)];
                    if (!proof.Supported()) {
                        // CodexAstraLocal: Weight all live carry refusals under
                        // either contract, including selected-output dependence.
                        if (proof.status == ParallelVertexStatus::TemporaryCarry ||
                            proof.status == ParallelVertexStatus::AddressCarry ||
                            proof.status == ParallelVertexStatus::ConditionCarry ||
                            proof.status == ParallelVertexStatus::OutputCarry)
                            serial_reason = SerialReason::Carry;
                        return false;
                    }
                    if (!parallel.batch.Prepare(pipeline.num_vertices)) {
                        ++parallel.allocation_failures;
                        parallel_allocation_failed = true;
                        return false;
                    }
                } catch (const std::bad_alloc&) {
                    ++parallel.allocation_failures;
                    parallel_allocation_failed = true;
                    return false;
                }
                // CodexAstraLocal: Each grain starts from the same zeroed draw
                // state, loads immutable prepared inputs and runs unchanged JIT
                // arithmetic. The selected-output proof permits only unused
                // state/status differences; converted vertex bytes stay exact.
                // Only the owner touches FIFO discovery and primitive assembly.
                // CodexAstraLocal: Sample every chunk of selected admitted draws without
                // disabling their workers or adding per-vertex timing hooks.
                const bool measure_parallel = parallel.parallel_batches % 257 == 0;
                counts = parallel.batch.Run(pipeline.num_vertices, is_indexed, vertex_at,
                    [&](std::span<const NativeParallelBatch::Invocation> inputs,
                        std::span<OutputVertex> outputs) {
                        ShaderUnit independent;
                        for (u32 i = 0; i < inputs.size(); ++i) {
                            input_plan.Load(independent, input_default_attributes, inputs[i].vertex);
                            context.Run(independent);
                            outputs[i] = plan.Convert(independent);
                        }
                    }, [&](u32 size, const auto& get) {
                        // CodexAstraLocal: The admitted accelerated sink only
                        // appends owned vertices: it neither observes/reenters
                        // this assembler nor retains these borrowed references.
                        primitive_assembler.SubmitOrdered(size, get, triangle);
                    }, measure_parallel);
                const auto& work = parallel.batch.LastWork();
                ++parallel.parallel_batches;
                parallel.owner_invocations += work.owner_invocations;
                parallel.worker_invocations += work.worker_invocations;
                if (measure_parallel) {
                    ++parallel.sampled_draws;
                    parallel.sampled_inputs += work.sampled_inputs;
                    parallel.sampled_invocations += work.sampled_invocations;
                    parallel.sampled_chunks += work.sampled_chunks;
                    parallel.plan_ns += work.plan_ns;
                    parallel.pool_ns += work.pool_ns;
                    parallel.owner_process_ns += work.owner_process_ns;
                    parallel.join_ns += work.join_ns;
                    parallel.submit_ns += work.submit_ns;
                }
                // CodexAstraLocal: Count completed work once, outside the vertex
                // loop; owner-only accepted chunks are still explicitly visible.
                if (contract == ParallelVertexContract::SelectedOutputValues) {
                    ++parallel.observable_batches;
                    parallel.observable_owner_invocations += work.owner_invocations;
                    parallel.observable_worker_invocations += work.worker_invocations;
                }
                parallel.chunks += work.chunks;
                parallel.peak_threads = std::max(parallel.peak_threads, work.peak_threads);
                return true;
            };
            const bool parallel_completed = try_parallel();
            if (parallel_completed) {
                // CodexAstraLocal: Counts below consume the same actual FIFO
                // misses/hits; completed parallel output must never execute twice.
            } else if (timing_range) {
                // CodexAstraLocal: Preserve the old sparse-admission cadence but
                // suppress its whole batch consistently in boundary/detail modes.
                // No old setup/vertex/draw counters receive partial new samples.
                vertex_timing->SuppressSparse(native_batch_sampled);
                native_batch_sampled = false;
                // CodexAstraLocal: Only the explicit diagnostic route exposes
                // separate callbacks. Both routes retain the same input plans.
                // CodexAstraLocal: The shader callback now uses the prepared or
                // inherited adapter selected for this draw; its overhead is measured.
                const auto load_input = [&](u32 vertex, u32 index) {
                    const bool fused = input_plan.CanLoad(vertex);
                    if (fused) {
                        input_plan.Load(shader_unit, input_default_attributes, vertex);
                    } else {
                        escaped_input_vertices += input_plan.Ready();
                        AttributeBuffer input;
                        loader.LoadVertex(base_address, index, vertex, input, input_default_attributes);
                        plan.LoadInput(shader_unit, input);
                    }
                    return fused;
                };
                counts = RunTimedNativeVertexBatch(pipeline.num_vertices, is_indexed, vertex_at,
                    load_input, [&] { run_diagnostic(shader_unit); },
                    [&] { return plan.Convert(shader_unit); }, submit, *vertex_timing, *timing_range);
            } else if (native_batch_sampled) {
                // AstraEH: Include loader/JIT/map setup before the loop without timing every draw.
                native_samples.setup_ns +=
                    NativeVertexSamples::Nanoseconds(batch_start, std::chrono::steady_clock::now());
                // AstraEH: Rotate the selected input; never time only the first cache miss.
                const u32 sample_index =
                    (static_cast<u32>(samples + 1) * 2654435761U) % pipeline.num_vertices;
                counts = RunNativeVertexBatch<true>(pipeline.num_vertices, is_indexed, vertex_at,
                                                    diagnostic_shade, submit, native_samples, sample_index);
                ++native_samples.batches;
                native_samples.batch_inputs += pipeline.num_vertices;
                native_samples.batch_invocations += counts.invocations;
            } else if (context) {
                // CodexAstraLocal: Ordinary loops have no adapter or per-miss
                // policy branch; choose the complete FIFO specialization once.
                counts = RunNativeVertexBatch<false>(pipeline.num_vertices, is_indexed, vertex_at,
                                                     prepared_shade, submit, native_samples);
            } else {
                counts = RunNativeVertexBatch<false>(pipeline.num_vertices, is_indexed, vertex_at,
                                                     inherited_shade, submit, native_samples);
            }
            // CodexAstraLocal: Consume the completed count once after rendering.
            // Certified batches already account for owner/worker work, including
            // accepted batches below the separate actual-miss parallel cutoff.
            if (!parallel_completed && attempted_parallel) {
                auto& serial_count = serial_reason == SerialReason::Small
                    ? attempted_parallel->serial_small_invocations
                    : serial_reason == SerialReason::Carry
                        ? attempted_parallel->serial_carry_invocations
                        : attempted_parallel->serial_other_invocations;
                serial_count += counts.invocations;
            }
            // AstraPro: Separate recovered invocation coverage from batch counts.
            if (rescued_input)
                native_rescued_vertices += counts.invocations - escaped_input_vertices;
            native_index_escapes += escaped_input_vertices;
            // AstraEH: Count actual misses using each transport, not all submitted indices.
            if (input_plan.Ready()) {
                // CodexAstraLocal: Reuse the completed FIFO count and existing
                // escape count once; descriptor recognition and reused indices
                // are not executed input transport. No per-vertex accounting is added.
                const u64 prepared_invocations = counts.invocations - escaped_input_vertices;
                native_input_fused_vertices += prepared_invocations;
                native_input_recipe_invocations[input_plan.RecipeSlot()] += prepared_invocations;
                native_input_legacy_vertices += escaped_input_vertices;
            } else {
                native_input_legacy_vertices += counts.invocations;
            }
            ++native_vertex_batches;
            native_vertex_inputs += pipeline.num_vertices;
            native_vertex_conversions += counts.invocations;
            native_vertex_reuses += counts.hits;
            virtual_vertex_invocations += counts.invocations;
            virtual_vertex_hits += counts.hits;
            return;
        }
        ++native_mapping_fallbacks;
    } else if (virtual_test) {
        if (debug_context)
            ++native_debug_fallbacks;
        else
            ++native_geometry_fallbacks;
    }

    // CodexAstraLocal: Recovery/geometry draws remain rendered below but
    // are outside the finite no-GS timing cohort.
    if (vertex_timing) vertex_timing->Unsupported();

    // AstraEH: Preserve the original attribute cache and geometry pipeline for recovery.
    VertexCacheIndex vertex_index;
    std::array<AttributeBuffer, VertexCacheIndex::Capacity> vertex_cache;
    AttributeBuffer vs_output;
    u64 invocations = 0, cache_hits = 0;

    for (u32 index = 0; index < pipeline.num_vertices; ++index) {
        // Indexed rendering doesn't use the start offset
        const u32 vertex = is_indexed
                               ? (index_u16 ? index_address_16[index] : index_address_8[index])
                               : (index + pipeline.vertex_offset);

        bool vertex_cache_hit = false;
        if (is_indexed) {
            if (geometry_pipeline.NeedIndexInput()) {
                geometry_pipeline.SubmitIndex(vertex);
                continue;
            }

            if (const int slot = vertex_index.Find(static_cast<u16>(vertex)); slot >= 0) {
                vs_output = vertex_cache[slot];
                vertex_cache_hit = true;
                ++cache_hits;
            }
        }

        if (!vertex_cache_hit) {
            // Initialize data for the current vertex
            AttributeBuffer input;
            loader.LoadVertex(base_address, index, vertex, input, input_default_attributes);

            // Record vertex processing to the debugger.
            if (debug_context) {
                debug_context->OnEvent(DebugContext::Event::VertexShaderInvocation,
                                       std::addressof(input));
            }

            // Invoke the vertex shader for this vertex.
            shader_unit.LoadInput(regs.internal.vs, input);
            shader_engine->Run(vs_setup, shader_unit);
            shader_unit.WriteOutput(regs.internal.vs, vs_output);

            ++invocations;
            // AstraEH: Insert only on a miss; hits never move the FIFO cursor.
            if (is_indexed)
                vertex_cache[vertex_index.Insert(static_cast<u16>(vertex))] = vs_output;
        }

        // Send to geometry pipeline
        geometry_pipeline.SubmitVertex(vs_output);
    }
    // AstraEH: Count actual shader runs separately from indexed inputs; no per-vertex clock reads.
    if (virtual_test) {
        virtual_vertex_invocations += invocations;
        virtual_vertex_hits += cache_hits;
    }
}

PicaCore::RenderPropertiesGuess PicaCore::GuessCmdRenderProperties(PAddr list, u32 size) {
    // Initialize command list tracking.
    const u8* head = memory.GetPhysicalPointer(list);
    cmd_list.Reset(list, head, size);

    constexpr size_t max_iterations = 0x100;

    RenderPropertiesGuess find_info{};

    find_info.vp_height = regs.internal.rasterizer.viewport_size_y.Value();
    find_info.paddr = regs.internal.framebuffer.framebuffer.color_buffer_address.Value() * 8;

    auto process_write = [this, &find_info](u32 cmd_id, u32 value) {
        switch (cmd_id) {
        case PICA_REG_INDEX(rasterizer.viewport_size_y):
            find_info.vp_height = value;
            find_info.vp_heigh_found = true;
            break;
        case PICA_REG_INDEX(framebuffer.framebuffer.color_buffer_address):
            find_info.paddr = value * 8;
            find_info.paddr_found = true;
            break;
        [[unlikely]] case PICA_REG_INDEX(pipeline.command_buffer.trigger[0]):
        [[unlikely]] case PICA_REG_INDEX(pipeline.command_buffer.trigger[1]): {
            const u32 index =
                static_cast<u32>(cmd_id - PICA_REG_INDEX(pipeline.command_buffer.trigger[0]));
            const PAddr addr = regs.internal.pipeline.command_buffer.GetPhysicalAddress(index);
            const u32 size = regs.internal.pipeline.command_buffer.GetSize(index);
            const u8* head = memory.GetPhysicalPointer(addr);
            cmd_list.Reset(addr, head, size);
            break;
        }
        default:
            break;
        }
        return find_info.vp_heigh_found && find_info.paddr_found;
    };

    size_t iterations = 0;
    while (cmd_list.current_index < cmd_list.length && iterations < max_iterations) {
        // Align read pointer to 8 bytes
        if (cmd_list.current_index % 2 != 0) {
            cmd_list.current_index++;
        }

        // Read the header and the value to write.
        const u32 value = cmd_list.head[cmd_list.current_index++];
        const CommandHeader header{cmd_list.head[cmd_list.current_index++]};

        // Write to the requested PICA register.
        if (process_write(header.cmd_id, value))
            break;

        // Write any extra paramters as well.
        for (u32 i = 0; i < header.extra_data_length; ++i) {
            const u32 cmd = header.cmd_id + (header.group_commands ? i + 1 : 0);
            const u32 extra_value = cmd_list.head[cmd_list.current_index++];
            if (process_write(cmd, extra_value))
                break;
        }

        iterations++;
    }

    return find_info;
}

template <class Archive>
void PicaCore::CommandList::serialize(Archive& ar, const u32 file_version) {
    ar & addr;
    ar & length;
    ar & current_index;
    if (Archive::is_loading::value) {
        const u8* ptr = Core::System::GetInstance().Memory().GetPhysicalPointer(addr);
        head = reinterpret_cast<const u32*>(ptr);
    }
}

SERIALIZE_IMPL(PicaCore::CommandList)

} // namespace Pica
