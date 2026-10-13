// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include "video_core/pica/primitive_assembly.h"

namespace Pica {

// CodexAstraLocal: This owner-only transaction permits CPU assembly while later
// shader grains finish. The sink must reserve reversible, unpublished appends;
// no guest-memory write, renderer submission or external callback is permitted.
template <typename Sink>
class VertexAppendTransaction {
public:
    VertexAppendTransaction(PrimitiveAssembler& assembler_, Sink& sink_,
                            const PrimitiveAssembler::TriangleHandler& triangle_)
        : assembler{assembler_}, sink{sink_}, triangle{triangle_} {}

    bool Begin(u32 inputs) {
        if (!sink.BeginVertexAppend(inputs, checkpoint)) return false;
        // CodexAstraLocal: Include retained unused buffers and winding. The
        // compute-export API includes only live slots and cannot undo an append.
        static_assert(std::is_trivially_copyable_v<PrimitiveAssembler>);
        std::memcpy(image.data(), &assembler, image.size());
        return true;
    }

    template <typename Get>
    void operator()(u32 count, const Get& get) {
        assembler.SubmitOrdered(count, get, triangle);
    }

    void Rollback() noexcept {
        // CodexAstraLocal: All shader borrowers have joined before rollback;
        // restoring object bytes never evaluates uninitialized f24 components.
        sink.RollbackVertexAppend(checkpoint);
        std::memcpy(&assembler, image.data(), image.size());
    }
    void Commit() noexcept {}

private:
    PrimitiveAssembler& assembler;
    Sink& sink;
    const PrimitiveAssembler::TriangleHandler& triangle;
    std::array<std::byte, sizeof(PrimitiveAssembler)> image;
    std::size_t checkpoint{};
};
} // namespace Pica
