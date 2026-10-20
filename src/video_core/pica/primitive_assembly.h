// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstring> // CodexAstraLocal: Copy only live/touched compute semantic slots.
#include <functional>
#include <type_traits> // CodexAstraLocal: Enforce the immutable borrowed getter contract.
#include <boost/serialization/access.hpp>
#include <boost/serialization/array.hpp>
#include "common/assert.h"     // AstraEH: Enforce the accelerated-batch entry contract.
#include "common/scope_exit.h" // AstraEH: Restore assembly state on every exit path.
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/compute_assembly.h"
#include "video_core/pica/regs_pipeline.h"

namespace Pica {

/**
 * Utility class to build triangles from a series of vertices,
 * according to a given triangle topology.
 */
struct PrimitiveAssembler {
    using TriangleHandler =
        std::function<void(const OutputVertex&, const OutputVertex&, const OutputVertex&)>;

    explicit PrimitiveAssembler(
        PipelineRegs::TriangleTopology topology = PipelineRegs::TriangleTopology::List);

    /**
     * Queues a vertex, builds primitives from the vertex queue according to the given
     * triangle topology, and calls triangle_handler for each generated primitive.
     * NOTE: We could specify the triangle handler in the constructor, but this way we can
     * keep event and handler code next to each other.
     */
    void SubmitVertex(const OutputVertex& vtx, const TriangleHandler& triangle_handler);

    /**
     * Invert the vertex order of the next triangle. Called by geometry shader emitter.
     * This only takes effect for TriangleTopology::Shader.
     */
    void SetWinding() noexcept {
        winding = true;
    }

    /**
     * Resets the internal state of the PrimitiveAssembler.
     */
    void Reset() {
        buffer_index = 0;
        strip_ready = false;
        winding = false;
    }

    /**
     * Reconfigures the PrimitiveAssembler to use a different triangle topology.
     */
    void Reconfigure(PipelineRegs::TriangleTopology topology) {
        Reset();
        this->topology = topology;
    }

    /**
     * Returns whether the PrimitiveAssembler has an empty internal buffer.
     */
    bool IsEmpty() const {
        return buffer_index == 0 && !strip_ready;
    }

    // AstraPro: Read-only admission evidence. Do not clear a pending GS winding
    // request to make a GPU draw eligible; the ordinary CPU path must consume it.
    bool HasPendingWinding() const noexcept {
        return winding;
    }

    /**
     * Returns the current topology.
     */
    PipelineRegs::TriangleTopology GetTopology() const {
        return topology;
    }

    // CodexAstraLocal: Reset/Reconfigure preserve buffer[] and are logical no-ops
    // for an empty, unwound complete-triangle stream with unchanged topology.
    // Its deferred packets still own the exact saved pair until reconciliation.
    bool CanResetDeferredTriangles(PipelineRegs::TriangleTopology next) const {
        return next == topology &&
               (topology == PipelineRegs::TriangleTopology::List ||
                topology == PipelineRegs::TriangleTopology::Shader) &&
               IsEmpty() && !winding;
    }

    // CodexAstraLocal: A completed deferred List/Shader draw has emitted every
    // triangle in order. Reconcile its exact serialized trailing pair on the
    // owner without replaying all vertices or emitting the draw a second time.
    void AdoptCompletedTriangleTail(const std::array<OutputVertex, 2>& last_pair) {
        ASSERT((topology == PipelineRegs::TriangleTopology::List ||
                topology == PipelineRegs::TriangleTopology::Shader) && IsEmpty() && !winding);
        buffer = last_pair;
    }

    // CodexAstraLocal: Preserve this serialized owner across compute draws and
    // immediate vertices. Do not read unused slots, and commit only after whole
    // producer success and successful renderer admission of every emitted triangle.
    ComputeAssemblyState ExportComputeState() const {
        ComputeAssemblyState state{};
        state.topology = static_cast<u32>(topology);
        state.index = static_cast<u32>(buffer_index);
        state.ready = strip_ready;
        state.winding = winding;
        ASSERT(state.Valid());
        for (u32 slot = 0; slot < 2; ++slot)
            if (state.LiveMask() & (1U << slot))
                std::memcpy(state.words.data() + slot * 24, &buffer[slot], sizeof(OutputVertex));
        return state;
    }
    bool CommitComputeState(const ComputeAssemblyResult& completed) {
        const auto& state = completed.state;
        if (!state.Valid() || state.topology != static_cast<u32>(topology) ||
            completed.written_mask > 3) return false;
        for (u32 slot = 0; slot < 2; ++slot)
            if (completed.written_mask & (1U << slot))
                std::memcpy(&buffer[slot], state.words.data() + slot * 24, sizeof(OutputVertex));
        buffer_index = static_cast<int>(state.index);
        strip_ready = state.ready;
        winding = state.winding;
        return true;
    }

    // AstraEH: An accelerated host draw assembles its own primitives and leaves
    // this persistent assembler untouched. A ready CPU bridge must do the same:
    // otherwise a strip/fan tail forces later draws onto the ordinary CPU path.
    // Entry is allowed only at the existing acceleration boundary (empty buffer,
    // guest GS disabled). Preserve the saved winding flag even after exceptions;
    // no uninitialized vertex-buffer contents need to be copied.
    template <typename Draw>
    void RunIsolatedBatch(Draw&& draw) {
        ASSERT(IsEmpty());
        const bool saved_winding = winding;
        Reset();
        SCOPE_EXIT({
            Reset();
            winding = saved_winding;
        });
        std::forward<Draw>(draw)();
    }

    // CodexAstraLocal: The accelerated append-only sink cannot inspect/reenter
    // this assembler. Borrow immutable references through the full call and skip
    // redundant first/second-vertex copies for interior List/Shader triangles.
    // On normal exit or callback exception, restore the exact last buffered pair
    // so subsequent tails and serialized (even currently unused) values match.
    // This specialized API requires a noexcept getter and a non-reentrant handler;
    // general scalar submission and Strip/Fan behavior remain inherited.
    template <typename Getter>
    void SubmitOrdered(u32 count, Getter&& get, const TriangleHandler& handler) {
        static_assert(noexcept(get(u32{})), "borrowed getter must not throw");
        static_assert(std::is_same_v<decltype(get(u32{})), const OutputVertex&>,
                      "borrowed getter must retain immutable vertices");
        if (topology != PipelineRegs::TriangleTopology::List &&
            topology != PipelineRegs::TriangleTopology::Shader) {
            for (u32 index = 0; index < count; ++index)
                SubmitVertex(get(index), handler);
            return;
        }
        u32 index{};
        while (index < count && buffer_index != 0)
            SubmitVertex(get(index++), handler);
        const OutputVertex* last_first{};
        const OutputVertex* last_second{};
        const auto preserve_pair = [&] {
            if (last_first) {
                buffer[0] = *last_first;
                buffer[1] = *last_second;
            }
        };
        try {
            while (count - index >= 3) {
                const auto& first = get(index);
                const auto& second = get(index + 1);
                const auto& third = get(index + 2);
                last_first = &first;
                last_second = &second;
                if (topology == PipelineRegs::TriangleTopology::Shader && winding) {
                    handler(second, first, third);
                    winding = false;
                } else {
                    handler(first, second, third);
                }
                index += 3;
            }
        } catch (...) {
            preserve_pair();
            throw;
        }
        preserve_pair();
        while (index < count)
            SubmitVertex(get(index++), handler);
    }

private:
    PipelineRegs::TriangleTopology topology;
    int buffer_index = 0;
    std::array<OutputVertex, 2> buffer;
    bool strip_ready = false;
    bool winding = false;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & topology;
        ar & buffer_index;
        ar & buffer;
        ar & strip_ready;
        ar & winding;
    }
    friend class boost::serialization::access;
};

} // namespace Pica
