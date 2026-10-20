// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <json.hpp>
#include "common/alignment.h"
#include "common/file_util.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/uberhar_activity.h"
#include "common/uberhar_test_profile.h"
#include "video_core/rasterizer_accelerated.h"
#include "video_core/renderer_vulkan/uberhar_compute_benchmark.h"
#include "video_core/renderer_vulkan/uberhar_compute_rect_shader.h"
#include "video_core/renderer_vulkan/vk_compute_benchmark.h"
#include "video_core/renderer_vulkan/vk_compute_rect.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "video_core/shader_recovery_error.h"

// CodexAstraLocal: Apply the renderer's Vulkan feature macros before the C VMA header.
#include <vk_mem_alloc.h>

namespace Vulkan {
namespace {
using namespace ComputeBenchmarkData;
using namespace Pica::Shader::Generator;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// CodexAstraLocal: Expose only the inherited vertex type for synthetic data;
// this abstract type is never constructed and borrows no live rasterizer state.
struct VertexAccess : VideoCore::RasterizerAccelerated {
    using RasterizerAccelerated::HardwareVertex;
};
using Vertex = VertexAccess::HardwareVertex;
static_assert(sizeof(Vertex) == 88 && offsetof(Vertex, color) == 16 &&
              offsetof(Vertex, view) == 76 && std::endian::native == std::endian::little);
constexpr u32 UploadBytes = MaxImageBytes + 16384;
constexpr u32 ResultBytes = 65536;
constexpr vk::ImageSubresourceRange ColorRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
constexpr vk::ImageSubresourceLayers ColorLayer{vk::ImageAspectFlagBits::eColor, 0, 0, 1};

// CodexAstraLocal: Device loss affects the shared renderer and must not be
// downgraded to an optional allocation/host-visibility failure by VMA wrappers.
void RequireVma(VkResult result, const char* reason) {
    if (result == VK_ERROR_DEVICE_LOST)
        throw VideoCore::ShaderRecoveryError("Compute benchmark lost the Vulkan device");
    if (result != VK_SUCCESS)
        throw std::runtime_error(reason);
}

// CodexAstraLocal: Fixed raw allocations are owned independently of reusable
// stream buffers. VMA handles coherence; failed partial creation stays local.
struct BufferOwner {
    VmaAllocator allocator{};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    void* mapped{};
    u64 allocated{};
    ~BufferOwner() { if (buffer) vmaDestroyBuffer(allocator, buffer, allocation); }
    void Create(VmaAllocator owner, u32 bytes, vk::BufferUsageFlags usage) {
        allocator = owner;
        const vk::BufferCreateInfo info{.size = bytes, .usage = usage};
        const auto raw = static_cast<VkBufferCreateInfo>(info);
        const VmaAllocationCreateInfo request{
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                     VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
        VmaAllocationInfo result{};
        RequireVma(vmaCreateBuffer(owner, &raw, &request, &buffer, &allocation, &result),
                   "buffer_allocation");
        mapped = result.pMappedData;
        allocated = result.size;
        if (!mapped) throw std::runtime_error("buffer_mapping");
    }
    vk::Buffer Handle() const { return vk::Buffer{buffer}; }
};

struct ImageOwner {
    VmaAllocator allocator{};
    VkImage image{};
    VmaAllocation allocation{};
    u64 allocated{};
    vk::UniqueImageView color, storage;
    vk::UniqueFramebuffer framebuffer;
    ~ImageOwner() {
        framebuffer.reset(); storage.reset(); color.reset();
        if (image) vmaDestroyImage(allocator, image, allocation);
    }
    void Create(const Instance& instance, u32 width, u32 height) {
        allocator = instance.GetAllocator();
        const std::array formats{vk::Format::eR8G8B8A8Unorm, vk::Format::eR32Uint};
        const vk::ImageFormatListCreateInfo list{.viewFormatCount = 2, .pViewFormats = formats.data()};
        const vk::ImageCreateInfo info{
            .pNext = &list, .flags = vk::ImageCreateFlagBits::eMutableFormat,
            .imageType = vk::ImageType::e2D, .format = formats[0], .extent = {width, height, 1},
            .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage |
                     vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eSampled};
        const auto raw = static_cast<VkImageCreateInfo>(info);
        const VmaAllocationCreateInfo request{
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VmaAllocationInfo result{};
        RequireVma(vmaCreateImage(allocator, &raw, &request, &image, &allocation, &result),
                   "image_allocation");
        allocated = result.size;
        const auto device = instance.GetDevice();
        // CodexAstraLocal: Mutable views inherit only their actual usage. The
        // integer storage view must not require color-attachment/sample support.
        const vk::ImageViewUsageCreateInfo color_usage{
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled};
        const vk::ImageViewUsageCreateInfo storage_usage{.usage = vk::ImageUsageFlagBits::eStorage};
        color = device.createImageViewUnique({.pNext = &color_usage, .image = vk::Image{image},
            .viewType = vk::ImageViewType::e2D, .format = formats[0], .subresourceRange = ColorRange});
        storage = device.createImageViewUnique({.pNext = &storage_usage, .image = vk::Image{image},
            .viewType = vk::ImageViewType::e2D, .format = formats[1], .subresourceRange = ColorRange});
    }
};

// CodexAstraLocal: These barriers belong to private scratch resources only;
// reset/readback are outside the timed route and host access follows completion.
void ImageBarrier(vk::CommandBuffer command, vk::Image image, vk::ImageLayout old_layout,
                  vk::AccessFlags source, vk::AccessFlags destination,
                  vk::PipelineStageFlags source_stage, vk::PipelineStageFlags destination_stage) {
    const vk::ImageMemoryBarrier barrier{.srcAccessMask = source, .dstAccessMask = destination,
        .oldLayout = old_layout, .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = ColorRange};
    command.pipelineBarrier(source_stage, destination_stage, {}, {}, {}, barrier);
}

// CodexAstraLocal: Cap each artifact at 64 KiB and use exclusive raw creation;
// an existing or partial ID is never overwritten or treated as permission to retry.
bool WriteExclusive(const std::string& path, const Json& value) {
    const auto text = value.dump(2) + '\n';
    if (text.size() > ResultBytes || !FileUtil::CreateFullPath(path)) return false;
    FileUtil::IOFile file{path, "wbx"};
    return file.IsOpen() && file.WriteBytes(text.data(), text.size()) == text.size() &&
           file.Flush() && file.Close();
}
} // namespace

// CodexAstraLocal: All mutable diagnostic state stays with one renderer owner.
// Only immutable handles/packets are queued; Poll runs after the existing worker
// drain and never waits for a GPU tick or timestamp availability.
struct ComputeBenchmark::Impl {
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass;
    const u64 title;
    const std::string id, result_path;
    const Clock::time_point started{Clock::now()};
    std::string status{"preparing"};
    bool reported{}, pending{}, image_initialized{}, submitted{};
    u32 next_pair{}, enqueued_routes{}, submitted_routes{}, completed_routes{}, validated_routes{};
    u64 tick{}, run{}, preparation_ns{}, recording_ns{}, validation_ns{}, allocated_bytes{};
    double timestamp_period{};
    u64 timestamp_mask{};
    u32 timestamp_bits{}, vs_offset{}, fs_offset{}, lut_offset{};
    std::array<std::array<double, 2>, CaseCount * PairsPerCase> durations{};
    std::array<std::array<u64, 8>, CaseCount * PairsPerCase> query_results{};
    std::array<u32, 6> failed_pixel{};
    bool has_failed_pixel{};
    std::array<u64, 3> source_hashes{}, module_hashes{};
    std::array<u32, 3> module_words{};
    std::array<u64, CaseCount * PairsPerCase> completed_ticks{};
    std::array<u64, CaseCount * PairsPerCase> latency_ns{};
    Clock::time_point recorded_at{};
    std::array<u64, CaseCount> touched_pixels{};
    Workload<Vertex> workload;
    const Pica::Shader::Profile profile;
    GLSL::DynamicTevState fragment_state{};
    BufferOwner upload;
    std::array<BufferOwner, 2> readbacks;
    // CodexAstraLocal: Declaration order plus explicit image cleanup keeps views
    // and framebuffers alive through pending work and shorter-lived than memory.
    vk::UniqueRenderPass pass;
    std::array<ImageOwner, 2> images;
    ImageOwner dummy;
    vk::UniqueSampler sampler;
    std::array<vk::UniqueBufferView, 3> lut_views;
    vk::UniquePipelineCache cache;
    vk::UniqueQueryPool queries;
    vk::UniqueDescriptorPool descriptors;
    std::array<vk::UniqueDescriptorSetLayout, 3> descriptor_layouts;
    std::array<vk::DescriptorSet, 3> sets{};
    vk::UniquePipelineLayout compute_layout, graphics_layout;
    std::array<vk::UniqueShaderModule, 3> modules;
    vk::UniquePipeline compute_pipeline;
    std::array<vk::UniquePipeline, 2> graphics_pipelines;

    Impl(const Instance& i, Scheduler& s, RenderManager& r, const Pica::Shader::Profile& p,
         u64 t, std::string request,
         std::string result)
        : instance{i}, scheduler{s}, renderpass{r}, title{t}, id{std::move(request)},
          result_path{std::move(result)}, profile{p} {
        // CodexAstraLocal: Failure labels never allocate on the running path.
        status.reserve(64);
    }

    void Prepare();
    void RecordPair();
    bool Collect();
    void Report() noexcept;
    void Fail(std::string_view reason) { status.assign(reason.substr(0, 64)); }
};

// CodexAstraLocal: Prepare a fixed private pipeline/resource set once. The
// device's current capabilities are checked; no retained Thor fact is assumed.
void ComputeBenchmark::Impl::Prepare() {
    const auto device = instance.GetDevice();
    const auto physical = instance.GetPhysicalDevice();
    const auto properties = physical.getProperties();
    const auto family = physical.getQueueFamilyProperties().at(instance.GetGraphicsQueueFamilyIndex());
    if (!instance.IsImageFormatListSupported() || properties.apiVersion < VK_API_VERSION_1_1 ||
        !(family.queueFlags & vk::QueueFlagBits::eCompute) || !family.timestampValidBits ||
        !properties.limits.timestampComputeAndGraphics ||
        !(properties.limits.timestampPeriod > 0) ||
        !std::isfinite(properties.limits.timestampPeriod) ||
        properties.limits.minUniformBufferOffsetAlignment > 4096 ||
        properties.limits.minTexelBufferOffsetAlignment > 4096 ||
        properties.limits.maxPushConstantsSize < sizeof(fragment_state))
        throw std::runtime_error("capabilities");
    const auto storage = physical.getFormatProperties(vk::Format::eR32Uint).optimalTilingFeatures;
    const auto color = physical.getFormatProperties(vk::Format::eR8G8B8A8Unorm).optimalTilingFeatures;
    const auto required_color = vk::FormatFeatureFlagBits::eColorAttachment |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eStorageImage;
    if (!(storage & vk::FormatFeatureFlagBits::eStorageImage) ||
        (color & required_color) != required_color)
        throw std::runtime_error("format_capabilities");
    // CodexAstraLocal: Query the actual mutable combined-usage allocation;
    // view creation remains a checked second compatibility boundary.
    const auto format = physical.getImageFormatProperties(vk::Format::eR8G8B8A8Unorm,
        vk::ImageType::e2D, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage |
        vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eSampled, vk::ImageCreateFlagBits::eMutableFormat);
    if (format.maxExtent.width < MaxWidth || format.maxExtent.height < MaxHeight)
        throw std::runtime_error("image_extent");
    timestamp_period = properties.limits.timestampPeriod;
    timestamp_bits = family.timestampValidBits;
    timestamp_mask = timestamp_bits >= 64 ? ~u64{} : (u64{1} << timestamp_bits) - 1;
    if (double(timestamp_mask) * timestamp_period <= double(DeadlineSeconds) * 2e9)
        throw std::runtime_error("timestamp_wrap_horizon");
    queries = device.createQueryPoolUnique({.queryType = vk::QueryType::eTimestamp,
                                            .queryCount = QueryCount});
    cache = device.createPipelineCacheUnique({});
    upload.Create(instance.GetAllocator(), UploadBytes,
        vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eVertexBuffer |
        vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eUniformTexelBuffer);
    for (auto& buffer : readbacks)
        buffer.Create(instance.GetAllocator(), MaxImageBytes, vk::BufferUsageFlagBits::eTransferDst);
    vs_offset = Common::AlignUp<u32>(MaxImageBytes + sizeof(workload.vertices),
        std::max<u32>(1, properties.limits.minUniformBufferOffsetAlignment));
    fs_offset = Common::AlignUp<u32>(vs_offset + sizeof(VSUniformData),
        std::max<u32>(1, properties.limits.minUniformBufferOffsetAlignment));
    lut_offset = Common::AlignUp<u32>(fs_offset + sizeof(FSUniformData),
        std::max<u32>(16, properties.limits.minTexelBufferOffsetAlignment));
    if (lut_offset + 256 > UploadBytes) throw std::runtime_error("upload_bound");
    std::memset(upload.mapped, 0, UploadBytes);

    const vk::AttachmentDescription attachment{.format = vk::Format::eR8G8B8A8Unorm,
        .samples = vk::SampleCountFlagBits::e1, .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore, .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eGeneral, .finalLayout = vk::ImageLayout::eGeneral};
    const vk::AttachmentReference reference{0, vk::ImageLayout::eGeneral};
    const vk::SubpassDescription subpass{.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1, .pColorAttachments = &reference};
    pass = device.createRenderPassUnique({.attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass});
    for (auto& image : images) {
        image.Create(instance, MaxWidth, MaxHeight);
        image.framebuffer = device.createFramebufferUnique({.renderPass = *pass,
            .attachmentCount = 1, .pAttachments = &*image.color,
            .width = MaxWidth, .height = MaxHeight, .layers = 1});
    }
    dummy.Create(instance, 1, 1);
    sampler = device.createSamplerUnique({.magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest, .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge, .maxLod = 0});
    const std::array lut_formats{vk::Format::eR32G32Sfloat, vk::Format::eR32G32Sfloat,
                                 vk::Format::eR32G32B32A32Sfloat};
    for (u32 i = 0; i < lut_views.size(); ++i) {
        if (!(physical.getFormatProperties(lut_formats[i]).bufferFeatures &
              vk::FormatFeatureFlagBits::eUniformTexelBuffer))
            throw std::runtime_error("texel_buffer_format");
        lut_views[i] = device.createBufferViewUnique({.buffer = upload.Handle(),
            .format = lut_formats[i], .offset = lut_offset, .range = 256});
    }

    // CodexAstraLocal: Every statically declared generic shader resource has a
    // valid private binding even though this workload consumes primary color.
    const std::array compute_bindings{vk::DescriptorSetLayoutBinding{
        0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute}};
    const std::array buffer_bindings{
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment}};
    const std::array texture_bindings{
        vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}};
    descriptor_layouts[0] = device.createDescriptorSetLayoutUnique({
        .bindingCount = compute_bindings.size(), .pBindings = compute_bindings.data()});
    descriptor_layouts[1] = device.createDescriptorSetLayoutUnique({
        .bindingCount = buffer_bindings.size(), .pBindings = buffer_bindings.data()});
    descriptor_layouts[2] = device.createDescriptorSetLayoutUnique({
        .bindingCount = texture_bindings.size(), .pBindings = texture_bindings.data()});
    const std::array pool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 2},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 3},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 3}};
    descriptors = device.createDescriptorPoolUnique({.maxSets = 3,
        .poolSizeCount = pool_sizes.size(), .pPoolSizes = pool_sizes.data()});
    const std::array layouts{*descriptor_layouts[0], *descriptor_layouts[1], *descriptor_layouts[2]};
    const auto allocated = device.allocateDescriptorSets({.descriptorPool = *descriptors,
        .descriptorSetCount = layouts.size(), .pSetLayouts = layouts.data()});
    std::copy(allocated.begin(), allocated.end(), sets.begin());
    {
        const vk::DescriptorImageInfo info{.imageView = *images[1].storage,
                                            .imageLayout = vk::ImageLayout::eGeneral};
        device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = sets[0], .dstBinding = 0,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &info}, {});
    }
    const std::array uniforms{vk::DescriptorBufferInfo{upload.Handle(), vs_offset, sizeof(VSUniformData)},
                              vk::DescriptorBufferInfo{upload.Handle(), fs_offset, sizeof(FSUniformData)}};
    for (u32 i = 0; i < 2; ++i)
        device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = sets[1], .dstBinding = i + 1,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &uniforms[i]}, {});
    for (u32 i = 0; i < 3; ++i) {
        device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = sets[1], .dstBinding = i + 3,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
            .pTexelBufferView = &*lut_views[i]}, {});
        const vk::DescriptorImageInfo info{*sampler, *dummy.color, vk::ImageLayout::eGeneral};
        device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = sets[2], .dstBinding = i,
            .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &info}, {});
    }
    const vk::PushConstantRange compute_range{vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputeRectPacket)};
    compute_layout = device.createPipelineLayoutUnique({.setLayoutCount = 1,
        .pSetLayouts = &layouts[0], .pushConstantRangeCount = 1, .pPushConstantRanges = &compute_range});
    const vk::PushConstantRange graphics_range{vk::ShaderStageFlagBits::eFragment, 0, sizeof(fragment_state)};
    graphics_layout = device.createPipelineLayoutUnique({.setLayoutCount = 2,
        .pSetLayouts = &layouts[1], .pushConstantRangeCount = 1, .pPushConstantRanges = &graphics_range});

    // CodexAstraLocal: Compile production shader text with an explicit cold
    // mandatory policy. Preparation time is not part of paired GPU intervals.
    if (!ComputeBenchmarkData::Prepare(0, workload)) throw std::runtime_error("workload_admission");
    // CodexAstraLocal: Copy the real cache profile, including clip/logic support;
    // this reference must not silently compile a different hardware interface.
    if (!profile.is_vulkan || !profile.has_separable_shaders)
        throw std::runtime_error("shader_profile");
    const Pica::Shader::FSConfig config{workload.regs};
    const Pica::Shader::UserConfig user{};
    if (!GLSL::SupportsDynamicTev(config, user)) throw std::runtime_error("generic_support");
    fragment_state = GLSL::MakeDynamicTevState(config, profile);
    const auto family_config = GLSL::MakeDynamicTevFamilyConfig(config, profile);
    const std::array sources{std::string{ComputeRectShader},
        std::string{"#version 450\n"} + GLSL::GenerateTrivialVertexShader(profile.has_clip_planes, true),
        std::string{"#version 450\n"} + GLSL::FragmentModule{family_config, user, profile, true}.Generate()};
    // CodexAstraLocal: All four fixed states must share the actual generated
    // generic family; dynamic push data alone may not hide a different shader.
    for (u32 case_id = 1; case_id < CaseCount; ++case_id) {
        if (!ComputeBenchmarkData::Prepare(case_id, workload))
            throw std::runtime_error("workload_admission");
        const Pica::Shader::FSConfig sibling{workload.regs};
        if (std::string{"#version 450\n"} + GLSL::FragmentModule{
                GLSL::MakeDynamicTevFamilyConfig(sibling, profile), user, profile, true}.Generate() != sources[2])
            throw std::runtime_error("fragment_family_changed");
    }
    const std::array stages{vk::ShaderStageFlagBits::eCompute, vk::ShaderStageFlagBits::eVertex,
                             vk::ShaderStageFlagBits::eFragment};
    for (u32 i = 0; i < modules.size(); ++i) {
        // CodexAstraLocal: Vulkan parsing already defines VULKAN=100; redefining
        // it as 1 rejected every scratch shader before any benchmark commands.
        const auto words = CompileGLSL(sources[i], stages[i], "", true);
        if (words.empty()) throw std::runtime_error("shader_compile");
        source_hashes[i] = Common::ComputeHash64(sources[i].data(), sources[i].size());
        module_hashes[i] = Common::ComputeHash64(words.data(), words.size() * sizeof(u32));
        module_words[i] = static_cast<u32>(words.size());
        modules[i] = device.createShaderModuleUnique({.codeSize = words.size() * sizeof(u32),
                                                       .pCode = words.data()});
    }
    compute_pipeline = device.createComputePipelineUnique(*cache, {
        .stage = {.stage = stages[0], .module = *modules[0], .pName = "main"},
        .layout = *compute_layout}).value;
    const std::array shader_stages{
        vk::PipelineShaderStageCreateInfo{.stage = stages[1], .module = *modules[1], .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = stages[2], .module = *modules[2], .pName = "main"}};
    const vk::VertexInputBindingDescription binding{0, sizeof(Vertex), vk::VertexInputRate::eVertex};
    const std::array attributes{
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, position)},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, color)},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, tex_coord0)},
        vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, tex_coord1)},
        vk::VertexInputAttributeDescription{4, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, tex_coord2)},
        vk::VertexInputAttributeDescription{5, 0, vk::Format::eR32Sfloat, offsetof(Vertex, tex_coord0_w)},
        vk::VertexInputAttributeDescription{6, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, normquat)},
        vk::VertexInputAttributeDescription{7, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, view)}};
    const vk::PipelineVertexInputStateCreateInfo vertex_input{.vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding, .vertexAttributeDescriptionCount = attributes.size(),
        .pVertexAttributeDescriptions = attributes.data()};
    const vk::PipelineInputAssemblyStateCreateInfo assembly{.topology = vk::PrimitiveTopology::eTriangleList};
    const vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster{.polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone, .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.f};
    const vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};
    const vk::PipelineDepthStencilStateCreateInfo depth{};
    const std::array dynamic{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic_info{.dynamicStateCount = dynamic.size(),
                                                          .pDynamicStates = dynamic.data()};
    for (u32 i = 0; i < graphics_pipelines.size(); ++i) {
        const vk::PipelineColorBlendAttachmentState attachment_state{
            .colorWriteMask = vk::ColorComponentFlags{Cases[i].channels}};
        // CodexAstraLocal: Match the inherited blend-disabled Copy logic state,
        // including devices whose generic fragment shader emulates logic ops.
        const vk::PipelineColorBlendStateCreateInfo blend{
            .logicOpEnable = !instance.NeedsLogicOpEmulation(), .logicOp = vk::LogicOp::eCopy,
            .attachmentCount = 1,
                                                           .pAttachments = &attachment_state};
        graphics_pipelines[i] = device.createGraphicsPipelineUnique(*cache, {
            .stageCount = shader_stages.size(), .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly,
            .pViewportState = &viewport, .pRasterizationState = &raster,
            .pMultisampleState = &multisample, .pDepthStencilState = &depth,
            .pColorBlendState = &blend, .pDynamicState = &dynamic_info,
            .layout = *graphics_layout, .renderPass = *pass, .subpass = 0}).value;
    }
    allocated_bytes = upload.allocated + dummy.allocated;
    for (u32 i = 0; i < 2; ++i) allocated_bytes += images[i].allocated + readbacks[i].allocated;
    preparation_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
    status = "ready";
}

// CodexAstraLocal: Exactly one pair reuses the upload/readback storage. The
// caller has already observed completion of its predecessor before host writes.
void ComputeBenchmark::Impl::RecordPair() {
    if (pending || next_pair >= CaseCount * PairsPerCase)
        throw std::runtime_error("pair_bound");
    const auto begin = Clock::now();
    const u32 case_id = next_pair / PairsPerCase;
    const auto c = Cases[case_id];
    if (!ComputeBenchmarkData::Prepare(case_id, workload))
        throw std::runtime_error("workload_admission");
    auto* background = static_cast<u32*>(upload.mapped);
    for (u32 y = 0; y < c.height; ++y)
        for (u32 x = 0; x < c.width; ++x)
            background[y * c.width + x] = Background(x, y);
    auto* bytes = static_cast<u8*>(upload.mapped);
    std::memcpy(bytes + MaxImageBytes, workload.vertices.data(), sizeof(workload.vertices));
    const VSUniformData vs{};
    FSUniformData fs{};
    fs.framebuffer_scale = 1;
    fs.depth_scale = -1.f;
    std::memcpy(bytes + vs_offset, &vs, sizeof(vs));
    std::memcpy(bytes + fs_offset, &fs, sizeof(fs));
    const Pica::Shader::FSConfig config{workload.regs};
    fragment_state = GLSL::MakeDynamicTevState(config, profile);
    RequireVma(vmaFlushAllocation(upload.allocator, upload.allocation, 0, VK_WHOLE_SIZE), "upload_flush");

    // CodexAstraLocal: No live graphics handles, dynamic state, descriptors or
    // push constants may survive the scratch pass as falsely reusable state.
    renderpass.EndRendering();
    scheduler.MakeDirty(StateFlags::Pipeline | StateFlags::DescriptorSets | StateFlags::FragmentConstants);
    const std::array image_handles{vk::Image{images[0].image}, vk::Image{images[1].image}};
    const std::array readback_handles{readbacks[0].Handle(), readbacks[1].Handle()};
    const auto packets = workload.packets;
    const auto initial_layout = image_initialized ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined;
    scheduler.Record([case_info = c, pair = next_pair, image_handles, readback_handles, packets,
                      initial_layout, first = !image_initialized, dummy_image = vk::Image{dummy.image},
                      upload_buffer = upload.Handle(), pool = *queries, descriptor_sets = sets,
                      compute_layout = *compute_layout, compute_pipeline = *compute_pipeline,
                      graphics_layout = *graphics_layout, graphics_pipeline = *graphics_pipelines[case_id % 2],
                      render_pass = *pass, framebuffer = *images[0].framebuffer,
                      constants = fragment_state](vk::CommandBuffer command) {
        const auto all = vk::PipelineStageFlagBits::eAllCommands;
        const auto transfer = vk::PipelineStageFlagBits::eTransfer;
        const auto memory = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        if (first) {
            ImageBarrier(command, dummy_image, vk::ImageLayout::eUndefined, {},
                         vk::AccessFlagBits::eTransferWrite, vk::PipelineStageFlagBits::eTopOfPipe, transfer);
            command.clearColorImage(dummy_image, vk::ImageLayout::eGeneral,
                                    vk::ClearColorValue{.float32 = std::array<float, 4>{0, 0, 0, 1}}, ColorRange);
            ImageBarrier(command, dummy_image, vk::ImageLayout::eGeneral,
                         vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                         transfer, vk::PipelineStageFlagBits::eFragmentShader);
        }
        command.resetQueryPool(pool, pair * 4, 4);
        const vk::BufferImageCopy copy{.imageSubresource = ColorLayer,
            .imageExtent = {case_info.width, case_info.height, 1}};
        // CodexAstraLocal: AB/BA order alternates; both routes start from exactly
        // the same private pattern. Reset/upload/readback are outside timestamps.
        for (u32 ordinal = 0; ordinal < 2; ++ordinal) {
            const u32 route = (pair & 1U) ^ ordinal;
            const auto image = image_handles[route];
            ImageBarrier(command, image, initial_layout, first ? vk::AccessFlags{} : memory,
                         vk::AccessFlagBits::eTransferWrite, all, transfer);
            command.copyBufferToImage(upload_buffer, image, vk::ImageLayout::eGeneral, copy);
            ImageBarrier(command, image, vk::ImageLayout::eGeneral,
                         vk::AccessFlagBits::eTransferWrite, memory, transfer, all);
            command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, pool, pair * 4 + route * 2);
            if (route == 1) {
                for (u32 operation = 0; operation < OperationsPerRoute; ++operation)
                    RecordComputeRectCommands(command, image, compute_layout, compute_pipeline,
                                              descriptor_sets[0], packets[operation & 1U]);
            } else {
                ImageBarrier(command, image, vk::ImageLayout::eGeneral, memory,
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                    all, vk::PipelineStageFlagBits::eColorAttachmentOutput);
                command.beginRenderPass(vk::RenderPassBeginInfo{.renderPass = render_pass,
                    .framebuffer = framebuffer, .renderArea = {{0, 0}, {case_info.width, case_info.height}}},
                    vk::SubpassContents::eInline);
                command.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);
                const std::array graphics_sets{descriptor_sets[1], descriptor_sets[2]};
                command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics_layout,
                                           0, graphics_sets, {});
                command.pushConstants(graphics_layout, vk::ShaderStageFlagBits::eFragment,
                                      0, sizeof(constants), &constants);
                command.setViewport(0, vk::Viewport{0, 0, float(case_info.width), float(case_info.height), 0, 1});
                command.setScissor(0, vk::Rect2D{{0, 0}, {case_info.width, case_info.height}});
                command.bindVertexBuffers(0, upload_buffer, vk::DeviceSize{MaxImageBytes});
                for (u32 operation = 0; operation < OperationsPerRoute; ++operation)
                    command.draw(6, 1, (operation & 1U) * 6, 0);
                command.endRenderPass();
                ImageBarrier(command, image, vk::ImageLayout::eGeneral,
                             vk::AccessFlagBits::eColorAttachmentWrite, memory,
                             vk::PipelineStageFlagBits::eColorAttachmentOutput, all);
            }
            command.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, pool, pair * 4 + route * 2 + 1);
            ImageBarrier(command, image, vk::ImageLayout::eGeneral, memory,
                         vk::AccessFlagBits::eTransferRead, all, transfer);
            command.copyImageToBuffer(image, vk::ImageLayout::eGeneral, readback_handles[route], copy);
            const vk::BufferMemoryBarrier visible{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eHostRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = readback_handles[route], .offset = 0, .size = VK_WHOLE_SIZE};
            command.pipelineBarrier(transfer, vk::PipelineStageFlagBits::eHost, {}, {}, visible, {});
        }
    });
    // CodexAstraLocal: The existing next frame submission sends these commands;
    // no diagnostic Flush/Finish or replaced scheduler callback is introduced.
    tick = scheduler.CurrentTick();
    pending = true;
    submitted = false;
    image_initialized = true;
    // CodexAstraLocal: This is queue admission, not worker vkCmd execution;
    // only subsequent accepted/completed ticks authorize a complete result.
    enqueued_routes += 2;
    recorded_at = Clock::now();
    recording_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(recorded_at - begin).count();
}

// CodexAstraLocal: A completed submission is required before host invalidation
// or slot reuse. Every byte is checked against an independent integer oracle;
// timestamps alone never establish correct or even useful rendering.
bool ComputeBenchmark::Impl::Collect() {
    if (!pending) return false;
    const auto accepted = scheduler.TrySubmittedTick();
    if (!submitted && accepted && *accepted >= tick) {
        submitted_routes += 2;
        submitted = true;
    }
    if (!submitted || !scheduler.IsFree(tick)) return false;
    pending = false;
    completed_routes += 2;
    completed_ticks[next_pair] = scheduler.GetMasterSemaphore()->KnownGpuTick();
    const auto begin = Clock::now();
    latency_ns[next_pair] = std::chrono::duration_cast<std::chrono::nanoseconds>(begin - recorded_at).count();
    std::array<u64, 8> stamps{};
    const auto result = instance.GetDevice().getQueryPoolResults(*queries, next_pair * 4, 4,
        sizeof(stamps), stamps.data(), sizeof(u64) * 2,
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
    // CodexAstraLocal: This raw Vulkan-Hpp overload returns rather than throws
    // device loss, which must remain terminal for the shared renderer.
    if (result == vk::Result::eErrorDeviceLost)
        throw VideoCore::ShaderRecoveryError("Compute benchmark query lost the Vulkan device");
    query_results[next_pair] = stamps;
    if (result != vk::Result::eSuccess || !stamps[1] || !stamps[3] || !stamps[5] || !stamps[7]) {
        Fail("timestamp_unavailable"); return false;
    }
    std::array<double, 2> elapsed{};
    for (u32 route = 0; route < 2; ++route) {
        elapsed[route] = double((stamps[route * 4 + 2] - stamps[route * 4]) & timestamp_mask) * timestamp_period;
        if (!(elapsed[route] > 0) || !std::isfinite(elapsed[route]) ||
            elapsed[route] > double(DeadlineSeconds) * 1e9) {
            Fail("timestamp_invalid"); return false;
        }
        RequireVma(vmaInvalidateAllocation(readbacks[route].allocator, readbacks[route].allocation,
                                           0, VK_WHOLE_SIZE), "readback_invalidate");
    }
    const u32 case_id = next_pair / PairsPerCase;
    const auto c = Cases[case_id];
    const auto* graphics = static_cast<const u32*>(readbacks[0].mapped);
    const auto* compute = static_cast<const u32*>(readbacks[1].mapped);
    u64 touched = 0;
    for (u32 y = 0; y < c.height; ++y) {
        for (u32 x = 0; x < c.width; ++x) {
            const u32 expected = Expected(case_id, x, y);
            const auto offset = y * c.width + x;
            if (graphics[offset] != expected || compute[offset] != expected) {
                failed_pixel = {next_pair, x, y, graphics[offset], compute[offset], expected};
                has_failed_pixel = true;
                Fail("pixel_mismatch"); return false;
            }
            touched += expected != Background(x, y);
        }
    }
    if (!touched) { Fail("zero_useful_pixels"); return false; }
    touched_pixels[case_id] = touched;
    durations[next_pair] = elapsed;
    validated_routes += 2;
    ++next_pair;
    validation_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
    return true;
}

// CodexAstraLocal: Only all64 validated route outcomes authorize ratios. Partial
// results retain counts/reasons; the create-only report never overwrites an ID.
void ComputeBenchmark::Impl::Report() noexcept {
    if (reported) return;
    reported = true;
    try {
        const bool complete = status == "complete" && !pending && next_pair == CaseCount * PairsPerCase &&
            enqueued_routes == RouteCount && submitted_routes == RouteCount &&
            completed_routes == RouteCount && validated_routes == RouteCount;
        Json output{{"schema", 1}, {"id", id}, {"title_id", fmt::format("{:016X}", title)},
            {"source", std::string{Common::g_scm_rev}}, {"run", run}, {"status", status}, {"complete", complete},
            {"scope", "synthetic_supported_fragment_work_not_title_throughput"},
            {"vertex_input", "original_88_byte_cpu_HardwareVertex_no_guest_shader_execution"},
            {"timing_scope", "TOP_BOTTOM_route_including_barriers_one_graphics_pass_or_32_compute_dispatches"},
            {"outside_gpu_interval", "reset_upload_readback_host_preparation"},
            {"repetition", "two_overlapping_rectangles_repeated_16_times_idempotent_final_image"},
            {"preparation_ns", preparation_ns}, {"host_enqueue_wall_ns", recording_ns},
            {"host_enqueue_scope", "CPU_workload_setup_upload_flush_and_scheduler_enqueue_not_worker_vkCmd_time"},
            {"host_validation_wall_ns", validation_ns}, {"allocated_vma_bytes", allocated_bytes},
            {"allocation_accounting_scope", "successful_preparation_only_partial_failure_bytes_unknown"},
            {"raw_image_bytes", 2 * MaxImageBytes + 4}, {"raw_upload_bytes", UploadBytes},
            {"raw_readback_bytes", 2 * MaxImageBytes}, {"timestamp_period_ns", timestamp_period},
            {"timestamp_bits", timestamp_bits}, {"expected_routes", RouteCount},
            {"source_hash64", source_hashes}, {"module_hash64", module_hashes},
            {"module_words", module_words}, {"compiler_disable_optimizer", true},
            {"profile_clip_planes", bool(profile.has_clip_planes)},
            {"profile_logic_op", bool(profile.has_logic_op)},
            {"profile_accurate_mul", bool(profile.enable_accurate_mul)},
            {"enqueued_routes", enqueued_routes}, {"submitted_routes", submitted_routes},
            {"completed_routes", completed_routes}, {"validated_routes", validated_routes},
            {"operations_per_route", OperationsPerRoute}, {"pending_tick", pending ? tick : 0},
            {"enqueued_operations", enqueued_routes * OperationsPerRoute},
            {"operation_scope", "enqueued_route_lambdas_until_submission_and_GPU_completion"},
            {"lifecycle_scope", "observed_through_final_poll_pending_work_may_complete_after_incomplete_report"},
            {"failure_or_next_pair", next_pair},
            {"limits", {{"cases", CaseCount}, {"pairs_per_case", PairsPerCase},
                         {"queries", QueryCount}, {"soft_deadline_seconds", DeadlineSeconds},
                         {"max_inflight_pairs", 1}}}, {"pairs", Json::array()}};
        if (has_failed_pixel)
            output["pixel_failure_pair_xy_graphics_compute_expected"] = failed_pixel;
        if (next_pair < query_results.size())
            output["unvalidated_timestamp_and_availability"] = query_results[next_pair];
        for (u32 pair = 0; pair < next_pair; ++pair) {
            const auto c = Cases[pair / PairsPerCase];
            Json row{{"pair", pair}, {"case", pair / PairsPerCase}, {"extent", {c.width, c.height}},
                {"channels", c.channels}, {"order", pair & 1 ? "compute_graphics" : "graphics_compute"},
                {"graphics_ns", durations[pair][0]}, {"compute_ns", durations[pair][1]},
                {"timestamp_and_availability", query_results[pair]},
                {"observed_gpu_tick", completed_ticks[pair]}, {"readback_poll_latency_ns", latency_ns[pair]},
                {"changed_pixels", touched_pixels[pair / PairsPerCase]}};
            row["compute_over_graphics"] = complete ? Json(durations[pair][1] / durations[pair][0]) : Json(nullptr);
            output["pairs"].push_back(std::move(row));
        }
        const bool saved = WriteExclusive(result_path, output);
        // CodexAstraLocal Log Line: One bounded result marker contains no guest
        // payload; synthetic work never enters ordinary route/selector totals.
        LOG_INFO(Render_Vulkan,
            "Uberhar compute benchmark: schema=1 id={} status={} complete={} saved={} "
            "enqueued_routes={} completed_routes={} validated_routes={} scope=synthetic_scratch",
            id, status, complete, saved, enqueued_routes, completed_routes, validated_routes);
    } catch (...) {
        // CodexAstraLocal: The earlier persistent claim remains incomplete if
        // reporting fails; teardown must not rearm or disturb title execution.
    }
}

ComputeBenchmark::ComputeBenchmark(std::unique_ptr<Impl> value) : impl{std::move(value)} {}
ComputeBenchmark::~ComputeBenchmark() = default;

// CodexAstraLocal: This optional Thor diagnostic is admitted only by an explicit
// raw-filesystem request on Android, whose cache-load/RunLoop typed-error handling
// is established. Other frontends return before IO or resource construction.
std::unique_ptr<ComputeBenchmark> ComputeBenchmark::Load(const Instance& instance,
    Scheduler& scheduler, RenderManager& renderpass, const Pica::Shader::Profile& profile, u64 title) {
#if !defined(ANDROID) || defined(HAVE_LIBRETRO) || defined(HAVE_LIBRETRO_VFS)
    return {};
#else
    if (!Settings::RequiresComputeOnly(Settings::values.uberhar_test_mode.GetValue()) ||
        !title || !AndroidUtils::CanUseRawFS()) return {};
    std::unique_ptr<ComputeBenchmark> result;
    try {
        const auto path = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "uberhar_compute_benchmark.json";
        FileUtil::IOFile file{path, "rb"};
        if (!file.IsOpen() || file.GetSize() > 1024) return {};
        std::array<char, 1025> bytes{};
        const auto size = file.ReadBytes(bytes.data(), bytes.size());
        if (size > 1024) return {};
        u32 nodes = 0;
        std::array<std::string, 5> keys{};
        u32 key_count = 0;
        const auto request = Json::parse(bytes.data(), bytes.data() + size,
            [&](int depth, Json::parse_event_t event, Json& item) {
                if (++nodes > 32 || depth > 1) throw std::runtime_error("request_bound");
                if (event == Json::parse_event_t::key) {
                    const auto key = item.get<std::string>();
                    if (key_count == keys.size() || std::find(keys.begin(), keys.begin() + key_count, key) != keys.begin() + key_count)
                        throw std::runtime_error("duplicate_request_key");
                    keys[key_count++] = key;
                }
                return true;
            });
        if (!request.is_object() || request.size() != 5 ||
            !request.contains("schema") || !request.at("schema").is_number_unsigned() || request.at("schema") != 1 ||
            !request.contains("enabled") || !request.at("enabled").is_boolean() || !request.at("enabled").get<bool>() ||
            request.value("trigger", "") != "renderer_startup" ||
            request.value("title_id", "") != fmt::format("{:016X}", title)) return {};
        const auto id = request.at("benchmark_id").get<std::string>();
        if (id.empty() || id.size() > 48 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        })) return {};
        const auto base = FileUtil::GetUserPath(FileUtil::UserPath::DumpDir) + "compute-benchmark-" + id;
        // CodexAstraLocal: Commit the persistent consumed ID before compilation
        // or allocations. A crash/timeout/rights change leaves no implicit retry.
        if (!WriteExclusive(base + ".claimed.json", Json{{"schema", 1}, {"id", id},
                {"title_id", fmt::format("{:016X}", title)}, {"source", std::string{Common::g_scm_rev}},
                {"state", "consumed_results_pending"}})) return {};
        result.reset(new ComputeBenchmark(std::make_unique<Impl>(instance, scheduler, renderpass, profile,
                                                                 title, id, base + ".result.json")));
        result->impl->Prepare();
    } catch (const VideoCore::ShaderRecoveryError&) {
        throw;
    } catch (const vk::DeviceLostError&) {
        throw VideoCore::ShaderRecoveryError("Compute benchmark preparation lost the Vulkan device");
    } catch (...) {
        if (result) { result->impl->Fail("preparation_failed"); result->impl->Report(); }
    }
    return result;
#endif
}

// CodexAstraLocal: Timeout is a soft admission deadline, never cancellation of
// submitted Vulkan work. Owners remain until renderer shutdown's existing drain.
void ComputeBenchmark::Poll(u64 title) {
    auto& state = *impl;
    if (state.reported) return;
    try {
        // CodexAstraLocal: A sidecar authorizes one owning emulation run only;
        // an activity reset must not carry pending evidence into another run.
        const auto current_run = Common::UberharActivity::session.load(std::memory_order_relaxed);
        if (!state.run) state.run = current_run;
        else if (current_run != state.run) state.Fail("run_changed");
        if (title != state.title) state.Fail("title_changed");
        if (Clock::now() - state.started >= std::chrono::seconds{DeadlineSeconds}) state.Fail("deadline");
        if (state.status != "ready") { state.Report(); return; }
        if (state.pending && !state.Collect()) {
            if (state.status != "ready") state.Report();
            return;
        }
        // CodexAstraLocal: Host validation can cross the deadline too; it cannot
        // authorize one extra pair or a late complete-result claim.
        if (Clock::now() - state.started >= std::chrono::seconds{DeadlineSeconds}) {
            state.Fail("deadline"); state.Report(); return;
        }
        if (state.next_pair == CaseCount * PairsPerCase) {
            state.status = "complete"; state.Report(); return;
        }
        state.RecordPair();
    } catch (const VideoCore::ShaderRecoveryError&) {
        throw;
    } catch (const vk::DeviceLostError&) {
        throw VideoCore::ShaderRecoveryError("Compute benchmark execution lost the Vulkan device");
    } catch (...) {
        state.Fail("execution_failed"); state.Report();
    }
}

// CodexAstraLocal: Called only after the inherited scheduler/GPU drain. It
// records incomplete evidence on early exit without scheduling further work.
void ComputeBenchmark::FinishAfterDrain() noexcept {
    if (!impl->reported) {
        impl->Fail("renderer_exit");
        impl->Report();
    }
}

} // namespace Vulkan
