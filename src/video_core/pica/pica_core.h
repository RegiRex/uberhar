// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <chrono> // AstraEH: Bounded vertex-stage windows.

#include "common/common_types.h"
#include "core/hle/service/gsp/gsp_interrupt.h"
#include "video_core/pica/dirty_regs.h"
#include "video_core/pica/geometry_pipeline.h"
#include "video_core/pica/packed_attribute.h"
#include "video_core/pica/primitive_assembly.h"
#include "video_core/pica/regs_external.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/regs_lcd.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/pica/uberhar_vertex_plan_cache.h"
#include "video_core/pica/uberhar_vertex_output.h" // AstraEH: Prepared native output and sparse samples.
#include "video_core/pica/uberhar_vertex_timing.h" // CodexAstraLocal: Finite opt-in CPU chunks.

// AstraPro: A shared typed admission query lets Vulkan recheck the real PICA
// assembler/debugger state, rather than substituting assumed-safe booleans.
namespace Vulkan::ReadyVertexPolicy {
enum class Admission : u32;
}

namespace Memory {
class MemorySystem;
}

namespace VideoCore {
class RasterizerInterface;
}

namespace Pica {

class DebugContext;
class ShaderEngine;

class DelayGenerator {
private:
    // A GPU is a very complex system, the timings resulting from
    // a 3D draw depend on many factors, including triangle counts,
    // texture sizes and format, shader complexity, cache
    // and memory layout, etc. At this point in time, we don't
    // have enough information nor implemented hw emulation
    // capabilities to achieve a proper timing estimate.
    //
    // Instead, we will try to measure how complex a scene is based
    // on the amount of geometry that is drawn, the amount of GPU
    // commands and the shader complexity. We will ignore all
    // the other factors for now.

    // Using Mario Kart 7 as the reference, it is understood that on
    // average the console can handle around 20k triangles per frame.
    // This game uses standard GPU features, with no fancy stuff,
    // so we can consider it an average. To prevent hurting performance,
    // we will also assume the GPU is twice as powerful. Afterall we only
    // want timing accuracy to fix bugs at this point.
    // This average already takes into account shader complexity averages.
    static constexpr float nanoseconds_per_triangle = 800.f / 2;

    // Of the total amount of submitted triangles, many of them will be culled.
    // This heavily depends on the specific scene, so we will assume 35% of the
    // triangles being culled. Furthermore, the culled triangles will take way less
    // processing time as they will skip most of the pipeline processing, so we
    // can assume that a culled triangle will only take about 20% of the time.
    static constexpr float culled_triangle_threshold = 0.35f; // 35%
    static constexpr float culled_triangle_time_cost = 0.20f; // 20%

    // We will assume that each command will take around 6 cycles @ 268MHz
    // There are no real measurements to support this claim, but it sounds
    // reasonable. TODO: Measure on real HW.
    static constexpr float nanoseconds_per_command = 22.4f;

public:
    inline void AddCommands(size_t commands) {
        command_count += commands;
    }

    inline void AddVertices(size_t vertices, PipelineRegs::TriangleTopology topology) {
        size_t triangles{};
        if (topology == PipelineRegs::TriangleTopology::Fan ||
            topology == PipelineRegs::TriangleTopology::Strip) {
            triangles = (vertices >= 3) ? (vertices - 2) : 1;
        } else {
            // Geometry shaders produce more vertices per given vertex,
            // but they are not that relevant for timing emulation.
            triangles = vertices / 3;
        }

        triangle_count += triangles;
    }

    u64 CalculateAndResetDelay() {
        float result = command_count * nanoseconds_per_command;

        result += (1.f - culled_triangle_threshold) * triangle_count * nanoseconds_per_triangle;
        result += culled_triangle_threshold * triangle_count *
                  (nanoseconds_per_triangle * culled_triangle_time_cost);

        triangle_count = 0;
        command_count = 0;

        return static_cast<u64>(result);
    }

private:
    size_t triangle_count{};
    size_t command_count{};

    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const u32 file_version) {
        ar & triangle_count;
        ar & command_count;
    }
};

class PicaCore {
public:
    explicit PicaCore(Memory::MemorySystem& memory, std::shared_ptr<DebugContext> debug_context_);
    ~PicaCore();

    void BindRasterizer(VideoCore::RasterizerInterface* rasterizer);

    void SetInterruptHandler(Service::GSP::InterruptHandler& signal_interrupt);

    void ProcessCmdList(PAddr list, u32 size, bool ignore_list);

    Vulkan::ReadyVertexPolicy::Admission GetReadyGpuVertexAdmission() const;

private:
    void InitializeRegs();

    void HandleSpecialRegBatch(u32 id, const u32* values, u32 count);

    void HandleSpecialReg(u32 id, u32 value, bool& stop_requested);

    void WriteInternalRegBatch(u32 id, const u32* values, u32 count, u32 mask,
                               bool& stop_requested);

    void WriteInternalRegSequential(u32 id, const u32* __restrict values, u32 count, u32 mask,
                                    bool& stop_requested);

    void WriteInternalReg(u32 id, u32 value, u32 mask, bool& stop_requested);

    void SubmitImmediate(u32 data);

    void DrawImmediate();

    void DrawArrays(bool is_indexed);

    // AstraEH: Reuse the enclosing draw timestamp for sparse diagnostic admission.
    void LoadVertices(bool is_indexed, std::chrono::steady_clock::time_point batch_start);
    // CodexAstraLocal: Owned complete-list CPU work may overlap renderer
    // preparation; all guest-visible boundaries reconcile on this owner.
    bool TryDeferredVertices(bool is_indexed, std::chrono::steady_clock::time_point batch_start);
    void ReconcileDeferredVertices();
    // AstraEH: Aggregate progress, including the engine actually in use.
    void ReportVirtualVertices(const char* kind, std::chrono::steady_clock::time_point now);
    // AstraPro: GPU-heavy runs must not starve route progress diagnostics.
    void ReportVirtualVerticesIfDue();

public:
    union Regs {
        static constexpr std::size_t NUM_REGS = 0x732;

        struct {
            u32 hardware_id;
            INSERT_PADDING_WORDS(0x3);
            MemoryFillConfig memory_fill_config[2];
            u32 vram_bank_control;
            u32 gpu_busy;
            INSERT_PADDING_WORDS(0x22);
            u32 backlight_control;
            INSERT_PADDING_WORDS(0xCF);
            FramebufferConfig framebuffer_config[2];
            INSERT_PADDING_WORDS(0x180);
            DisplayTransferConfig display_transfer_config;
            INSERT_PADDING_WORDS(0xF5);
            RegsInternal internal;
        };
        std::array<u32, NUM_REGS> reg_array;
    };
    static_assert(sizeof(Regs) == Regs::NUM_REGS * sizeof(u32));

    struct CommandList {
        PAddr addr;
        const u32* head;
        u32 current_index;
        u32 length;

        void Reset(PAddr addr, const u8* head, u32 size) {
            this->addr = addr;
            this->head = reinterpret_cast<const u32*>(head);
            this->length = size / sizeof(u32);
            current_index = 0;
        }

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const u32 file_version);
    };

    struct ImmediateModeState {
        AttributeBuffer input_vertex{};
        u32 current_attribute{};
        bool reset_geometry_pipeline{true};
        PackedAttribute queue;

        void Reset() {
            current_attribute = 0;
            reset_geometry_pipeline = true;
            queue.Reset();
        }

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const u32 file_version) {
            ar & input_vertex;
            ar & current_attribute;
            ar & reset_geometry_pipeline;
            ar & queue;
        }
    };

    struct ProcTex {
        static constexpr u8 TableAllDirty = 0xFF;

        union ValueEntry {
            u32 raw;

            // LUT value, encoded as 12-bit fixed point, with 12 fraction bits
            BitField<0, 12, u32> value; // 0.0.12 fixed point

            // Difference between two entry values. Used for efficient interpolation.
            // 0.0.12 fixed point with two's complement. The range is [-0.5, 0.5).
            // Note: the type of this is different from the one of lighting LUT
            BitField<12, 12, s32> difference;

            f32 ToFloat() const {
                return static_cast<f32>(value) / 4095.f;
            }

            f32 DiffToFloat() const {
                return static_cast<f32>(difference) / 4095.f;
            }
        };

        union ColorEntry {
            u32 raw;
            BitField<0, 8, u32> r;
            BitField<8, 8, u32> g;
            BitField<16, 8, u32> b;
            BitField<24, 8, u32> a;

            Common::Vec4<u8> ToVector() const {
                return {static_cast<u8>(r), static_cast<u8>(g), static_cast<u8>(b),
                        static_cast<u8>(a)};
            }
        };

        union ColorDifferenceEntry {
            u32 raw;
            BitField<0, 8, s32> r; // half of the difference between two ColorEntry
            BitField<8, 8, s32> g;
            BitField<16, 8, s32> b;
            BitField<24, 8, s32> a;

            Common::Vec4<s32> ToVector() const {
                return Common::Vec4<s32>{r, g, b, a} * 2;
            }
        };

        std::array<ValueEntry, 128> noise_table;
        std::array<ValueEntry, 128> color_map_table;
        std::array<ValueEntry, 128> alpha_map_table;
        std::array<ColorEntry, 256> color_table;
        std::array<ColorDifferenceEntry, 256> color_diff_table;
        union {
            u8 table_dirty = TableAllDirty;
            BitField<0, 1, u8> noise_lut_dirty;
            BitField<2, 1, u8> color_map_dirty;
            BitField<3, 1, u8> alpha_map_dirty;
            BitField<4, 1, u8> lut_dirty;
            BitField<5, 1, u8> diff_lut_dirty;
        };

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const u32 file_version) {
            ar& boost::serialization::make_binary_object(this, sizeof(ProcTex));
            if (Archive::is_loading::value) {
                table_dirty = TableAllDirty;
            }
        }
    };

    struct Lighting {
        static constexpr u32 LutAllDirty = 0xFFFFFF;

        union LutEntry {
            // Used for raw access
            u32 raw;

            // LUT value, encoded as 12-bit fixed point, with 12 fraction bits
            BitField<0, 12, u32> value; // 0.0.12 fixed point

            // Used for efficient interpolation.
            BitField<12, 11, u32> difference; // 0.0.11 fixed point
            BitField<23, 1, u32> neg_difference;

            f32 ToFloat() const {
                return static_cast<f32>(value) / 4095.f;
            }

            f32 DiffToFloat() const {
                const f32 diff = static_cast<f32>(difference) / 2047.f;
                return neg_difference ? -diff : diff;
            }

            template <class Archive>
            void serialize(Archive& ar, const u32 file_version) {
                ar & raw;
            }
        };

        std::array<std::array<LutEntry, 256>, 24> luts;
        u32 lut_dirty = LutAllDirty;

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const u32 file_version) {
            ar& boost::serialization::make_binary_object(this, sizeof(Lighting));
            if (Archive::is_loading::value) {
                lut_dirty = LutAllDirty;
            }
        }
    };

    struct Fog {
        union LutEntry {
            // Used for raw access
            u32 raw;

            BitField<0, 13, s32> difference; // 1.1.11 fixed point
            BitField<13, 11, u32> value;     // 0.0.11 fixed point

            f32 ToFloat() const {
                return static_cast<f32>(value) / 2047.0f;
            }

            f32 DiffToFloat() const {
                return static_cast<f32>(difference) / 2047.0f;
            }
        };

        std::array<LutEntry, 128> lut;
        bool lut_dirty = true;

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const u32 file_version) {
            ar& boost::serialization::make_binary_object(this, sizeof(Fog));
            if (Archive::is_loading::value) {
                lut_dirty = true;
            }
        }
    };

    RegsLcd regs_lcd{};
    Regs regs{};
    DirtyRegs dirty_regs{};
    GeometryShaderUnit gs_unit;
    ShaderSetup vs_setup;
    ShaderSetup gs_setup;
    ProcTex proctex{};
    Lighting lighting{};
    Fog fog{};
    AttributeBuffer input_default_attributes{};
    ImmediateModeState immediate{};

    DelayGenerator delay_generator{};

private:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const u32 file_version) {
        // CodexAstraLocal: Host tickets never enter save state. Restore the exact
        // completed assembler pair before saving or replacing any guest state.
        ReconcileDeferredVertices();
        ar & regs_lcd;
        ar & regs.reg_array;
        ar & gs_unit;
        ar & vs_setup;
        ar & gs_setup;
        ar & proctex;
        ar & lighting;
        ar & fog;
        ar & input_default_attributes;
        ar & immediate;
        ar & delay_generator;
        ar & geometry_pipeline;
        ar & primitive_assembler;
        ar & cmd_list;
        if (Archive::is_loading::value) {
            dirty_regs.SetAllDirty();
        }
    }

public:
    struct RenderPropertiesGuess {
        u32 vp_height;
        PAddr paddr;
        bool vp_heigh_found = false;
        bool paddr_found = false;
    };

    RenderPropertiesGuess GuessCmdRenderProperties(PAddr list, u32 size);

private:
    Memory::MemorySystem& memory;
    VideoCore::RasterizerInterface* rasterizer;
    std::shared_ptr<DebugContext> debug_context;
    Service::GSP::InterruptHandler signal_interrupt;
    GeometryPipeline geometry_pipeline;
    PrimitiveAssembler primitive_assembler;
    CommandList cmd_list;
    std::unique_ptr<ShaderEngine> shader_engine;
    // CodexAstraLocal: Draw-synchronous CPU workers and bounded host certificates
    // never enter guest save state; their borrowed jobs finish before each draw returns.
    struct ParallelVertexState;
    std::unique_ptr<ParallelVertexState> parallel_vertices;
    bool parallel_allocation_failed{};
    // CodexAstraLocal: Published packets own all worker inputs; the bounded
    // owner list retains only completion/statistics and serialized-tail duties.
    struct DeferredVertexState;
    std::unique_ptr<DeferredVertexState> deferred_vertices;
    bool deferred_allocation_failed{};
    unsigned deferred_processors{}; // CodexAstraLocal: Query process allowance once per PICA owner.
    // AstraEH: Window snapshots are host-only diagnostics and never enter save states.
    std::chrono::steady_clock::time_point virtual_window_start{};
    u64 virtual_vertex_invocations{}, virtual_vertex_hits{}, virtual_last_ns{},
        virtual_last_inputs{}, virtual_last_invocations{};
    // AstraEH: Measure the complete CPU vertex stage separately from GPU pipeline waits.
    u64 virtual_vertex_batches{}, virtual_vertex_inputs{}, virtual_vertex_ns{},
        virtual_vertex_max_ns{};
    // AstraEH: Host-only beta route/sample counters; never serialized as guest state.
    // AstraEH: Fused-input admission/results, reset with the PICA instance at title start.
    std::array<u64, 5> native_input_results{};
    u64 native_input_maps{}, native_input_fused_vertices{}, native_input_legacy_vertices{};
    // CodexAstraLocal: Five fixed actual-input invocation buckets consume 40B;
    // update once after each completed FIFO draw, reset with this PICA owner,
    // and report at the existing cadence. No guest state or per-vertex counter.
    std::array<u64, 5> native_input_recipe_invocations{};
    // AstraPro: Exact-index retry coverage, reset with the PICA core.
    // AstraPro: Count only successfully submitted optional GPU batches.
    u64 ready_gpu_vertex_batches{}, ready_gpu_vertex_inputs{}, ready_gpu_vertex_attempts{};
    // AstraPro: Periodic host attempt spans, not GPU execution or total CPU use.
    // Sample one in 1024 attempts; the native CPU fallback happens after this span.
    u64 ready_gpu_host_success_samples{}, ready_gpu_host_fallback_samples{};
    u64 ready_gpu_host_success_ns{}, ready_gpu_host_fallback_ns{}, ready_gpu_host_max_ns{};
    std::array<u64, 5> ready_gpu_topologies{};
    // AstraPro: Exclusive policy outcomes and actual accepted topology; host-only.
    std::array<u64, 11> ready_gpu_admissions{};
    std::array<u64, 5> ready_gpu_selected_topologies{};
    u64 native_index_retries{}, native_scanned_indices{}, native_index_rescues{},
        native_rescued_vertices{}, native_index_escapes{};
    // AstraPro: Pure mapping reuse also accelerates CPU fallback inside Combo.
    NativeVertexPlanCache native_plan_cache;
    NativeVertexSamples native_samples;
    // AstraEH: Host-only admission state; ordinary batches add no diagnostic clock reads.
    NativeVertexSampleBudget native_sample_budget;
    bool native_batch_sampled{};
    // CodexAstraLocal: Loaded once only for Combo modes; host observations never
    // enter a save state, another title, or the ordinary Native/Custom runner.
    std::unique_ptr<VertexTiming::Session> vertex_timing;
    u64 native_vertex_batches{}, native_vertex_inputs{}, native_vertex_conversions{},
        native_vertex_reuses{}, native_mapping_fallbacks{}, native_geometry_fallbacks{},
        native_debug_fallbacks{};
};

#define GPU_REG_INDEX(field_name) (offsetof(Pica::PicaCore::Regs, field_name) / sizeof(u32))

} // namespace Pica
