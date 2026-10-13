// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/shader_recovery_error.h"

namespace Vulkan {

ResourcePool::ResourcePool(MasterSemaphore* master_semaphore_, std::size_t grow_step_)
    : master_semaphore{master_semaphore_}, grow_step{grow_step_} {}

std::size_t ResourcePool::CommitResource() {
    u64 gpu_tick = master_semaphore->KnownGpuTick();
    // CodexAstraUlt: Replace the inherited copied tick: refresh must make completed
    // slots reusable in this search instead of retaining an unnecessary pool batch.
    const auto search = [this, &gpu_tick](std::size_t begin,
                                         std::size_t end) -> std::optional<std::size_t> {
        for (std::size_t iterator = begin; iterator < end; ++iterator) {
            if (gpu_tick >= ticks[iterator]) {
                ticks[iterator] = master_semaphore->CurrentTick();
                return iterator;
            }
        }
        return std::nullopt;
    };

    // Try to find a free resource from the hinted position to the end.
    auto found = search(hint_iterator, ticks.size());
    if (!found) {
        // Refresh semaphore to query updated results
        master_semaphore->Refresh();
        gpu_tick = master_semaphore->KnownGpuTick();
        found = search(hint_iterator, ticks.size());
    }
    if (!found) {
        // Search from beginning to the hinted position.
        found = search(0, hint_iterator);
        if (!found) {
            // Both searches failed, the pool is full; handle it.
            const std::size_t free_resource = ManageOverflow();

            ticks[free_resource] = master_semaphore->CurrentTick();
            found = free_resource;
        }
    }

    // Free iterator is hinted to the resource after the one that's been commited.
    hint_iterator = (*found + 1) % ticks.size();
    return *found;
}

std::size_t ResourcePool::ManageOverflow() {
    const std::size_t old_capacity = ticks.size();
    // CodexAstraLocal: New fence slots are unpublished until their handles exist.
    // A failed growth must leave the previous live ticks and search hint intact.
    if (grow_step == 0 || grow_step > ticks.max_size() - old_capacity) {
        throw VideoCore::ShaderRecoveryError{"Vulkan resource-pool capacity overflow"};
    }
    try {
        ticks.resize(old_capacity + grow_step);
        Allocate(old_capacity, old_capacity + grow_step);
    } catch (const std::bad_alloc&) {
        ticks.resize(old_capacity);
        throw VideoCore::ShaderRecoveryError{"Vulkan resource-pool bookkeeping allocation failed"};
    } catch (...) {
        ticks.resize(old_capacity);
        throw;
    }
    return old_capacity;
}

constexpr std::size_t COMMAND_BUFFER_POOL_SIZE = 4;

CommandPool::CommandPool(const Instance& instance, MasterSemaphore* master_semaphore)
    : ResourcePool{master_semaphore, COMMAND_BUFFER_POOL_SIZE}, instance{instance} {
    const vk::CommandPoolCreateInfo pool_create_info = {
        .flags = vk::CommandPoolCreateFlagBits::eTransient |
                 vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
    };
    const vk::Device device = instance.GetDevice();
    cmd_pool = device.createCommandPoolUnique(pool_create_info);
    if (instance.HasDebuggingToolAttached()) {
        SetObjectName(device, *cmd_pool, "CommandPool");
    }
}

CommandPool::~CommandPool() {
    // CodexAstraUlt: Retire the successful handle count after destroying its owner;
    // these diagnostics describe retained capacity, not in-flight work or bytes.
    cmd_pool.reset();
    if (allocated_command_buffers != 0) {
        instance.RecordCommandBufferFree(allocated_command_buffers);
    }
}

void CommandPool::Allocate(std::size_t begin, std::size_t end) {
    cmd_buffers.resize(end);

    const vk::CommandBufferAllocateInfo buffer_alloc_info = {
        .commandPool = *cmd_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = COMMAND_BUFFER_POOL_SIZE,
    };

    const vk::Device device = instance.GetDevice();
    const auto result =
        device.allocateCommandBuffers(&buffer_alloc_info, cmd_buffers.data() + begin);
    ASSERT(result == vk::Result::eSuccess);
    // CodexAstraUlt: Publish once per successful batch, never per resource reuse.
    if (result == vk::Result::eSuccess) {
        allocated_command_buffers += COMMAND_BUFFER_POOL_SIZE;
        instance.RecordCommandBufferAllocation(COMMAND_BUFFER_POOL_SIZE);
    }

    if (instance.HasDebuggingToolAttached()) {
        for (std::size_t i = begin; i < end; ++i) {
            SetObjectName(device, cmd_buffers[i], "CommandPool: Command Buffer {}", i);
        }
    }
}

vk::CommandBuffer CommandPool::Commit() {
    const std::size_t index = CommitResource();
    return cmd_buffers[index];
}

constexpr u32 DESCRIPTOR_SET_BATCH = 64;
constexpr u32 DESCRIPTOR_MULTIPLIER = 4; // Increase capacity of each pool

DescriptorHeap::DescriptorHeap(const Instance& instance, MasterSemaphore* master_semaphore,
                               std::span<const vk::DescriptorSetLayoutBinding> bindings,
                               u32 descriptor_heap_count_)
    : ResourcePool{master_semaphore, DESCRIPTOR_SET_BATCH}, instance{instance},
      device{instance.GetDevice()}, descriptor_heap_count{} {
    // CodexAstraLocal: Every pool must fit the actual 64-set allocation, including
    // each binding's array length. Preserve larger caller hints and reject all
    // narrowing/overflow before creating any Vulkan object.
    const u64 capacity = std::max<u64>(DESCRIPTOR_SET_BATCH,
                                     u64{descriptor_heap_count_} * DESCRIPTOR_MULTIPLIER);
    if (capacity > std::numeric_limits<u32>::max() ||
        bindings.size() > std::numeric_limits<u32>::max()) {
        throw VideoCore::ShaderRecoveryError{"Vulkan descriptor-pool set capacity overflow"};
    }
    descriptor_heap_count = static_cast<u32>(capacity);
    try {
        std::unordered_map<vk::DescriptorType, u64> descriptor_type_counts;
        for (const auto& binding : bindings) {
            auto& count = descriptor_type_counts[binding.descriptorType];
            if (binding.descriptorCount > std::numeric_limits<u32>::max() / capacity - count) {
                throw VideoCore::ShaderRecoveryError{
                    "Vulkan descriptor-pool type/array capacity overflow"};
            }
            count += binding.descriptorCount;
        }
        for (const auto& [type, count] : descriptor_type_counts) {
            if (count != 0) {
                pool_sizes.push_back({type, static_cast<u32>(count * capacity)});
            }
        }
    } catch (const std::bad_alloc&) {
        throw VideoCore::ShaderRecoveryError{"Vulkan descriptor-pool budget allocation failed"};
    }

    const vk::DescriptorSetLayoutCreateInfo layout_ci = {
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    try {
        descriptor_set_layout = device.createDescriptorSetLayoutUnique(layout_ci);
    } catch (const vk::SystemError& error) {
        AllocationFailed("create-layout", static_cast<vk::Result>(error.code().value()), 0, false);
    }
    if (instance.HasDebuggingToolAttached()) {
        SetObjectName(device, *descriptor_set_layout, "DescriptorSetLayout");
    }

    // Create descriptor pool
    AppendDescriptorPool();
}

DescriptorHeap::~DescriptorHeap() {
    // CodexAstraUlt: Pool destruction frees its sets. Null entries from failed
    // creation were never counted; release only successfully created owners.
    for (auto& pool : pools) {
        if (pool) {
            pool.reset();
            instance.RecordDescriptorPoolFree();
        }
    }
    if (allocated_descriptor_sets != 0) {
        instance.RecordDescriptorSetFree(allocated_descriptor_sets);
    }
}

void DescriptorHeap::Allocate(std::size_t begin, std::size_t end) {
    ASSERT(end - begin == DESCRIPTOR_SET_BATCH);
    const std::size_t old_size = descriptor_sets.size();
    const std::size_t retained_pools = pools.size();
    descriptor_sets.resize(end);

    std::array<vk::DescriptorSetLayout, DESCRIPTOR_SET_BATCH> layouts;
    layouts.fill(*descriptor_set_layout);

    vk::DescriptorSetAllocateInfo alloc_info = {
        .descriptorSetCount = DESCRIPTOR_SET_BATCH,
        .pSetLayouts = layouts.data(),
    };

    // CodexAstraLocal: Visit each retained pool once and at most one fresh pool.
    // Only pool exhaustion/fragmentation permits recovery; other device failures
    // terminate before publishing handles or repeatedly consuming mappings.
    try {
        for (std::size_t attempt = 0; attempt <= retained_pools; ++attempt) {
            const bool fresh_pool = attempt == retained_pools;
            if (fresh_pool) {
                AppendDescriptorPool(retained_pools, true);
            }
            alloc_info.descriptorPool = *pools[attempt];
            const auto result =
                device.allocateDescriptorSets(&alloc_info, descriptor_sets.data() + begin);
            if (result == vk::Result::eSuccess) {
                // CodexAstraUlt: Retained set capacity excludes unsuccessful resized slots.
                allocated_descriptor_sets += DESCRIPTOR_SET_BATCH;
                instance.RecordDescriptorSetAllocation(DESCRIPTOR_SET_BATCH);
                return;
            }
            if (fresh_pool || (result != vk::Result::eErrorOutOfPoolMemory &&
                               result != vk::Result::eErrorFragmentedPool)) {
                AllocationFailed("allocate-sets", result,
                                 fresh_pool ? retained_pools : attempt + 1, fresh_pool);
            }
        }
    } catch (...) {
        // CodexAstraLocal: Only this unpublished attempt's slots/new empty pool
        // may be rolled back. Earlier pools and in-flight descriptor sets survive.
        descriptor_sets.resize(old_size);
        if (pools.size() > retained_pools) {
            pools.back().reset();
            pools.pop_back();
            instance.RecordDescriptorPoolFree();
        }
        throw;
    }
}

vk::DescriptorSet DescriptorHeap::Commit() {
    const std::size_t index = CommitResource();
    return descriptor_sets[index];
}

void DescriptorHeap::AppendDescriptorPool(std::size_t retained_attempts, bool fresh_pool) {
    const vk::DescriptorPoolCreateInfo pool_info = {
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = descriptor_heap_count,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };
    // CodexAstraLocal: Reserve bookkeeping first, then publish a successfully
    // created unique owner. Failed creation never appends a null pool entry.
    try {
        pools.reserve(pools.size() + 1);
    } catch (const std::bad_alloc&) {
        throw VideoCore::ShaderRecoveryError{"Vulkan descriptor-pool owner allocation failed"};
    }
    vk::UniqueDescriptorPool pool;
    try {
        pool = device.createDescriptorPoolUnique(pool_info);
    } catch (const vk::SystemError& error) {
        AllocationFailed("create-pool", static_cast<vk::Result>(error.code().value()),
                         retained_attempts, fresh_pool);
    }
    pools.emplace_back(std::move(pool));
    // CodexAstraUlt: Publish only after Vulkan returns an owned pool successfully.
    instance.RecordDescriptorPoolAllocation();
}

void DescriptorHeap::AllocationFailed(const char* operation, vk::Result result,
                                      std::size_t retained_attempts, bool fresh_pool) const {
    // CodexAstraLocal: One bounded permanent terminal reason includes the actual
    // driver result and capacity contract. The renderer recovery owner logs it;
    // there is no per-attempt log or success-path formatting/allocation here.
    std::array<char, 1024> message{};
    const int prefix = std::snprintf(
        message.data(), message.size(),
        "Vulkan descriptor %s failed: result=%d request_sets=%u pool_sets=%u "
        "retained_attempts=%zu fresh_pool=%u type_budgets=",
        operation, static_cast<int>(result), DESCRIPTOR_SET_BATCH, descriptor_heap_count,
        retained_attempts, static_cast<unsigned>(fresh_pool));
    std::size_t used = prefix > 0 ? std::min<std::size_t>(prefix, message.size() - 1) : 0;
    for (const auto& size : pool_sizes) {
        const int written = std::snprintf(message.data() + used, message.size() - used, "%d:%u,",
                                          static_cast<int>(size.type), size.descriptorCount);
        if (written <= 0 || static_cast<std::size_t>(written) >= message.size() - used) {
            break;
        }
        used += written;
    }
    throw VideoCore::ShaderRecoveryError{message.data()};
}

} // namespace Vulkan
