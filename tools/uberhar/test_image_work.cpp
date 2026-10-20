// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Check production region predicates and actual Vulkan RGBA8
// copy/blit pixels. This finite correctness check is not a Thor speed estimate.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "video_core/renderer_vulkan/uberhar_image_work.h"

static void Need(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
static void Check(VkResult result) { Need(result == VK_SUCCESS, "Vulkan operation failed"); }

int main() {
    try {
        using namespace Vulkan::ImageWork;
        // CodexAstraLocal: Boundary witnesses reject flipped, expanded, empty,
        // negative and overflow-sensitive regions instead of mirroring a loop.
        Need(ContainsArea(3, 5, 17, 19, 4, 6, 15, 17), "contained area rejected");
        Need(!ContainsArea(3, 5, 17, 19, 2, 6, 15, 17), "left escape accepted");
        Need(!ContainsArea(3, 5, 17, 19, 4, 6, 17, 17), "right escape accepted");
        Need(!ContainsArea(3, 5, 17, 19, 4, 6, 15, 19), "top escape accepted");
        Need(!ContainsArea(-1, 5, 17, 19, 4, 6, 15, 17), "negative area accepted");
        Need(!ContainsArea(0, 0, 8, 8, 0, 0, 0, 8), "empty area accepted");
        Need(!ContainsArea(0, 0, 8, 8, 7, 0, UINT32_MAX, 1), "wrapped extent accepted");
        Need(EqualPositiveRegions(2, 3, 10, 19, 5, 7, 13, 23), "translated copy rejected");
        Need(!EqualPositiveRegions(2, 3, 10, 19, 5, 7, 14, 23), "scaled copy accepted");
        Need(!EqualPositiveRegions(10, 3, 2, 19, 5, 7, 13, 23), "flipped copy accepted");
        Need(!EqualPositiveRegions(2, 19, 10, 3, 5, 7, 13, 23), "vertical flip accepted");

        // CodexAstraLocal: A software ICD is selected by the runner. Real image
        // transfers compare translated mip regions against their CPU texel oracle.
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        VkInstance instance{}; Check(vkCreateInstance(&instance_info, nullptr, &instance));
        std::uint32_t count{}; Check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        Need(count == 1, "select exactly one test ICD");
        VkPhysicalDevice physical{}; Check(vkEnumeratePhysicalDevices(instance, &count, &physical));
        VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical, &properties);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        std::uint32_t family = 0;
        while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
        Need(family < count, "graphics queue unavailable");
        const float priority = 1;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
        VkDevice device{}; Check(vkCreateDevice(physical, &device_info, nullptr, &device));
        VkQueue queue{}; vkGetDeviceQueue(device, family, 0, &queue);
        auto allocate = [&](VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
            std::uint32_t type = 0;
            while (type < memory.memoryTypeCount && (!(req.memoryTypeBits & (1U << type)) ||
                (memory.memoryTypes[type].propertyFlags & flags) != flags)) ++type;
            Need(type < memory.memoryTypeCount, "memory type unavailable");
            VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            info.allocationSize = req.size; info.memoryTypeIndex = type;
            VkDeviceMemory result{}; Check(vkAllocateMemory(device, &info, nullptr, &result)); return result;
        };
        constexpr std::uint32_t width = 32, height = 32, bytes = width * height * 4;
        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = bytes * 3;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buffer{}; Check(vkCreateBuffer(device, &buffer_info, nullptr, &buffer));
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device, buffer, &req);
        auto buffer_memory = allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkBindBufferMemory(device, buffer, buffer_memory, 0));
        void* mapped{}; Check(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        auto* data = static_cast<std::uint8_t*>(mapped);
        for (unsigned i = 0; i < bytes; ++i) data[i] = (i * 73 + i / 7) & 255;
        std::memset(data + bytes, 0, bytes * 2);
        std::array<VkImage, 3> images{}; std::array<VkDeviceMemory, 3> allocations{};
        for (unsigned i = 0; i < images.size(); ++i) {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = {width * 2, height * 2, 1}; info.mipLevels = 2; info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            Check(vkCreateImage(device, &info, nullptr, &images[i]));
            vkGetImageMemoryRequirements(device, images[i], &req);
            allocations[i] = allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Check(vkBindImageMemory(device, images[i], allocations[i], 0));
        }
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool_info.queueFamilyIndex = family;
        VkCommandPool pool{}; Check(vkCreateCommandPool(device, &pool_info, nullptr, &pool));
        VkCommandBufferAllocateInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_info.commandPool = pool; cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cmd_info.commandBufferCount = 1;
        VkCommandBuffer cmd{}; Check(vkAllocateCommandBuffers(device, &cmd_info, &cmd));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; Check(vkBeginCommandBuffer(cmd, &begin));
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, 1};
        for (auto image : images) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image; barrier.subresourceRange = range;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        VkBufferImageCopy upload{}; upload.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1};
        upload.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, buffer, images[0], VK_IMAGE_LAYOUT_GENERAL, 1, &upload);
        auto transfer_barrier = [&] {
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        };
        transfer_barrier();
        VkImageCopy copy{}; copy.srcSubresource = copy.dstSubresource = upload.imageSubresource;
        copy.srcOffset = {2, 3, 0}; copy.dstOffset = {5, 7, 0}; copy.extent = {17, 19, 1};
        vkCmdCopyImage(cmd, images[0], VK_IMAGE_LAYOUT_GENERAL, images[1], VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        VkImageBlit blit{}; blit.srcSubresource = blit.dstSubresource = upload.imageSubresource;
        blit.srcOffsets[0] = copy.srcOffset; blit.srcOffsets[1] = {19, 22, 1};
        blit.dstOffsets[0] = copy.dstOffset; blit.dstOffsets[1] = {22, 26, 1};
        vkCmdBlitImage(cmd, images[0], VK_IMAGE_LAYOUT_GENERAL, images[2], VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
        transfer_barrier();
        for (unsigned i = 1; i < images.size(); ++i) {
            VkBufferImageCopy read{}; read.bufferOffset = bytes * i;
            read.imageSubresource = upload.imageSubresource; read.imageOffset = copy.dstOffset;
            read.imageExtent = copy.extent;
            vkCmdCopyImageToBuffer(cmd, images[i], VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &read);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, nullptr, 0, nullptr);
        Check(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
        Check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); Check(vkQueueWaitIdle(queue));
        for (unsigned y = 0; y < 19; ++y) for (unsigned x = 0; x < 17; ++x) {
            const auto* expected = data + ((y + 3) * width + x + 2) * 4;
            const auto offset = (y * 17 + x) * 4;
            Need(std::memcmp(expected, data + bytes + offset, 4) == 0, "copy lost texels");
            Need(std::memcmp(expected, data + bytes * 2 + offset, 4) == 0, "1:1 linear blit differs");
        }
        // CodexAstraLocal: All test-only queue work completed before destruction.
        vkDestroyCommandPool(device, pool, nullptr);
        for (unsigned i = 0; i < images.size(); ++i) {
            vkDestroyImage(device, images[i], nullptr); vkFreeMemory(device, allocations[i], nullptr);
        }
        vkUnmapMemory(device, buffer_memory); vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, buffer_memory, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
        std::cout << "PASS image-work predicates and 323 RGBA8 texels; device=" << properties.deviceName << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
