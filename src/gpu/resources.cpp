#include "state.hpp"

#include <SDL3/SDL.h>

#include <limits>

namespace ember {
using gpu::check;
using gpu::enumerate;
using gpu::vk;

std::uint32_t Renderer::State::memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    for (std::uint32_t i=0; i<memory.memoryTypeCount; ++i) {
        if ((bits & (1U<<i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    throw std::runtime_error("No compatible GPU memory type");
}

VkImageView Renderer::State::make_view(VkImage image, VkFormat format, VkImageAspectFlags aspect) const {
    auto view = vk<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect,0,1,0,1};
    VkImageView result{};
    check(vkCreateImageView(device,&view,nullptr,&result),"Create image view");
    return result;
}

void Renderer::State::make_image(Image& image, VkFormat format, VkImageUsageFlags use, VkImageAspectFlags aspect) {
    auto info = vk<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width,extent.height,1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = use;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateImage(device,&info,nullptr,&image.image),"Create graph image");
    VkMemoryRequirements request{};
    vkGetImageMemoryRequirements(device,image.image,&request);
    auto allocation = vk<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = request.size;
    allocation.memoryTypeIndex = memory_type(request.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device,&allocation,nullptr,&image.memory),"Allocate graph image memory");
    check(vkBindImageMemory(device,image.image,image.memory,0),"Bind graph image memory");
    image.view = make_view(image.image,format,aspect);
}

void Renderer::State::make_buffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags use) {
    auto info = vk<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = size;
    info.usage = use;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device,&info,nullptr,&buffer.buffer),"Create upload buffer");
    VkMemoryRequirements request{};
    vkGetBufferMemoryRequirements(device,buffer.buffer,&request);
    auto allocation = vk<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = request.size;
    allocation.memoryTypeIndex = memory_type(request.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device,&allocation,nullptr,&buffer.memory),"Allocate coherent upload memory");
    check(vkBindBufferMemory(device,buffer.buffer,buffer.memory,0),"Bind buffer memory");
    check(vkMapMemory(device,buffer.memory,0,VK_WHOLE_SIZE,0,&buffer.mapped),"Map buffer memory");
}

void Renderer::State::free(Image& image) {
    vkDestroyImageView(device,image.view,nullptr);
    vkDestroyImage(device,image.image,nullptr);
    vkFreeMemory(device,image.memory,nullptr);
    image = {};
}

void Renderer::State::free(Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(device,buffer.memory);
    vkDestroyBuffer(device,buffer.buffer,nullptr);
    vkFreeMemory(device,buffer.memory,nullptr);
    buffer = {};
}

void Renderer::State::create_pipeline_layouts() {
    const VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    auto sample_info = vk<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    sample_info.bindingCount = 1;
    sample_info.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device,&sample_info,nullptr,&sample_layout),"Create tonemap descriptor layout");
    const VkPushConstantRange camera{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0,80};
    auto layout = vk<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &camera;
    check(vkCreatePipelineLayout(device,&layout,nullptr,&scene_layout),"Create scene pipeline layout");
    const VkPushConstantRange display{VK_SHADER_STAGE_FRAGMENT_BIT,0,12};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &sample_layout;
    layout.pPushConstantRanges = &display;
    check(vkCreatePipelineLayout(device,&layout,nullptr,&tonemap_layout),"Create tonemap pipeline layout");
    const VkPushConstantRange overlay{VK_SHADER_STAGE_FRAGMENT_BIT,0,4};
    layout.setLayoutCount = 0;
    layout.pSetLayouts = nullptr;
    layout.pPushConstantRanges = &overlay;
    check(vkCreatePipelineLayout(device,&layout,nullptr,&overlay_layout),"Create overlay pipeline layout");
    auto sampling = vk<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampling.magFilter = VK_FILTER_LINEAR;
    sampling.minFilter = VK_FILTER_LINEAR;
    sampling.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampling.addressModeU = sampling.addressModeV = sampling.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device,&sampling,nullptr,&sampler),"Create HDR sampler");
}

void Renderer::State::create_frames() {
    auto type = vk<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    auto semaphore = vk<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphore.pNext = &type;
    check(vkCreateSemaphore(device,&semaphore,nullptr,&timeline),"Create frame completion timeline");
    semaphore.pNext = nullptr;
    for (auto& frame : frames) {
        auto pool = vk<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool.queueFamilyIndex = family;
        check(vkCreateCommandPool(device,&pool,nullptr,&frame.pool),"Create frame command pool");
        auto allocate = vk<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocate.commandPool = frame.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device,&allocate,&frame.commands),"Allocate frame commands");
        check(vkCreateSemaphore(device,&semaphore,nullptr,&frame.acquire),"Create frame acquire semaphore");
        if (timestamp_bits) {
            auto query = vk<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = 2;
            check(vkCreateQueryPool(device,&query,nullptr,&frame.timer),"Create frame timer queries");
        }
        make_buffer(frame.instances,instance_limit*sizeof(RenderItem),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        make_buffer(frame.overlay,overlay_limit*sizeof(OverlayVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
}

void Renderer::State::clear_swapchain() {
    vkDestroyPipeline(device,tonemap_pipeline,nullptr);
    tonemap_pipeline = nullptr;
    vkDestroyPipeline(device,overlay_pipeline,nullptr);
    overlay_pipeline = nullptr;
    free(capture);
    vkDestroyDescriptorPool(device,descriptors,nullptr);
    descriptors = nullptr;
    for (auto& frame : frames) {
        free(frame.hdr);
        free(frame.depth);
        frame.sample = nullptr;
    }
    for (const auto& screen : screens) {
        vkDestroySemaphore(device,screen.ready,nullptr);
        vkDestroyImageView(device,screen.view,nullptr);
    }
    screens.clear();
    vkDestroySwapchainKHR(device,swapchain,nullptr);
    swapchain = nullptr;
}

bool Renderer::State::rebuild_swapchain() {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window,&width,&height)) throw std::runtime_error(SDL_GetError());
    if (width <= 0 || height <= 0) return false;
    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"Query swapchain capabilities");
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) return false;
    check(vkDeviceWaitIdle(device),"Drain device before swapchain rebuild");
    clear_swapchain();
    const auto formats = enumerate<VkSurfaceFormatKHR>([&](auto count, auto* list) {
        return vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,count,list);
    });
    std::optional<VkSurfaceFormatKHR> preferred;
    for (auto format : {VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM}) {
        const auto chosen = std::ranges::find_if(formats,[format](auto candidate) {
            return candidate.format == format && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (chosen != formats.end()) {
            preferred = *chosen;
            break;
        }
    }
    if (!preferred) throw std::runtime_error("No supported sRGB presentation format");
    screen_format = preferred->format;
    extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        extent.width = std::clamp(static_cast<std::uint32_t>(width),caps.minImageExtent.width,caps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<std::uint32_t>(height),caps.minImageExtent.height,caps.maxImageExtent.height);
    }
    if (extent.width > 4096 || extent.height > 4096) throw std::runtime_error("Surface exceeds the 4096x4096 allocation budget");
    auto info = vk<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface;
    info.minImageCount = std::max(caps.minImageCount,3U);
    if (caps.maxImageCount) info.minImageCount = std::min(info.minImageCount,caps.maxImageCount);
    info.imageFormat = screen_format;
    info.imageColorSpace = preferred->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    capture_supported = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (capture_supported) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    for (auto flag : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if (!(caps.supportedCompositeAlpha & static_cast<VkCompositeAlphaFlagsKHR>(flag))) continue;
        info.compositeAlpha = flag;
        break;
    }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device,&info,nullptr,&swapchain),"Create FIFO swapchain");
    const auto images = enumerate<VkImage>([&](auto count, auto* list) {
        return vkGetSwapchainImagesKHR(device,swapchain,count,list);
    });
    const auto semaphore = vk<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    screens.resize(images.size());
    for (std::size_t i=0; i<images.size(); ++i) {
        auto& screen = screens[i];
        screen.image = images[i];
        screen.view = make_view(screen.image,screen_format,VK_IMAGE_ASPECT_COLOR_BIT);
        check(vkCreateSemaphore(device,&semaphore,nullptr,&screen.ready),"Create present semaphore");
    }
    tonemap_pipeline = gpu::pipeline(device,shaders,tonemap_layout,screen_format,VK_FORMAT_UNDEFINED,gpu::Pipeline::tonemap);
    overlay_pipeline = gpu::pipeline(device,shaders,overlay_layout,screen_format,VK_FORMAT_UNDEFINED,gpu::Pipeline::overlay);
    const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,slots};
    auto pool = vk<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = slots;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &pool_size;
    check(vkCreateDescriptorPool(device,&pool,nullptr,&descriptors),"Create frame descriptors");
    for (auto& frame : frames) {
        make_image(frame.hdr,hdr_format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        make_image(frame.depth,depth_format,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        auto set = vk<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        set.descriptorPool = descriptors;
        set.descriptorSetCount = 1;
        set.pSetLayouts = &sample_layout;
        check(vkAllocateDescriptorSets(device,&set,&frame.sample),"Allocate HDR descriptor");
        const VkDescriptorImageInfo image{sampler,frame.hdr.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto update = vk<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        update.dstSet = frame.sample;
        update.descriptorCount = 1;
        update.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        update.pImageInfo = &image;
        vkUpdateDescriptorSets(device,1,&update,0,nullptr);
    }
    dirty = false;
    return true;
}

}
