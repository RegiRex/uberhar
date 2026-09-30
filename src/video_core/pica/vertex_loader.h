// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/memory.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica/uberhar_vertex_input.h" // AstraEH: Fused transport description.

namespace Memory {
class MemorySystem;
}

namespace Pica {

class VertexLoader {
public:
    explicit VertexLoader(Memory::MemorySystem& memory_, const PipelineRegs& regs);
    ~VertexLoader();

    void LoadVertex(PAddr base_address, u32 index, u32 vertex, AttributeBuffer& input,
                    AttributeBuffer& input_default_attributes) const;

    template <typename T>
    void LoadAttribute(PAddr source_addr, u32 attrib, AttributeBuffer& out) const {
        const T* data = reinterpret_cast<const T*>(memory.GetPhysicalPointer(source_addr));
        for (u32 comp = 0; comp < vertex_attribute_elements[attrib]; ++comp) {
            out[attrib][comp] = f24::FromFloat32(data[comp]);
        }
    }

    // AstraEH: Expose only initialized values from the authoritative loader layout.
    NativeInputAttribute DescribeNativeInput(u32 attribute) const {
        const bool fixed = vertex_attribute_is_default[attribute];
        const u32 elements = vertex_attribute_elements[attribute];
        return {vertex_attribute_sources[attribute], vertex_attribute_strides[attribute], elements,
                !fixed && elements ? vertex_attribute_formats[attribute]
                                   : PipelineRegs::VertexAttributeFormat::BYTE, fixed};
    }

    int GetNumTotalAttributes() const {
        return num_total_attributes;
    }

private:
    Memory::MemorySystem& memory;
    std::array<u32, 16> vertex_attribute_sources;
    std::array<u32, 16> vertex_attribute_strides{};
    std::array<PipelineRegs::VertexAttributeFormat, 16> vertex_attribute_formats;
    std::array<u32, 16> vertex_attribute_elements{};
    std::array<bool, 16> vertex_attribute_is_default;
    int num_total_attributes = 0;
};

} // namespace Pica
