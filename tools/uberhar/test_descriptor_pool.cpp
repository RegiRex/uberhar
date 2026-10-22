// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

// CodexAstraLocal: The driver inserts the actual ResourcePool/DescriptorHeap
// definitions below. Vulkan-Hpp and its unique-owner deleters stay real; only
// the Vulkan endpoints, device counters and GPU-completion source are modeled.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/shader_recovery_error.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {
unsigned checks{};
void Check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error{reason};
}
template <typename T>
T Handle(u64 value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
    else return static_cast<T>(value);
}
struct Pool {
    u32 capacity{}, used{};
    std::map<VkDescriptorType, u64> capacity_by_type, used_by_type;
};
struct Driver {
    u64 next{256};
    unsigned allocations{}, creations{}, layout_creations{}, destructions{};
    unsigned allocation_bound{64};
    std::map<VkDescriptorSetLayout, std::vector<VkDescriptorSetLayoutBinding>> layouts;
    std::map<VkDescriptorPool, Pool> pools;
    std::map<VkDescriptorSet, VkDescriptorPool> sets;
    std::vector<VkDescriptorPool> attempted_pools, destroyed_pools;
    std::deque<VkResult> allocate_results, create_results, layout_results;
    std::vector<Pool> created_contracts;
    static VkResult Pop(std::deque<VkResult>& results) {
        if (results.empty()) return VK_SUCCESS;
        const auto result = results.front();
        results.pop_front();
        return result;
    }
} driver;

// CodexAstraLocal: Enforce both maxSets and every descriptor-type/array budget;
// permissive software Vulkan drivers did not expose the original 64/16 defect.
VKAPI_ATTR VkResult VKAPI_CALL CreateLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo* info,
                                           const VkAllocationCallbacks*, VkDescriptorSetLayout* out) {
    ++driver.layout_creations;
    if (const auto result = Driver::Pop(driver.layout_results); result != VK_SUCCESS) return result;
    *out = Handle<VkDescriptorSetLayout>(driver.next++);
    driver.layouts[*out] = {info->pBindings, info->pBindings + info->bindingCount};
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroyLayout(VkDevice, VkDescriptorSetLayout layout,
                                        const VkAllocationCallbacks*) {
    driver.layouts.erase(layout);
}
VKAPI_ATTR VkResult VKAPI_CALL CreatePool(VkDevice, const VkDescriptorPoolCreateInfo* info,
                                         const VkAllocationCallbacks*, VkDescriptorPool* out) {
    ++driver.creations;
    if (const auto result = Driver::Pop(driver.create_results); result != VK_SUCCESS) return result;
    Pool pool{info->maxSets};
    for (u32 i = 0; i < info->poolSizeCount; ++i)
        pool.capacity_by_type[info->pPoolSizes[i].type] += info->pPoolSizes[i].descriptorCount;
    driver.created_contracts.push_back(pool);
    *out = Handle<VkDescriptorPool>(driver.next++);
    driver.pools[*out] = std::move(pool);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroyPool(VkDevice, VkDescriptorPool pool,
                                      const VkAllocationCallbacks*) {
    ++driver.destructions;
    driver.destroyed_pools.push_back(pool);
    driver.pools.erase(pool);
    std::erase_if(driver.sets, [pool](const auto& entry) { return entry.second == pool; });
}
VKAPI_ATTR VkResult VKAPI_CALL AllocateSets(VkDevice, const VkDescriptorSetAllocateInfo* info,
                                           VkDescriptorSet* out) {
    if (++driver.allocations > driver.allocation_bound) {
        std::fputs("FAIL: driver allocation call bound exceeded\n", stderr);
        std::abort();
    }
    driver.attempted_pools.push_back(info->descriptorPool);
    std::fill_n(out, info->descriptorSetCount, VK_NULL_HANDLE);
    if (const auto result = Driver::Pop(driver.allocate_results); result != VK_SUCCESS) return result;
    auto found = driver.pools.find(info->descriptorPool);
    if (found == driver.pools.end()) return VK_ERROR_UNKNOWN;
    auto& pool = found->second;
    std::map<VkDescriptorType, u64> needed;
    for (u32 i = 0; i < info->descriptorSetCount; ++i) {
        for (const auto& binding : driver.layouts.at(info->pSetLayouts[i]))
            needed[binding.descriptorType] += binding.descriptorCount;
    }
    if (info->descriptorSetCount > pool.capacity - pool.used) return VK_ERROR_OUT_OF_POOL_MEMORY;
    for (const auto& [type, count] : needed)
        if (count > pool.capacity_by_type[type] - pool.used_by_type[type])
            return VK_ERROR_OUT_OF_POOL_MEMORY;
    pool.used += info->descriptorSetCount;
    for (const auto& [type, count] : needed) pool.used_by_type[type] += count;
    for (u32 i = 0; i < info->descriptorSetCount; ++i) {
        out[i] = Handle<VkDescriptorSet>(driver.next++);
        driver.sets[out[i]] = info->descriptorPool;
    }
    return VK_SUCCESS;
}
void Reset() {
    Check(driver.layouts.empty() && driver.pools.empty() && driver.sets.empty(),
          "all previous unique owners released");
    driver = {};
}
template <typename F>
std::string Terminal(F&& operation) {
    try {
        operation();
    } catch (const VideoCore::ShaderRecoveryError& error) {
        const std::string message{error.what()};
        Check(!message.empty() && message.size() < 1024, "bounded typed terminal reason");
        return message;
    }
    throw std::runtime_error{"expected ShaderRecoveryError before publication"};
}
} // namespace

namespace Vulkan {
// CodexAstraLocal: Synthetic completion values exercise the real fence search;
// instance counters independently track successful ownership publication/free.
class MasterSemaphore {
public:
    u64 known{}, completion{}, current{2};
    unsigned refreshes{};
    u64 KnownGpuTick() const { return known; }
    u64 CurrentTick() const { return current; }
    void Refresh() { ++refreshes; known = completion; }
};
class Instance {
public:
    mutable u64 pools{}, sets{}, pool_allocations{}, pool_frees{}, set_allocations{}, set_frees{};
    vk::Device GetDevice() const { return vk::Device{Handle<VkDevice>(1)}; }
    bool HasDebuggingToolAttached() const { return false; }
    void RecordDescriptorPoolAllocation() const noexcept { ++pools; ++pool_allocations; }
    void RecordDescriptorPoolFree() const noexcept { --pools; ++pool_frees; }
    void RecordDescriptorSetAllocation(u64 n) const noexcept { sets += n; set_allocations += n; }
    void RecordDescriptorSetFree(u64 n) const noexcept { sets -= n; set_frees += n; }
};
// CodexAstraLocal: Debug naming is disabled in this endpoint fixture; its adapter
// accepts the real call shape without creating a logging/debug-utils dependency.
template <typename... T> void SetObjectName(T&&...) {}
#define ASSERT(condition) Check(condition, "production allocation batch invariant")
// UBERHAR_RESOURCE_METHODS
// UBERHAR_DESCRIPTOR_METHODS
#undef ASSERT

// CodexAstraLocal: Inspect the protected fence contract through an ordinary
// derived owner, without exposing DescriptorHeap internals or modeling its loop.
class FaultPool final : public ResourcePool {
public:
    explicit FaultPool(MasterSemaphore& sem) : ResourcePool{&sem, 4} {}
    bool fail{true};
    using ResourcePool::CommitResource;
    void Seed() { ticks = {7, 8, 9, 10}; hint_iterator = 2; }
    void Allocate(std::size_t, std::size_t) override {
        if (fail) throw VideoCore::ShaderRecoveryError{"controlled allocation failure"};
    }
    bool Unchanged() const { return ticks == std::vector<u64>{7, 8, 9, 10} && hint_iterator == 2; }
};
} // namespace Vulkan

namespace {
using namespace Vulkan;
constexpr std::array simple_bindings{
    vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eCompute},
    vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eCompute},
    vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1,
                                   vk::ShaderStageFlagBits::eCompute}};

void CheckContract(std::span<const vk::DescriptorSetLayoutBinding> bindings, u32 hint, u32 expected) {
    Reset();
    Instance instance;
    MasterSemaphore sem;
    {
        DescriptorHeap heap{instance, &sem, bindings, hint};
        Check(driver.created_contracts.at(0).capacity == expected, "exact effective set capacity");
        std::map<VkDescriptorType, u64> per_set;
        for (const auto& binding : bindings)
            per_set[static_cast<VkDescriptorType>(binding.descriptorType)] += binding.descriptorCount;
        for (const auto& [type, count] : per_set)
            Check(driver.created_contracts.at(0).capacity_by_type[type] == count * expected,
                  "exact type capacity includes every array element");
        const auto first = heap.Commit();
        Check(static_cast<bool>(first), "first actual descriptor batch exists");
        for (unsigned i = 1; i < 64; ++i) Check(heap.Commit() != first, "distinct live set");
        Check(driver.allocations == 1 && instance.sets == 64,
              "one driver allocation fills exactly 64 published slots");
        Check(driver.creations == 1, "valid initial batch needs no fresh retry");
        sem.known = sem.completion = 2;
        sem.current = 3;
        Check(heap.Commit() == first, "completed set reused with original handle");
        Check(driver.allocations == 1, "completed reuse never allocates");
    }
    Check(instance.pools == 0 && instance.sets == 0, "successful owners and sets retired exactly");
}
void ProductionContracts() {
    // CodexAstraLocal: Exact binding declarations and hint expressions are
    // inserted from actual graphics, rectangle, blit and presentation owners.
    // UBERHAR_OWNER_CONTRACTS
}

void FailureControls() {
    // CodexAstraLocal: Failure after existing live batches may not remove their
    // pools/ticks or leave resized null slots available to a subsequent Commit.
    for (const auto result : {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                              VK_ERROR_DEVICE_LOST, VK_ERROR_UNKNOWN}) {
        Reset();
        Instance instance;
        MasterSemaphore sem;
        {
            DescriptorHeap heap{instance, &sem, simple_bindings, 16};
            const auto first = heap.Commit();
            for (unsigned i = 1; i < 64; ++i) heap.Commit();
            const auto live_pool = driver.sets.at(static_cast<VkDescriptorSet>(first));
            driver.allocate_results = {result};
            const auto before = driver.allocations;
            const auto text = Terminal([&] { heap.Commit(); });
            Check(driver.allocations == before + 1 && driver.creations == 1,
                  "non-pool driver error is terminal without retry");
            Check(text.find("request_sets=64") != std::string::npos &&
                  text.find("pool_sets=64") != std::string::npos &&
                  text.find("type_budgets=7:192,") != std::string::npos &&
                  text.find("fresh_pool=0") != std::string::npos,
                  "terminal reason identifies capacity and attempt contract");
            Check(driver.pools.contains(live_pool) && instance.sets == 64 && instance.pools == 1,
                  "terminal error preserves earlier live owners and ledger");
            const auto next = heap.Commit();
            Check(static_cast<bool>(next) && next != first && instance.sets == 128,
                  "failed slots were rolled back before a future allocation");
        }
        Check(instance.pools == 0 && instance.sets == 0, "failure plus later growth retires once");
    }
    for (const auto result : {VK_ERROR_OUT_OF_POOL_MEMORY, VK_ERROR_FRAGMENTED_POOL}) {
        Reset();
        Instance instance;
        MasterSemaphore sem;
        {
            DescriptorHeap heap{instance, &sem, simple_bindings, 16};
            for (unsigned i = 0; i < 128; ++i) heap.Commit();
            Check(driver.pools.size() == 2 && instance.sets == 128, "two live retained owners");
            const auto retained = driver.pools;
            driver.allocate_results = {result, result, result};
            const auto before = driver.allocations;
            const auto text = Terminal([&] { heap.Commit(); });
            Check(driver.allocations == before + 3 && driver.creations == 3,
                  "each retained pool and one fresh pool attempted once");
            Check(driver.attempted_pools[before] != driver.attempted_pools[before + 1],
                  "retained attempts advance to the next owner");
            Check(driver.destructions == 1 && driver.pools.size() == 2 && instance.pools == 2 &&
                  instance.sets == 128, "only unpublished fresh pool is rolled back");
            for (const auto& [pool, state] : retained)
                Check(driver.pools.contains(pool) && driver.pools.at(pool).used == state.used,
                      "retained live pool state survives fresh failure");
            Check(text.find("retained_attempts=2 fresh_pool=1") != std::string::npos,
                  "last exhausted attempt is actionable and bounded");
            sem.known = sem.completion = 2;
            sem.current = 3;
            Check(static_cast<bool>(heap.Commit()) && driver.allocations == before + 3,
                  "live retained set remains reusable after a failed growth");
        }
        Check(instance.pool_allocations == instance.pool_frees &&
              instance.set_allocations == instance.set_frees,
              "rollback and final retirement have balanced publication counters");
    }

    // CodexAstraLocal: A later retained pool can succeed after fragmentation;
    // recovery must not create a fresh pool or discard previously live sets.
    Reset();
    {
        Instance instance;
        MasterSemaphore sem;
        DescriptorHeap heap{instance, &sem, simple_bindings, 32};
        driver.allocate_results = {VK_ERROR_FRAGMENTED_POOL, VK_SUCCESS};
        for (unsigned i = 0; i < 64; ++i) heap.Commit();
        driver.allocate_results = {VK_ERROR_FRAGMENTED_POOL, VK_SUCCESS};
        const auto before = driver.allocations;
        Check(static_cast<bool>(heap.Commit()) && driver.allocations == before + 2 &&
              driver.creations == 2, "second retained pool succeeds without another fresh owner");
    }

    // CodexAstraLocal: Driver creation failures are also typed, finite and do
    // not publish null owners, even when a previous descriptor set is live.
    for (bool layout : {true, false}) {
        Reset();
        Instance instance;
        MasterSemaphore sem;
        if (layout) driver.layout_results = {VK_ERROR_OUT_OF_HOST_MEMORY};
        else driver.create_results = {VK_ERROR_OUT_OF_DEVICE_MEMORY};
        Terminal([&] { DescriptorHeap heap{instance, &sem, simple_bindings, 4}; });
        Check(instance.pools == 0 && instance.sets == 0 && driver.layouts.empty() &&
              driver.pools.empty(), "failed construction cleans up unpublished unique owners");
    }
    Reset();
    {
        Instance instance;
        MasterSemaphore sem;
        DescriptorHeap heap{instance, &sem, simple_bindings, 16};
        const auto first = heap.Commit();
        for (unsigned i = 1; i < 64; ++i) heap.Commit();
        driver.create_results = {VK_ERROR_OUT_OF_HOST_MEMORY};
        Terminal([&] { heap.Commit(); });
        Check(driver.pools.size() == 1 && driver.destructions == 0 && instance.pools == 1 &&
              instance.sets == 64, "failed fresh creation leaves only the original live owner");
        Check(static_cast<bool>(heap.Commit()) && instance.pools == 2 &&
              driver.sets.contains(static_cast<VkDescriptorSet>(first)),
              "unpublished creation failure leaves no null pool/slot for next Commit");
    }

    Reset();
    {
        MasterSemaphore sem;
        FaultPool pool{sem};
        pool.Seed();
        Terminal([&] { pool.CommitResource(); });
        Check(pool.Unchanged(), "failed growth restores all old ticks and search hint");
        pool.fail = false;
        Check(pool.CommitResource() == 4, "retry starts at original unpublished boundary");
    }
}
} // namespace

int main() {
    auto& dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;
    dispatcher.vkCreateDescriptorSetLayout = CreateLayout;
    dispatcher.vkDestroyDescriptorSetLayout = DestroyLayout;
    dispatcher.vkCreateDescriptorPool = CreatePool;
    dispatcher.vkDestroyDescriptorPool = DestroyPool;
    dispatcher.vkAllocateDescriptorSets = AllocateSets;
    try {
        ProductionContracts();
        // CodexAstraLocal: Surviving consumers use hints of at least 16. Exercise
        // the shared 64-set minimum with a nonzero small hint before zero, so
        // removing that minimum fails its capacity assertion before overflow math.
        for (const u32 hint : {4U, 0U, 1U, 16U, 32U, 1024U})
            CheckContract(simple_bindings, hint, hint < 16 ? 64 : hint * 4);
        auto arrays = simple_bindings;
        arrays[0].descriptorCount = 65536;
        arrays[1].descriptorCount = 3;
        arrays[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        arrays[2].descriptorCount = 5;
        CheckContract(arrays, 4, 64);
        // CodexAstraLocal: Invalid arithmetic fails before any Vulkan allocation;
        // individual arrays and the sum of same-type bindings are both checked.
        for (unsigned kind = 0; kind < 3; ++kind) {
            Reset();
            Instance instance;
            MasterSemaphore sem;
            auto bindings = simple_bindings;
            u32 hint = 4;
            if (kind == 0) hint = std::numeric_limits<u32>::max();
            if (kind == 1) bindings[0].descriptorCount = std::numeric_limits<u32>::max();
            if (kind == 2) bindings[0].descriptorCount = bindings[1].descriptorCount = 40000000;
            Terminal([&] { DescriptorHeap heap{instance, &sem, bindings, hint}; });
            Check(driver.creations == 0 && driver.layout_creations == 0,
                  "overflow is rejected before creating Vulkan objects");
        }
        FailureControls();
        Reset();
        std::printf("Descriptor pool: %u checks; actual production methods, strict modeled Vulkan endpoints\n",
                    checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL after %u checks: %s\n", checks, error.what());
        return 1;
    }
}
