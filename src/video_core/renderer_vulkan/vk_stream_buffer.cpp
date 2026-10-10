// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring> // CodexAstraLocal: Bounded read-only upload snapshot.
#include <limits>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/literals.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_memory_util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

namespace Vulkan {

namespace {

using namespace Common::Literals;

std::string_view BufferTypeName(BufferType type) {
    switch (type) {
    case BufferType::Upload:
        return "Upload";
    case BufferType::Download:
        return "Download";
    case BufferType::Stream:
        return "Stream";
    default:
        return "Invalid";
    }
}

vk::MemoryPropertyFlags MakePropertyFlags(BufferType type) {
    switch (type) {
    case BufferType::Upload:
        return vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    case BufferType::Download:
        return vk::MemoryPropertyFlagBits::eHostVisible |
               vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostCached;
    case BufferType::Stream:
        return vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible |
               vk::MemoryPropertyFlagBits::eHostCoherent;
    default:
        UNREACHABLE_MSG("Unknown buffer type {}", type);
        return vk::MemoryPropertyFlagBits::eHostVisible;
    }
}

/// Get the preferred host visible memory type.
u32 GetMemoryType(const vk::PhysicalDeviceMemoryProperties& properties, BufferType type) {
    vk::MemoryPropertyFlags flags = MakePropertyFlags(type);
    std::optional<u32> preferred_type;

    // Try to find a memory type with all the requested flags
    preferred_type = FindMemoryType(properties, flags);
    if (preferred_type) {
        return *preferred_type;
    }

    // If not found, try removing flags one by one
    constexpr std::array remove_flags = {
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::MemoryPropertyFlagBits::eHostCached,
        vk::MemoryPropertyFlagBits::eHostCoherent,
    };

    for (auto remove_flag : remove_flags) {
        if ((flags & remove_flag) == vk::MemoryPropertyFlags{}) {
            continue;
        }
        flags &= ~remove_flag;
        preferred_type = FindMemoryType(properties, flags);
        if (preferred_type) {
            return *preferred_type;
        }
    }

    // If still not found, try with only eHostVisible flag
    preferred_type = FindMemoryType(properties, vk::MemoryPropertyFlagBits::eHostVisible);
    if (preferred_type) {
        return *preferred_type;
    }

    // If we reach here, we couldn't find any suitable memory type
    UNREACHABLE_MSG("Failed to find a suitable memory type for buffer type {}",
                    BufferTypeName(type));
}

constexpr u64 WATCHES_INITIAL_RESERVE = 0x4000;
constexpr u64 WATCHES_RESERVE_CHUNK = 0x1000;

} // Anonymous namespace

// CodexAstraLocal: A queued upload may outlive ring bookkeeping after a canceled
// command. Retain the actual mapping, buffer and memory as one allocation. The
// Instance/device must outlive scheduler drainage, as in ordinary renderer teardown.
class StreamBuffer::Allocation final {
public:
    Allocation(const Instance& instance_, vk::Device device_, vk::Buffer buffer_,
               vk::DeviceMemory memory_, u8* mapped_, u64 bytes_)
        : instance{instance_}, device{device_}, buffer{buffer_}, memory{memory_},
          mapped{mapped_}, bytes{bytes_} {}
    ~Allocation() {
        device.unmapMemory(memory);
        device.destroyBuffer(buffer);
        device.freeMemory(memory);
        instance.RecordRawStreamFree(bytes);
    }
    const Instance& instance;
    vk::Device device;
    vk::Buffer buffer;
    vk::DeviceMemory memory;
    u8* mapped;
    u64 bytes;
};

// CodexAstraLocal: Publish exactly once on the ordered recording thread, before
// that draw's eventual queue submission provides host-to-device visibility.
// A malformed payload returns failure for the caller's terminal error channel.
bool StreamBuffer::DeferredUpload::Publish(std::span<const u8> source) const noexcept {
    if (!owner || published || source.size() != size)
        return false;
    std::memcpy(owner->mapped + offset, source.data(), size);
    published = true;
    return true;
}

vk::Buffer StreamBuffer::DeferredUpload::Handle() const noexcept {
    return owner ? owner->buffer : vk::Buffer{};
}

StreamBuffer::StreamBuffer(const Instance& instance_, Scheduler& scheduler_,
                           vk::BufferUsageFlags usage_, u64 size, BufferType type_)
    : instance{instance_}, scheduler{scheduler_}, device{instance.GetDevice()},
      stream_buffer_size{size}, usage{usage_}, type{type_} {
    // CodexAstraUlt: Replace unguarded construction: a failed watch allocation
    // or non-Vulkan exception after buffer creation does not run our destructor.
    // Release partial raw ownership before preserving the original exception.
    try {
        CreateBuffers(size);
        ReserveWatches(current_watches, WATCHES_INITIAL_RESERVE);
        ReserveWatches(previous_watches, WATCHES_INITIAL_RESERVE);
    } catch (...) {
        DestroyBuffers();
        throw;
    }
}

StreamBuffer::~StreamBuffer() {
    // CodexAstraUlt: Share complete teardown with partial-attempt recovery so
    // raw Vulkan allocations have exactly one release and accounting decrement.
    DestroyBuffers();
}

void StreamBuffer::DestroyBuffers() noexcept {
    // CodexAstraLocal: Complete allocations have shared ownership; raw handles
    // below are aliases. Partial creation still follows the original cleanup.
    if (allocation) {
        allocation.reset();
        mapped = nullptr;
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        allocation_bytes = 0;
        return;
    }
    if (mapped) {
        device.unmapMemory(memory);
        mapped = nullptr;
    }
    if (buffer) {
        device.destroyBuffer(buffer);
        buffer = VK_NULL_HANDLE;
    }
    if (memory) {
        device.freeMemory(memory);
        memory = VK_NULL_HANDLE;
        instance.RecordRawStreamFree(allocation_bytes);
        allocation_bytes = 0;
    }
}

std::tuple<u8*, u32, bool> StreamBuffer::Map(u32 size, u64 alignment) {
    // CodexAstraLocal: Any new map invalidates previous deferred reservations.
    if (mapping_generation != std::numeric_limits<u64>::max())
        ++mapping_generation;
    if (!is_coherent && type == BufferType::Stream) {
        size = Common::AlignUp(size, instance.NonCoherentAtomSize());
    }

    ASSERT(size <= stream_buffer_size);
    mapped_size = size;

    if (alignment > 0) {
        offset = Common::AlignUp(offset, alignment);
    }

    bool invalidate{false};
    if (offset + size > stream_buffer_size) {
        // CodexAstraLocal: Clean UBO/LUT offsets may have outlived their upload
        // watches. Complete their last actual draw before exposing reused bytes;
        // non-wrapping maps keep the original per-range waiting behavior.
        if (last_draw_use_tick) {
            scheduler.Wait(last_draw_use_tick);
            last_draw_use_tick = 0;
        }
        // The buffer would overflow, save the amount of used watches and reset the state.
        invalidate = true;
        invalidation_mark = current_watch_cursor;
        current_watch_cursor = 0;
        offset = 0;

        // Swap watches and reset waiting cursors.
        std::swap(previous_watches, current_watches);
        wait_cursor = 0;
        wait_bound = 0;
    }

    const u64 mapped_upper_bound = offset + size;
    WaitPendingOperations(mapped_upper_bound);

    return std::make_tuple(mapped + offset, offset, invalidate);
}

void StreamBuffer::Commit(u32 size) {
    // CodexAstraLocal: An ordinary commit also consumes a deferred reservation.
    if (mapping_generation != std::numeric_limits<u64>::max())
        ++mapping_generation;
    if (!is_coherent && type == BufferType::Stream) {
        size = Common::AlignUp(size, instance.NonCoherentAtomSize());
    }

    ASSERT_MSG(size <= mapped_size, "Reserved size {} is too small compared to {}", mapped_size,
               size);

    const vk::MappedMemoryRange range = {
        .memory = memory,
        .offset = offset,
        .size = size,
    };

    if (!is_coherent && type == BufferType::Download) {
        device.invalidateMappedMemoryRanges(range);
    } else if (!is_coherent) {
        device.flushMappedMemoryRanges(range);
    }

    offset += size;

    if (current_watch_cursor + 1 >= current_watches.size()) {
        // Ensure that there are enough watches.
        ReserveWatches(current_watches, WATCHES_RESERVE_CHUNK);
    }
    auto& watch = current_watches[current_watch_cursor++];
    watch.upper_bound = offset;
    watch.tick = scheduler.CurrentTick();
}

// CodexAstraLocal: Called only by the owner after a real draw is queued. This
// records lifetime without copying data, adding a watch or waiting per draw.
void StreamBuffer::MarkDrawUse() noexcept {
    last_draw_use_tick = scheduler.CurrentTick();
}

// CodexAstraLocal: Coherence is an observed allocation property, not a device
// name or requested flag. No implicit flush may race a later deferred write.
bool StreamBuffer::CanDeferUpload() const noexcept {
    return is_coherent && type == BufferType::Stream && allocation && mapped &&
           mapping_generation != std::numeric_limits<u64>::max();
}

std::optional<StreamBuffer::DeferredMapping> StreamBuffer::MapDeferredUpload(u32 size,
                                                                           u64 alignment) {
    // CodexAstraLocal: Reject unsupported/empty reservations before ring state
    // changes. Ordinary Map retains its existing capacity/wait behavior.
    if (!CanDeferUpload() || size == 0 || size > stream_buffer_size)
        return std::nullopt;
    const auto [data, position, invalidated] = Map(size, alignment);
    if (!CanDeferUpload())
        return std::nullopt;
    return DeferredMapping{this, position, size, mapping_generation};
}

std::optional<StreamBuffer::DeferredUpload> StreamBuffer::CommitDeferredUpload(
    const DeferredMapping& reservation) {
    // CodexAstraLocal: Refuse before modifying the ordinary cursor on an invalid
    // or stale reservation. Caller permits no intervening geometry Map/Commit.
    const u32 size = reservation.size;
    if (!CanDeferUpload() || reservation.owner != this ||
        reservation.generation != mapping_generation || size == 0 ||
        offset != reservation.offset ||
        size > mapped_size || offset > stream_buffer_size ||
        size > stream_buffer_size - offset)
        return std::nullopt;
    DeferredUpload upload{allocation, offset, size};
    Commit(size); // Owner only: advances ring and stamps the final draw tick.
    return upload;
}

bool StreamBuffer::CopyHostWrittenBytes(u64 read_offset,
                                       std::span<u8> destination) const noexcept {
    // CodexAstraLocal: Download memory has different visibility requirements;
    // this accessor witnesses only host-written bytes, not device readback.
    if (!mapped || type == BufferType::Download || read_offset > stream_buffer_size ||
        destination.size() > stream_buffer_size - read_offset)
        return false;
    std::memcpy(destination.data(), mapped + read_offset, destination.size());
    return true;
}

void StreamBuffer::CreateBuffers(u64 preferred_size) {
    const vk::Device device = instance.GetDevice();
    const auto memory_properties = instance.GetPhysicalDevice().getMemoryProperties();
    const u32 preferred_type = GetMemoryType(memory_properties, type);
    const vk::MemoryType mem_type = memory_properties.memoryTypes[preferred_type];
    const u32 preferred_heap = mem_type.heapIndex;
    is_coherent =
        static_cast<bool>(mem_type.propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent);

    // Substract from the preferred heap size some bytes to avoid getting out of memory.
    const vk::DeviceSize heap_size = memory_properties.memoryHeaps[preferred_heap].size;
    // As per DXVK's example, using `heap_size / 2`
    const vk::DeviceSize allocable_size = heap_size / 2;

    vk::DeviceSize attempt_size = std::min(preferred_size, allocable_size);

    // Retry allocation until we reach minimum 8 KiB allocation size
    const vk::DeviceSize min_buffer_size = std::min<vk::DeviceSize>(8_KiB, attempt_size);

    while (attempt_size >= min_buffer_size) {
        try {
            // Create buffer with current attempt size
            buffer = device.createBuffer({
                .size = attempt_size,
                .usage = usage,
            });

            const auto requirements_chain = device.getBufferMemoryRequirements2<
                vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>({.buffer = buffer});

            const auto& requirements = requirements_chain.get<vk::MemoryRequirements2>();
            const auto& dedicated_requirements =
                requirements_chain.get<vk::MemoryDedicatedRequirements>();

            if (dedicated_requirements.prefersDedicatedAllocation) {
                vk::StructureChain<vk::MemoryAllocateInfo, vk::MemoryDedicatedAllocateInfo>
                    alloc_chain{};

                auto& alloc_info = alloc_chain.get<vk::MemoryAllocateInfo>();
                alloc_info.allocationSize = requirements.memoryRequirements.size;
                alloc_info.memoryTypeIndex = preferred_type;

                auto& dedicated_alloc_info = alloc_chain.get<vk::MemoryDedicatedAllocateInfo>();
                dedicated_alloc_info.buffer = buffer;

                memory = device.allocateMemory(alloc_chain.get());
            } else {
                memory = device.allocateMemory({
                    .allocationSize = requirements.memoryRequirements.size,
                    .memoryTypeIndex = preferred_type,
                });
            }
            // CodexAstraUlt: Record successful ownership before bind/map can fail;
            // updates happen only on allocation/free, never on per-draw ring use.
            allocation_bytes = requirements.memoryRequirements.size;
            instance.RecordRawStreamAllocation(allocation_bytes);

            // Allocation succeeded, bind and map
            device.bindBufferMemory(buffer, memory, 0);

            mapped = reinterpret_cast<u8*>(
                device.mapMemory(memory, 0, requirements.memoryRequirements.size));

            stream_buffer_size = static_cast<u64>(attempt_size);

            LOG_INFO(Render_Vulkan, "Created {} buffer with size {} KiB (flags {})",
                     BufferTypeName(type), stream_buffer_size / 1024,
                     vk::to_string(mem_type.propertyFlags));

            if (instance.HasDebuggingToolAttached()) {
                SetObjectName(device, buffer, "StreamBuffer({}): {} KiB {}", BufferTypeName(type),
                              stream_buffer_size / 1024, vk::to_string(mem_type.propertyFlags));
                SetObjectName(device, memory, "StreamBufferMemory({}): {} Kib {}",
                              BufferTypeName(type), stream_buffer_size / 1024,
                              vk::to_string(mem_type.propertyFlags));
            }

            // CodexAstraLocal: Transfer complete resources only after every
            // creation step succeeds. Allocation failure retains raw cleanup.
            allocation = std::make_shared<Allocation>(instance, device, buffer, memory,
                                                       mapped, allocation_bytes);
            return;
        } catch (const vk::SystemError& err) {
            // CodexAstraUlt: Replace inherited buffer-only retry cleanup, which
            // leaked successful memory allocations when bind/map failed. Unmap
            // when needed, destroy the buffer before freeing memory, then retain
            // the same halving policy for the next attempt.
            instance.RecordRawStreamFailure();
            DestroyBuffers();

            attempt_size /= 2;
        }
    }

    UNREACHABLE_MSG("Failed to allocate {} buffer of preferred size {} KiB (flags {})",
                    BufferTypeName(type), preferred_size / 1024,
                    vk::to_string(mem_type.propertyFlags));
}

void StreamBuffer::ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size) {
    watches.resize(watches.size() + grow_size);
}

void StreamBuffer::WaitPendingOperations(u64 requested_upper_bound) {
    if (!invalidation_mark) {
        return;
    }
    while (requested_upper_bound > wait_bound && wait_cursor < *invalidation_mark) {
        auto& watch = previous_watches[wait_cursor];
        wait_bound = watch.upper_bound;
        scheduler.Wait(watch.tick);
        ++wait_cursor;
    }
}

} // namespace Vulkan
