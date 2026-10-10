// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <memory> // CodexAstraLocal: Queued coherent uploads retain their allocation.
#include <span>
#include <tuple>
#include <vector>
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

enum class BufferType : u32 {
    Upload = 0,
    Download = 1,
    Stream = 2,
};

class Instance;
class Scheduler;

class StreamBuffer final {
    static constexpr std::size_t MAX_BUFFER_VIEWS = 3;

    // CodexAstraLocal: One allocation owner is shared only with deferred upload
    // commands. GPU retirement still uses the existing scheduler/tick contract.
    class Allocation;

public:
    // CodexAstraLocal: A reservation is bound to one stream and Map generation;
    // ring wrap cannot turn an old offset into a valid current reservation.
    class DeferredMapping final {
    private:
        friend class StreamBuffer;
        DeferredMapping(const StreamBuffer* owner_, u32 offset_, u32 size_, u64 generation_)
            : owner{owner_}, offset{offset_}, size{size_}, generation{generation_} {}
        const StreamBuffer* owner;
        u32 offset;
        u32 size;
        u64 generation;
    };

    // CodexAstraLocal: A move-only command payload captures a sealed range. Only
    // the ordered Vulkan worker publishes bytes; it never mutates ring cursors.
    class DeferredUpload final {
    public:
        DeferredUpload(DeferredUpload&&) noexcept = default;
        DeferredUpload& operator=(DeferredUpload&&) noexcept = default;
        DeferredUpload(const DeferredUpload&) = delete;
        DeferredUpload& operator=(const DeferredUpload&) = delete;
        bool Publish(std::span<const u8> source) const noexcept;
        vk::Buffer Handle() const noexcept;
        u32 Offset() const noexcept { return offset; }

    private:
        friend class StreamBuffer;
        DeferredUpload(std::shared_ptr<Allocation> owner_, u32 offset_, u32 size_)
            : owner{std::move(owner_)}, offset{offset_}, size{size_} {}
        std::shared_ptr<Allocation> owner;
        u32 offset{};
        u32 size{};
        mutable bool published{}; // Single Vulkan-worker consumer, never shared between threads.
    };

    explicit StreamBuffer(const Instance& instance, Scheduler& scheduler,
                          vk::BufferUsageFlags usage, u64 size,
                          BufferType type = BufferType::Stream);
    ~StreamBuffer();

    /**
     * Reserves a region of memory from the stream buffer.
     * @param size Size to reserve.
     * @returns A pair of a raw memory pointer (with offset added), and the buffer offset
     */
    std::tuple<u8*, u32, bool> Map(u32 size, u64 alignment);

    /// Ensures that "size" bytes of memory are available to the GPU, potentially recording a copy.
    void Commit(u32 size);

    // CodexAstraLocal: Cached shader ranges can be consumed without a new
    // Commit. The owner calls this after the actual draw enqueue, at its final
    // tick, so wrap cannot overwrite bytes still referenced by that draw.
    void MarkDrawUse() noexcept;

    // CodexAstraLocal: Admission must happen before Map. The normal owner Map
    // reserves the range; seal it after final pipeline binding at the draw tick.
    // Noncoherent/download buffers retain synchronous Commit and are not admitted.
    bool CanDeferUpload() const noexcept;
    std::optional<DeferredMapping> MapDeferredUpload(u32 size, u64 alignment);
    std::optional<DeferredUpload> CommitDeferredUpload(const DeferredMapping& reservation);

    // CodexAstraLocal: Copy bounded already-written upload bytes for an armed
    // diagnostic. This never maps, invalidates, flushes, commits or waits.
    bool CopyHostWrittenBytes(u64 read_offset, std::span<u8> destination) const noexcept;

    vk::Buffer Handle() const noexcept {
        return buffer;
    }

private:
    struct Watch {
        u64 tick{};
        u64 upper_bound{};
    };

    /// Creates Vulkan buffer handles committing the required the required memory.
    void CreateBuffers(u64 prefered_size);

    // CodexAstraUlt: Release partial failed attempts and complete owned buffers
    // through the same ordered cleanup, including their raw allocation ledger.
    void DestroyBuffers() noexcept;

    /// Increases the amount of watches available.
    void ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size);

    void WaitPendingOperations(u64 requested_upper_bound);

private:
    const Instance& instance; ///< Vulkan instance.
    Scheduler& scheduler;     ///< Command scheduler.

    vk::Device device;
    vk::Buffer buffer;        ///< Mapped buffer.
    vk::DeviceMemory memory;  ///< Memory allocation.
    u8* mapped{};             ///< Pointer to the mapped memory
    // CodexAstraLocal: Raw aliases above preserve ordinary ring operations;
    // complete allocations are released by this owner after the last command lease.
    std::shared_ptr<Allocation> allocation;
    u64 stream_buffer_size{}; ///< Stream buffer size.
    // CodexAstraUlt: Actual VkDeviceMemory requirement size, not requested buffer
    // capacity; VMA does not own these allocations.
    u64 allocation_bytes{};
    vk::BufferUsageFlags usage{};
    BufferType type;

    u32 offset{};       ///< Buffer iterator.
    u32 mapped_size{};  ///< Size reserved for the current copy.
    bool is_coherent{}; ///< True if the buffer is coherent
    // CodexAstraLocal: Saturation permanently closes deferred admission instead
    // of allowing a stale reservation to become current after integer wrap.
    u64 mapping_generation{};

    // CodexAstraLocal: A conservative last-consumer fence protects every cached
    // range in this ring generation. Unmarked geometry/upload rings are unchanged.
    u64 last_draw_use_tick{};

    std::vector<Watch> current_watches;           ///< Watches recorded in the current iteration.
    std::size_t current_watch_cursor{};           ///< Count of watches, reset on invalidation.
    std::optional<std::size_t> invalidation_mark; ///< Number of watches used in the previous cycle.

    std::vector<Watch> previous_watches; ///< Watches used in the previous iteration.
    std::size_t wait_cursor{};           ///< Last watch being waited for completion.
    u64 wait_bound{};                    ///< Highest offset being watched for completion.
};

} // namespace Vulkan
