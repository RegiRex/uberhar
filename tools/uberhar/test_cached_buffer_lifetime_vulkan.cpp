// CodexAstraLocal: Execute real Vulkan fragment consumers of the source-derived
// cached ring. A delayed unsubmitted draw makes old-watch overwrite deterministic
// without racing GPU execution or depending on driver scheduling speed.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "common/alignment.h"
#define private public
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#undef private

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
    VkDescriptorSet set{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkBuffer readback{};
    VkDeviceMemory readback_memory{};
    u32* pixels{};
    std::array<VkBufferView, 2> views{};
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
        app.pApplicationName = "Uberhar cached-buffer lifetime regression"; app.apiVersion = VK_API_VERSION_1_1;
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
        // CodexAstraLocal: Integer attachment output compares exact distinct
        // UBO/proctex/LF values, avoiding color conversion or tolerance.
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
        std::array<VkDescriptorSetLayoutBinding,3> bindings{};
        bindings[0] = {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        for (u32 i = 1; i < 3; ++i) bindings[i] = {i,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount = bindings.size(); li.pBindings = bindings.data();
        VkCheck(vkCreateDescriptorSetLayout(device, &li, nullptr, &set_layout), "set layout");
        std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1},{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,2}}};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 1; dpi.poolSizeCount = sizes.size(); dpi.pPoolSizes = sizes.data();
        VkCheck(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptor_pool), "descriptor pool");
        VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; sai.descriptorPool = descriptor_pool; sai.descriptorSetCount = 1; sai.pSetLayouts = &set_layout;
        VkCheck(vkAllocateDescriptorSets(device, &sai, &set), "descriptor set");
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
        u8* mapped{}; MakeBuffer(48, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readback_memory, mapped);
        pixels = reinterpret_cast<u32*>(mapped);
    }
    void Descriptors(const std::array<VkBuffer,3>& buffers) {
        VkDescriptorBufferInfo info{buffers[0],0,16};
        std::array<VkWriteDescriptorSet,3> writes{};
        for (u32 i = 0; i < 3; ++i) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1; }
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; writes[0].pBufferInfo = &info;
        for (u32 i = 1; i < 3; ++i) {
            VkBufferViewCreateInfo vi{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO}; vi.buffer = buffers[i]; vi.format = VK_FORMAT_R32_UINT; vi.range = VK_WHOLE_SIZE;
            VkCheck(vkCreateBufferView(device, &vi, nullptr, &views[i-1]), "LUT view");
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER; writes[i].pTexelBufferView = &views[i-1];
        }
        vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    }
    void Draw(VkCommandBuffer cmd, u32 draw) {
        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; bi.renderPass = pass; bi.framebuffer = framebuffer; bi.renderArea.extent = {1,1};
        vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        const u32 offset = 0;
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,1,&set,1,&offset);
        vkCmdPushConstants(cmd,pipeline_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&draw);
        vkCmdDraw(cmd,3,1,0,0); vkCmdEndRenderPass(cmd);
        VkBufferImageCopy copy{}; copy.bufferOffset = draw * 16; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {1,1,1};
        vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
        ++rendered;
    }
    // CodexAstraLocal: Buffer views must be released before their referenced
    // ring buffers, including unwinding after an intended pixel mismatch.
    void DestroyViews() {
        for (auto& view : views) {
            if (view) vkDestroyBufferView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    ~Runtime() {
        if (device) VkCheck(vkDeviceWaitIdle(device), "final device idle");
        if (pixels) vkUnmapMemory(device, readback_memory);
        if (readback) vkDestroyBuffer(device, readback, nullptr);
        if (readback_memory) vkFreeMemory(device, readback_memory, nullptr);
        DestroyViews();
        vkDestroyPipeline(device,pipeline,nullptr); vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        vkDestroyDescriptorPool(device,descriptor_pool,nullptr); vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
        vkDestroyFramebuffer(device,framebuffer,nullptr); vkDestroyRenderPass(device,pass,nullptr);
        vkDestroyImageView(device,image_view,nullptr); vkDestroyImage(device,image,nullptr); vkFreeMemory(device,image_memory,nullptr);
        vkDestroyCommandPool(device,pool,nullptr); vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    }
};

namespace Vulkan {
constexpr u64 WATCHES_RESERVE_CHUNK = 8;
class Instance {
public:
    Runtime& gpu;
    explicit Instance(Runtime& gpu_) : gpu{gpu_} {}
    u64 NonCoherentAtomSize() const { return gpu.properties.limits.nonCoherentAtomSize; }
};
// CodexAstraLocal: This scheduler seam owns real command buffers and Vulkan
// fences. It models submit-on-current-wait; production ring methods call it.
class Scheduler {
public:
    Runtime& gpu;
    u64 tick{1}, completed{};
    VkCommandBuffer command{};
    std::vector<std::pair<u64,VkFence>> fences;
    explicit Scheduler(Runtime& gpu_) : gpu{gpu_} { Begin(); }
    u64 CurrentTick() const { return tick; }
    void Begin() {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = gpu.pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCheck(vkAllocateCommandBuffers(gpu.device,&ai,&command),"allocate command");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkCheck(vkBeginCommandBuffer(command,&bi),"begin command");
    }
    void Flush() {
        VkCheck(vkEndCommandBuffer(command),"end command");
        VkFence fence{}; VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkCheck(vkCreateFence(gpu.device,&fi,nullptr,&fence),"fence");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &command;
        VkCheck(vkQueueSubmit(gpu.queue,1,&si,fence),"queue submit"); fences.emplace_back(tick++,fence); Begin();
    }
    void Wait(u64 target) {
        if (target <= completed) return;
        if (target >= tick) Flush();
        for (auto [at,fence] : fences) if (at <= target && at > completed)
            VkCheck(vkWaitForFences(gpu.device,1,&fence,VK_TRUE,5000000000ULL),"real fence completion");
        completed = target;
    }
    ~Scheduler() {
        VkCheck(vkDeviceWaitIdle(gpu.device),"scheduler drain");
        for (auto [at,fence] : fences) vkDestroyFence(gpu.device,fence,nullptr);
    }
};
StreamBuffer::StreamBuffer(const Instance& i, Scheduler& s, vk::BufferUsageFlags u, u64 n, BufferType t)
    : instance{i}, scheduler{s}, device{i.gpu.device}, stream_buffer_size{n}, usage{u}, type{t}, is_coherent{true} {
    i.gpu.MakeBuffer(n,u,buffer,memory,mapped); ReserveWatches(current_watches,8); ReserveWatches(previous_watches,8);
}
StreamBuffer::~StreamBuffer() {
    VkCheck(vkDeviceWaitIdle(instance.gpu.device),"ring destruction drain");
    vkUnmapMemory(instance.gpu.device,memory); vkDestroyBuffer(instance.gpu.device,buffer,nullptr); vkFreeMemory(instance.gpu.device,memory,nullptr);
}
#include "ring.inc"
struct RasterizerVulkan {
    StreamBuffer& uniform_buffer;
    StreamBuffer& texture_buffer;
    StreamBuffer& texture_lf_buffer;
    void MarkCachedShaderBuffersUsed();
};
#include "mark.inc"

static void Case(const std::filesystem::path& vs, const std::filesystem::path& fs, unsigned kind, bool pass_flush) {
    Runtime gpu{vs,fs}; Instance instance{gpu}; Scheduler scheduler{gpu};
    const auto capacity = std::max<u64>(512,gpu.properties.limits.minUniformBufferOffsetAlignment * 2);
    StreamBuffer ubo{instance,scheduler,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,capacity};
    StreamBuffer proc{instance,scheduler,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,capacity};
    StreamBuffer lf{instance,scheduler,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,capacity};
    std::array<StreamBuffer*,3> rings{&ubo,&proc,&lf}; RasterizerVulkan raster{ubo,proc,lf};
    std::array<u32,4> expected{11,22,33,0};
    for (unsigned i = 0; i < rings.size(); ++i) {
        auto& ring = *rings[i]; auto [data,offset,invalidate] = ring.Map(16,16);
        std::array<u32,4> payload{expected[i],101U+i,201U+i,301U+i}; std::memcpy(data,payload.data(),16); ring.Commit(16);
        auto [fill,fill_offset,wrap] = ring.Map(capacity-16,1); std::memset(fill,0xa5,capacity-16); ring.Commit(capacity-16);
    }
    gpu.Descriptors({ubo.Handle(),proc.Handle(),lf.Handle()});
    // CodexAstraLocal: Created after the rings, this guard closes view ownership
    // first on every successful or deliberately failing test path.
    struct ViewGuard { Runtime& gpu; ~ViewGuard() { gpu.DestroyViews(); } } view_guard{gpu};
    gpu.Draw(scheduler.command,0); raster.MarkCachedShaderBuffersUsed(); scheduler.Wait(1);
    Require(std::memcmp(gpu.pixels,expected.data(),16)==0,"initial GPU payload");
    if (pass_flush) scheduler.Flush();
    gpu.Draw(scheduler.command,1); raster.MarkCachedShaderBuffersUsed();
    // CodexAstraLocal: Draw 1 is deliberately unsubmitted. The candidate's
    // real wait must render A before this wrap overwrites its range with B.
    auto& ring = *rings[kind]; auto [data,offset,invalidate] = ring.Map(16,16);
    Require(invalidate && offset==0,"actual ring wrapped");
    std::array<u32,4> replacement{expected[kind]+1000,701,801,901}; std::memcpy(data,replacement.data(),16); ring.Commit(16);
    gpu.Draw(scheduler.command,2); raster.MarkCachedShaderBuffersUsed(); scheduler.Wait(scheduler.CurrentTick());
    expected[3]=1;
    Require(std::memcmp(gpu.pixels+4,expected.data(),16)==0,"GPU cached draw bytes overwritten");
    expected[kind]+=1000; expected[3]=2;
    Require(std::memcmp(gpu.pixels+8,expected.data(),16)==0,"GPU replacement draw wrong");
}
} // namespace Vulkan

int main(int argc,char** argv) {
    try {
        Require(argc==3,"vertex/fragment module arguments");
        for (unsigned ring=0;ring<3;++ring) for (bool pass_flush:{false,true}) Vulkan::Case(argv[1],argv[2],ring,pass_flush);
        std::cout << "PASS Vulkan cached lifetime cases=6 rendered=" << rendered << " checks=" << checks << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n'; return 23;
    }
}
