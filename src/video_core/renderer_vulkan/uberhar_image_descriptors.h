// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <limits>
#include "common/assert.h"
#include "common/common_types.h" // CodexAstraLocal: Descriptor keys use fixed-width fields.
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

// CodexAstraLocal: Preserve the exact declared writes, including cube array
// elements and descriptor type. Equality compares fields, never Vulkan padding.
struct ImageDescriptorWrite {
    vk::DescriptorType type{};
    u8 binding{};
    u8 element{};
    vk::ImageView view{};
    vk::Sampler sampler{};
    vk::ImageLayout layout{vk::ImageLayout::eGeneral};
    bool operator==(const ImageDescriptorWrite&) const = default;
};

struct ImageDescriptorPayload {
    static constexpr std::size_t Capacity = 8; // Six shadow faces plus units 1 and 2.
    std::array<ImageDescriptorWrite, Capacity> writes{};
    std::size_t count{};

    // CodexAstraLocal: Preparation owns a fixed payload, not a descriptor set.
    // Every preparation resets count, so a rejected GPU attempt cannot leak writes.
    void Append(ImageDescriptorWrite write) {
        ASSERT(count < Capacity);
        writes[count++] = write;
    }
    bool operator==(const ImageDescriptorPayload& other) const {
        if (count != other.count)
            return false;
        for (std::size_t i = 0; i < count; ++i)
            if (!(writes[i] == other.writes[i]))
                return false;
        return true;
    }
};

// CodexAstraLocal: One immutable last value per heap. Resolve only at the final
// consumer tick; pool retirement and handle lifetime are separate requirements.
// The allocator callback must enqueue all writes before returning its new set.
class ImageDescriptorReuse {
public:
    template <typename Allocate>
    vk::DescriptorSet Resolve(const ImageDescriptorPayload& requested, u64 final_tick,
                              u64 resource_epoch, Allocate&& allocate) {
        ++requests;
        if (set && tick == final_tick && epoch == resource_epoch &&
            resource_epoch != std::numeric_limits<u64>::max() && payload == requested) {
            ++hits;
            return set;
        }
        const auto next = allocate(requested);
        ASSERT(next);
        // CodexAstraLocal: Publish only after successful allocation/writes. An
        // old captured set is never rewritten or extended into a later tick.
        payload = requested;
        tick = final_tick;
        epoch = resource_epoch;
        set = next;
        writes += requested.count;
        return set;
    }

    void Invalidate() { set = nullptr; }

    // CodexAstraLocal TEST-ONLY LOG: Existing report cadence exposes useful
    // descriptor work removed. Remove counters after on-device reuse qualification.
    u64 requests{}, hits{}, writes{};

private:
    ImageDescriptorPayload payload{};
    vk::DescriptorSet set{};
    u64 tick{}, epoch{};
};

} // namespace Vulkan
