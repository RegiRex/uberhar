// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version. Refer to license.txt.
// CodexAstraLocal: Real integer-fragment consumers of immutable image descriptor
// payloads. Vulkan setup is reused from test_cached_buffer_lifetime_vulkan.cpp;
// owner/tick/epoch seams remain controlled, not a full app or performance proof.
// CodexAstraLocal: Match vk_common.h's beta declarations before the C header's
// include guard closes; the policy still uses the actual Vulkan-Hpp types.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "common/common_types.h"
static void Require(bool value,const char* reason);
#include "video_core/renderer_vulkan/uberhar_image_descriptors.h"
static unsigned checks{}, rendered{};
static void Require(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
static void VkCheck(VkResult value, const char* reason) {
    // CodexAstraLocal: On a device failure/timeout, do not destroy resources
    // that an unresponsive queue might still own during stack unwinding.
    if (value != VK_SUCCESS) {
        std::cerr << "VULKAN_FAILURE " << reason << ' ' << value << std::endl;
        std::_Exit(24);
    }
}
#define ASSERT(x) Require(bool(x), "production assertion: " #x)
#define ASSERT_MSG(x, ...) ASSERT(x)

struct Runtime {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkCommandPool pool{};
    VkRenderPass pass{};
    VkImage image{};
    VkDeviceMemory image_memory{};
    VkImageView image_view{};
    VkFramebuffer framebuffer{};
    VkDescriptorSetLayout set_layout{};
    VkDescriptorPool descriptor_pool{};
    VkDescriptorSet first_set{};
    unsigned allocations{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkBuffer readback{};
    VkDeviceMemory readback_memory{};
    u32* pixels{};
    struct Texture {VkImage image{};VkDeviceMemory memory{};std::array<VkImageView,6> views{};};
    std::array<Texture,2> textures{};
    std::array<VkSampler,2> samplers{};
    VkImageLayout texture_layout{VK_IMAGE_LAYOUT_UNDEFINED};
    u32 queue_family{};

    u32 MemoryType(u32 bits, VkMemoryPropertyFlags flags) {
        for (u32 i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1U << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("required coherent/device memory type unavailable");
    }
    void MakeBuffer(u64 size, VkBufferUsageFlags usage, VkBuffer& buffer,
                    VkDeviceMemory& allocation, u8*& mapped) {
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkCheck(vkCreateBuffer(device, &ci, nullptr, &buffer), "create buffer");
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device, buffer, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkCheck(vkAllocateMemory(device, &ai, nullptr, &allocation), "allocate coherent buffer");
        VkCheck(vkBindBufferMemory(device, buffer, allocation, 0), "bind buffer");
        void* pointer{};
        VkCheck(vkMapMemory(device, allocation, 0, VK_WHOLE_SIZE, 0, &pointer), "map buffer");
        mapped = static_cast<u8*>(pointer); std::memset(mapped, 0, size);
    }
    VkShaderModule Module(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        Require(bool(file), "shader file exists");
        const auto bytes = file.tellg(); Require(bytes > 0 && bytes < 65536 && bytes % 4 == 0, "bounded SPIR-V");
        std::vector<u32> words(static_cast<std::size_t>(bytes) / 4);
        file.seekg(0); file.read(reinterpret_cast<char*>(words.data()), bytes);
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes; ci.pCode = words.data(); VkShaderModule module{};
        VkCheck(vkCreateShaderModule(device, &ci, nullptr, &module), "shader module"); return module;
    }
    Runtime(const std::filesystem::path& vertex, const std::filesystem::path& fragment) {
        // CodexAstraLocal: The caller explicitly selects its ICD. Record actual
        // device identity; this finite correctness run makes no target-speed claim.
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Uberhar image descriptor regression"; app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
        VkCheck(vkCreateInstance(&ici, nullptr, &instance), "instance");
        u32 count{}; VkCheck(vkEnumeratePhysicalDevices(instance, &count, nullptr), "device count");
        Require(count == 1, "one explicitly selected ICD device");
        VkCheck(vkEnumeratePhysicalDevices(instance, &count, &physical), "physical device");
        vkGetPhysicalDeviceProperties(physical, &properties);
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        std::cout << "device=" << properties.deviceName << " vendor=" << properties.vendorID
                  << " driver=" << properties.driverVersion << '\n';
        u32 families{}; vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> qs(families);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, qs.data());
        bool found{};
        for (u32 i = 0; i < families; ++i) if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { queue_family = i; found = true; break; }
        Require(found, "graphics queue exists");
        const float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = queue_family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
        VkCheck(vkCreateDevice(physical, &di, nullptr, &device), "device");
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.queueFamilyIndex = queue_family;
        VkCheck(vkCreateCommandPool(device, &pci, nullptr, &pool), "command pool");
        // CodexAstraLocal: Integer output preserves exact image/sampler choices
        // and draw ordinals without color-conversion tolerances.
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R32G32B32A32_UINT; ii.extent = {1,1,1};
        ii.mipLevels = ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkCheck(vkCreateImage(device, &ii, nullptr, &image), "color image");
        VkMemoryRequirements ir{}; vkGetImageMemoryRequirements(device, image, &ir);
        VkMemoryAllocateInfo ia{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ia.allocationSize = ir.size;
        ia.memoryTypeIndex = MemoryType(ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkCheck(vkAllocateMemory(device, &ia, nullptr, &image_memory), "color memory");
        VkCheck(vkBindImageMemory(device, image, image_memory, 0), "bind color");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkCheck(vkCreateImageView(device, &vi, nullptr, &image_view), "color view");
        VkAttachmentDescription attachment{}; attachment.format = ii.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        std::array<VkSubpassDependency,2> deps{};
        deps[0] = {VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0};
        deps[1] = {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,0};
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; ri.attachmentCount = 1; ri.pAttachments = &attachment;
        ri.subpassCount = 1; ri.pSubpasses = &sub; ri.dependencyCount = deps.size(); ri.pDependencies = deps.data();
        VkCheck(vkCreateRenderPass(device, &ri, nullptr, &pass), "render pass");
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fi.renderPass = pass;
        fi.attachmentCount = 1; fi.pAttachments = &image_view; fi.width = fi.height = fi.layers = 1;
        VkCheck(vkCreateFramebuffer(device, &fi, nullptr, &framebuffer), "framebuffer");
        // CodexAstraLocal: The actual consumer covers all six array elements
        // plus two ordinary units, exactly the production payload capacity.
        std::array<VkDescriptorSetLayoutBinding,3> bindings{};
        for(u32 i=0;i<3;++i)bindings[i]={i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,i?1u:6u,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount=bindings.size();li.pBindings=bindings.data();
        VkCheck(vkCreateDescriptorSetLayout(device,&li,nullptr,&set_layout),"image set layout");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,128*8};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.maxSets=128;dpi.poolSizeCount=1;dpi.pPoolSizes=&size;
        VkCheck(vkCreateDescriptorPool(device,&dpi,nullptr,&descriptor_pool),"image descriptor pool");
        VkPushConstantRange pc{VK_SHADER_STAGE_FRAGMENT_BIT,0,4};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &set_layout; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        VkCheck(vkCreatePipelineLayout(device, &pli, nullptr, &pipeline_layout), "pipeline layout");
        const auto vs = Module(vertex), fs = Module(fragment);
        std::array<VkPipelineShaderStageCreateInfo,2> stages{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vs,"main",nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main",nullptr};
        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,1,1,0,1}; VkRect2D scissor{{0,0},{1,1}};
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = vp.scissorCount = 1; vp.pViewports = &viewport; vp.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; bs.attachmentCount = 1; bs.pAttachments = &blend;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; gp.stageCount = stages.size(); gp.pStages = stages.data(); gp.pVertexInputState = &vin;
        gp.pInputAssemblyState = &assembly; gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &bs; gp.layout = pipeline_layout; gp.renderPass = pass;
        VkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline), "graphics pipeline");
        vkDestroyShaderModule(device, vs, nullptr); vkDestroyShaderModule(device, fs, nullptr);
        u8* mapped{}; MakeBuffer(96*16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readback_memory, mapped);
        pixels = reinterpret_cast<u32*>(mapped);
        CreateTextures();
    }
    VkCommandBuffer Begin() {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
        VkCommandBuffer cmd{};VkCheck(vkAllocateCommandBuffers(device,&ai,&cmd),"allocate image command");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkCheck(vkBeginCommandBuffer(cmd,&bi),"begin image command");return cmd;
    }
    void Submit(VkCommandBuffer cmd) {
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
        VkCheck(vkEndCommandBuffer(cmd),"end image command");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};
        VkCheck(vkCreateFence(device,&fi,nullptr,&fence),"image fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        VkCheck(vkQueueSubmit(queue,1,&submit,fence),"image submit");
        VkCheck(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ULL),"image fence wait");
        vkDestroyFence(device,fence,nullptr);vkFreeCommandBuffers(device,pool,1,&cmd);
    }
    void Transition(VkCommandBuffer cmd,VkImageLayout next) {
        if(texture_layout==next)return;
        std::array<VkImageMemoryBarrier,2> barriers{};
        for(u32 i=0;i<2;++i) {
            auto& b=barriers[i];b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout=texture_layout;b.newLayout=next;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            b.image=textures[i].image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6};
            b.srcAccessMask=texture_layout==VK_IMAGE_LAYOUT_UNDEFINED?0:
                texture_layout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask=next==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT;
        }
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,0,nullptr,0,nullptr,barriers.size(),barriers.data());texture_layout=next;
    }
    void CreateTextures() {
        for(auto& t:textures) {
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R32_UINT;ci.extent={2,1,1};
            ci.mipLevels=1;ci.arrayLayers=6;ci.samples=VK_SAMPLE_COUNT_1_BIT;
            ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VkCheck(vkCreateImage(device,&ci,nullptr,&t.image),"texture array image");
            VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,t.image,&req);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
            ai.memoryTypeIndex=MemoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkCheck(vkAllocateMemory(device,&ai,nullptr,&t.memory),"texture array memory");
            VkCheck(vkBindImageMemory(device,t.image,t.memory,0),"bind texture array");
            for(u32 face=0;face<6;++face) {
                VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                vi.image=t.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ci.format;
                vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,face,1};
                VkCheck(vkCreateImageView(device,&vi,nullptr,&t.views[face]),"face view");
            }
        }
        for(u32 i=0;i<2;++i) {
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
            si.addressModeU=si.addressModeV=si.addressModeW=i?VK_SAMPLER_ADDRESS_MODE_REPEAT:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VkCheck(vkCreateSampler(device,&si,nullptr,&samplers[i]),"image sampler");
        }
        VkBuffer upload{};VkDeviceMemory memory{};u8* data{};
        MakeBuffer(96,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,upload,memory,data);
        for(u32 set=0;set<2;++set)for(u32 face=0;face<6;++face)for(u32 x=0;x<2;++x) {
            const u32 value=100+set*1000+face*10+x;
            std::memcpy(data+(set*12+face*2+x)*4,&value,4);
        }
        auto cmd=Begin();Transition(cmd,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for(u32 set=0;set<2;++set) {
            VkBufferImageCopy copy{};copy.bufferOffset=set*48;
            copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,6};copy.imageExtent={2,1,1};
            vkCmdCopyBufferToImage(cmd,upload,textures[set].image,texture_layout,1,&copy);
        }
        Transition(cmd,VK_IMAGE_LAYOUT_GENERAL);Submit(cmd);
        vkUnmapMemory(device,memory);vkDestroyBuffer(device,upload,nullptr);vkFreeMemory(device,memory,nullptr);
    }
    VkDescriptorSet Allocate() {
#ifdef REWRITE_QUEUED_SET
        // CodexAstraLocal: Deliberate defect, before any command buffer binds
        // the captured sets. Wrong pixels are valid Vulkan, not update-after-bind UB.
        if(first_set)return first_set;
#endif
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool=descriptor_pool;ai.descriptorSetCount=1;ai.pSetLayouts=&set_layout;
        VkDescriptorSet set{};VkCheck(vkAllocateDescriptorSets(device,&ai,&set),"allocate immutable image set");
        ++allocations;if(!first_set)first_set=set;return set;
    }
    void Write(VkDescriptorSet set,const Vulkan::ImageDescriptorPayload& payload) {
        std::array<VkDescriptorImageInfo,8> images{};std::array<VkWriteDescriptorSet,8> writes{};
        for(size_t i=0;i<payload.count;++i) {
            const auto& value=payload.writes[i];
            images[i]={static_cast<VkSampler>(value.sampler),static_cast<VkImageView>(value.view),static_cast<VkImageLayout>(value.layout)};
            auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;
            w.dstBinding=value.binding;w.dstArrayElement=value.element;w.descriptorCount=1;
            w.descriptorType=static_cast<VkDescriptorType>(value.type);w.pImageInfo=&images[i];
        }
        vkUpdateDescriptorSets(device,payload.count,writes.data(),0,nullptr);
    }
    void Draw(VkCommandBuffer cmd,VkDescriptorSet set,u32 draw,VkImageLayout layout) {
        Require(draw<96,"bounded image draw count");Transition(cmd,layout);
        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        bi.renderPass=pass;bi.framebuffer=framebuffer;bi.renderArea.extent={1,1};
        vkCmdBeginRenderPass(cmd,&bi,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,1,&set,0,nullptr);
        vkCmdPushConstants(cmd,pipeline_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&draw);
        vkCmdDraw(cmd,3,1,0,0);vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy{};copy.bufferOffset=draw*16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
        vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);++rendered;
    }
    void DestroyTextures() {
        for(auto sampler:samplers)vkDestroySampler(device,sampler,nullptr);
        for(auto& t:textures) {
            for(auto view:t.views)vkDestroyImageView(device,view,nullptr);
            vkDestroyImage(device,t.image,nullptr);vkFreeMemory(device,t.memory,nullptr);
        }
    }
    ~Runtime() {
        if (device) VkCheck(vkDeviceWaitIdle(device), "final device idle");
        if (pixels) vkUnmapMemory(device, readback_memory);
        if (readback) vkDestroyBuffer(device, readback, nullptr);
        if (readback_memory) vkFreeMemory(device, readback_memory, nullptr);
        DestroyTextures();
        vkDestroyPipeline(device,pipeline,nullptr); vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        vkDestroyDescriptorPool(device,descriptor_pool,nullptr); vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
        vkDestroyFramebuffer(device,framebuffer,nullptr); vkDestroyRenderPass(device,pass,nullptr);
        vkDestroyImageView(device,image_view,nullptr); vkDestroyImage(device,image,nullptr); vkFreeMemory(device,image_memory,nullptr);
        vkDestroyCommandPool(device,pool,nullptr); vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    }
};

// CodexAstraLocal: The scalar oracle uses known uploaded texels and requested
// fields, never the descriptor set selected by the candidate reuse policy.
static std::array<u32,4> Expected(Runtime& gpu,const Vulkan::ImageDescriptorPayload& p,u32 ordinal) {
    std::array<u32,4> expected{};expected[3]=ordinal;
    for(u32 unit=0;unit<3;++unit) {
        const u32 element=unit?0:ordinal%6;
        const Vulkan::ImageDescriptorWrite* write{};
        for(size_t i=0;i<p.count;++i)if(p.writes[i].binding==unit&&p.writes[i].element==element)write=&p.writes[i];
        Require(write!=nullptr,"requested sampler element exists");
        bool found=false;
        for(u32 set=0;set<2;++set)for(u32 face=0;face<6;++face)
            if(static_cast<VkImageView>(write->view)==gpu.textures[set].views[face]) {
                const auto sampler=static_cast<VkSampler>(write->sampler);
                Require(sampler==gpu.samplers[0]||sampler==gpu.samplers[1],"declared sampler oracle");
                expected[unit]=100+set*1000+face*10+(sampler==gpu.samplers[0]?1u:0u);found=true;
            }
        Require(found,"requested image belongs to original upload");
    }
    return expected;
}

int main(int argc,char** argv) {
    try {
        Require(argc==3,"vertex/fragment modules required");
        Runtime gpu{argv[1],argv[2]};
        using namespace Vulkan;
        ImageDescriptorReuse cache;
        const auto payload=[&](u32 selected,u32 sampler=0,vk::ImageLayout layout=vk::ImageLayout::eGeneral) {
            ImageDescriptorPayload p;
            for(u8 face=0;face<6;++face)p.Append({vk::DescriptorType::eCombinedImageSampler,0,face,
                vk::ImageView{gpu.textures[selected].views[face]},vk::Sampler{gpu.samplers[sampler]},layout});
            for(u8 unit=1;unit<3;++unit)p.Append({vk::DescriptorType::eCombinedImageSampler,unit,0,
                vk::ImageView{gpu.textures[selected].views[0]},vk::Sampler{gpu.samplers[sampler]},layout});
            return p;
        };
        auto a=payload(0),b=payload(1);
        // CodexAstraLocal: These field-only checks include descriptor types and
        // declared count/order, which the fixed sampled-image layout cannot vary.
        for(u32 field=0;field<7;++field) {
            auto changed=a;
            switch(field) {
            case 0:changed.writes[5].view=b.writes[5].view;break;
            case 1:changed.writes[5].sampler=vk::Sampler{gpu.samplers[1]};break;
            case 2:changed.writes[5].layout=vk::ImageLayout::eShaderReadOnlyOptimal;break;
            case 3:changed.writes[5].type=vk::DescriptorType::eSampledImage;break;
            case 4:changed.writes[5].binding=2;break;
            case 5:changed.writes[5].element=4;break;
            case 6:--changed.count;break;
            }
            Require(!(a==changed),"every descriptor identity field matters");
        }
        struct Pending {VkDescriptorSet set;VkImageLayout layout;std::array<u32,4> expected;};
        std::vector<Pending> pending;u32 ordinal{};
        const auto allocate=[&](const ImageDescriptorPayload& p) {
            const auto set=gpu.Allocate();gpu.Write(set,p);return vk::DescriptorSet{set};
        };
        const auto capture=[&](const ImageDescriptorPayload& p,u64 tick,u64 epoch) {
            const auto set=cache.Resolve(p,tick,epoch,allocate);
            pending.push_back({static_cast<VkDescriptorSet>(set),static_cast<VkImageLayout>(p.writes[0].layout),Expected(gpu,p,ordinal++)});
            return set;
        };
        const auto execute=[&] {
            // All descriptor writes finish before command recording; immutable
            // queued captures must still represent their own A/A,B,A request.
            auto cmd=gpu.Begin();
            for(const auto& draw:pending)gpu.Draw(cmd,draw.set,draw.expected[3],draw.layout);
            gpu.Submit(cmd);
            for(const auto& draw:pending)Require(std::memcmp(gpu.pixels+draw.expected[3]*4,draw.expected.data(),16)==0,
                "queued image descriptor pixels changed");
            pending.clear();
        };
        for(u32 face=0;face<6;++face) {
            const auto first=capture(a,1,1);const auto same=capture(a,1,1);
            Require(first==same,"same-tick identical payload reuses immutable set");
            capture(b,1,1);capture(a,1,1);
        }
        execute();
        const auto before_tick=gpu.allocations;capture(a,2,1);
        Require(gpu.allocations==before_tick+1,"tick must allocate");
        // CodexAstraLocal: A real retired view is destroyed/recreated after the
        // first fence. Reuse must miss even for unchanged A handle values; this
        // models the production global resource epoch, not guaranteed driver
        // reuse of a particular numeric handle.
        vkDestroyImageView(gpu.device,gpu.textures[1].views[0],nullptr);
        VkImageViewCreateInfo recycled{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        recycled.image=gpu.textures[1].image;recycled.viewType=VK_IMAGE_VIEW_TYPE_2D;recycled.format=VK_FORMAT_R32_UINT;
        recycled.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VkCheck(vkCreateImageView(gpu.device,&recycled,nullptr,&gpu.textures[1].views[0]),"recycled view owner");
        b=payload(1);
        const auto before_epoch=gpu.allocations;capture(a,2,2);
        Require(gpu.allocations==before_epoch+1,"epoch must allocate");
        // Each array face changes independently, including the last element;
        // no first-face-only key can pass these exact actual sampler consumers.
        for(u32 i=0;i<6;++i) {
            auto changed=a;const u32 face=ordinal%6;changed.writes[face].view=b.writes[face].view;
            capture(changed,2,2);
        }
        capture(payload(0,0),2,2);capture(payload(0,1),2,2);
        capture(payload(0,0,vk::ImageLayout::eGeneral),2,2);
        const auto readonly=payload(0,0,vk::ImageLayout::eShaderReadOnlyOptimal);
        const auto previous=capture(readonly,2,2);
        bool threw=false;
        try {
            cache.Resolve(b,2,2,[&](const auto&) -> vk::DescriptorSet {
                (void)gpu.Allocate();throw std::runtime_error("declared allocation/write failure");
            });
        } catch(const std::runtime_error& error) {threw=std::string_view{error.what()}=="declared allocation/write failure";}
        Require(threw,"allocation failure reaches caller");
        const auto before_preserve=gpu.allocations;
        Require(capture(readonly,2,2)==previous&&gpu.allocations==before_preserve,
            "failed allocation publishes no cache state");
        capture(b,2,2); // One successful explicit retry; old queued sets stay valid.
        cache.Invalidate();const auto before_reset=gpu.allocations;capture(b,2,2);
        Require(gpu.allocations==before_reset+1,"reset invalidates last descriptor owner");
        const auto overflow=std::numeric_limits<u64>::max();
        const auto before_overflow=gpu.allocations;capture(b,2,overflow);capture(b,2,overflow);
        Require(gpu.allocations==before_overflow+2,"saturated epoch disables reuse");
        execute();
        std::cout<<"PASS image descriptors rendered="<<rendered<<" allocations="<<gpu.allocations
                 <<" requests="<<cache.requests<<" hits="<<cache.hits<<" checks="<<checks<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 23;
    }
}
