#include "state.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstring>
#include <iostream>
#include <string_view>

namespace ember {
using gpu::check;
using gpu::enumerate;
using gpu::vk;

namespace {

bool extension(std::span<const VkExtensionProperties> extensions, const char* name) {
    return std::ranges::any_of(extensions, [name](const auto& e) { return std::string_view(e.extensionName) == name; });
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void* data) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++*static_cast<std::atomic<unsigned>*>(data);
    }
    std::cerr << "Vulkan validation: " << message->pMessage << '\n';
    return VK_FALSE;
}

auto debug_info(std::atomic<unsigned>& errors) {
    auto info = vk<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_message;
    info.pUserData = &errors;
    return info;
}

}

Renderer::Renderer(SDL_Window* window, std::filesystem::path shaders, bool validation)
    : state_(std::make_unique<State>()) { state_->init(window,std::move(shaders),validation); }
Renderer::~Renderer() = default;
void Renderer::resize() { state_->dirty = true; }
const FrameReport& Renderer::report() const { return state_->report; }
unsigned Renderer::validation_errors() const { return state_->errors; }

std::string Renderer::adapter() const {
    const auto& p = state_->properties;
    return std::string(p.deviceName) + " Vulkan " + std::to_string(VK_API_VERSION_MAJOR(p.apiVersion)) +
           "." + std::to_string(VK_API_VERSION_MINOR(p.apiVersion)) + "." +
           std::to_string(VK_API_VERSION_PATCH(p.apiVersion)) + " driver " + std::to_string(p.driverVersion);
}

void Renderer::State::init(SDL_Window* owner, std::filesystem::path path, bool validation) {
    window = owner;
    shaders = std::move(path);
    if (!SDL_Vulkan_LoadLibrary(nullptr)) throw std::runtime_error(SDL_GetError());
    loaded = true;
    create_instance(validation);
    if (!SDL_Vulkan_CreateSurface(window,instance,nullptr,&surface)) throw std::runtime_error(SDL_GetError());
    create_device();
    create_pipeline_layouts();
    create_frames();
    auto vertices = gpu::cube(1);
    const auto dense = gpu::cube(4);
    vertices.insert(vertices.end(),dense.begin(),dense.end());
    const auto bytes = vertices.size()*sizeof(gpu::Vertex);
    make_buffer(geometry,bytes,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    std::memcpy(geometry.mapped,vertices.data(),bytes);
    shadow_pipeline = gpu::pipeline(device,shaders,shadow_layout,VK_FORMAT_UNDEFINED,depth_format,gpu::Pipeline::shadow);
    visibility.resize(instance_limit);
    scene_pipeline = gpu::pipeline(device,shaders,scene_layout,hdr_format,depth_format,gpu::Pipeline::scene);
}

void Renderer::State::create_instance(bool validation) {
    std::uint32_t number = 0;
    const char* const* required = SDL_Vulkan_GetInstanceExtensions(&number);
    if (!required) throw std::runtime_error(SDL_GetError());
    std::vector<const char*> enabled(required,required+number);
    const auto supported = enumerate<VkExtensionProperties>([](auto count, auto* list) {
        return vkEnumerateInstanceExtensionProperties(nullptr,count,list);
    });
    auto info = vk<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    const auto add = [&](const char* name) {
        if (!std::ranges::any_of(enabled,[name](const char* old) { return std::string_view(old) == name; })) {
            enabled.push_back(name);
        }
    };
    if (extension(supported,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        add(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    auto debug = debug_info(errors);
    const char* layer = "VK_LAYER_KHRONOS_validation";
    auto features = vk<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
    constexpr VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    if (validation) {
        const auto layers = enumerate<VkLayerProperties>(vkEnumerateInstanceLayerProperties);
        if (!std::ranges::any_of(layers,[layer](const auto& entry) { return std::string_view(entry.layerName) == layer; }) ||
            !extension(supported,VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            throw std::runtime_error("Validation requested but validation layer or debug utils unavailable");
        }
        add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const auto supplied = enumerate<VkExtensionProperties>([layer](auto count, auto* list) {
            return vkEnumerateInstanceExtensionProperties(layer,count,list);
        });
        if (!extension(supplied,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)) {
            throw std::runtime_error("Synchronization validation extension unavailable");
        }
        add(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = &sync;
        features.pNext = &debug;
        info.pNext = &features;
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &layer;
    }
    auto app = vk<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "The Lantern Reach";
    app.pEngineName = "Emberfall";
    app.apiVersion = VK_API_VERSION_1_3;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();
    check(vkCreateInstance(&info,nullptr,&instance),"Create Vulkan 1.3 instance");
    if (validation) {
        const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
        if (!create) throw std::runtime_error("Debug messenger entry unavailable");
        check(create(instance,&debug,nullptr,&messenger),"Create validation messenger");
    }
}

void Renderer::State::create_device() {
    const auto devices = enumerate<VkPhysicalDevice>([&](auto count, auto* list) {
        return vkEnumeratePhysicalDevices(instance,count,list);
    });
    auto v12 = vk<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    auto v13 = vk<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    auto features = vk<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    v12.pNext = &v13;
    features.pNext = &v12;
    std::vector<const char*> enabled;
    unsigned best_score = 0;
    for (auto adapter : devices) {
        VkPhysicalDeviceProperties candidate{};
        vkGetPhysicalDeviceProperties(adapter,&candidate);
        if (candidate.apiVersion < VK_API_VERSION_1_3) continue;
        vkGetPhysicalDeviceFeatures2(adapter,&features);
        if (!v13.dynamicRendering || !v13.synchronization2 || !v12.timelineSemaphore) continue;
        VkFormatProperties hdr{}, depth{};
        vkGetPhysicalDeviceFormatProperties(adapter,hdr_format,&hdr);
        vkGetPhysicalDeviceFormatProperties(adapter,depth_format,&depth);
        constexpr auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        constexpr auto required_depth = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((hdr.optimalTilingFeatures & required) != required ||
            (depth.optimalTilingFeatures & required_depth) != required_depth) continue;
        const auto extensions = enumerate<VkExtensionProperties>([&](auto count, auto* list) {
            return vkEnumerateDeviceExtensionProperties(adapter,nullptr,count,list);
        });
        if (!extension(extensions,VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(adapter,&count,nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(adapter,&count,queues.data());
        const unsigned score = candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3U :
                               candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2U : 1U;
        for (std::uint32_t i=0; i<count && score>best_score; ++i) {
            VkBool32 presentation = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(adapter,i,surface,&presentation),"Query presentation queue");
            if (!presentation || !(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            best_score = score;
            physical = adapter;
            properties = candidate;
            family = i;
            timestamp_bits = queues[i].timestampValidBits;
            enabled = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            if (extension(extensions,"VK_KHR_portability_subset")) enabled.push_back("VK_KHR_portability_subset");
        }
    }
    if (!physical) throw std::runtime_error("No device with Vulkan 1.3, synchronization2, dynamic rendering, timeline and HDR/depth formats");
    v12 = vk<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    v12.timelineSemaphore = VK_TRUE;
    v13 = vk<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    v12.pNext = &v13;
    const float priority = 1;
    auto request = vk<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    request.queueFamilyIndex = family;
    request.queueCount = 1;
    request.pQueuePriorities = &priority;
    auto info = vk<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    info.pNext = &v12;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &request;
    info.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();
    check(vkCreateDevice(physical,&info,nullptr,&device),"Create device with declared feature set");
    vkGetDeviceQueue(device,family,0,&queue);
}

Renderer::State::~State() {
    if (device) {
        vkDeviceWaitIdle(device);
        clear_swapchain();
        for (auto& frame : frames) {
            free(frame.instances);
            free(frame.overlay);
            free(frame.lighting);
            free(frame.skin);
            free(frame.skin_instances);
            free(frame.shadow);
            vkDestroyQueryPool(device,frame.timer,nullptr);
            vkDestroySemaphore(device,frame.acquire,nullptr);
            vkDestroyCommandPool(device,frame.pool,nullptr);
        }
        free(geometry);
        vkDestroySampler(device,sampler,nullptr);
        vkDestroySampler(device,shadow_sampler,nullptr);
        vkDestroySemaphore(device,timeline,nullptr);
        vkDestroyPipeline(device,scene_pipeline,nullptr);
        vkDestroyPipeline(device,shadow_pipeline,nullptr);
        vkDestroyPipelineLayout(device,scene_layout,nullptr);
        vkDestroyPipelineLayout(device,shadow_layout,nullptr);
        vkDestroyPipelineLayout(device,tonemap_layout,nullptr);
        vkDestroyPipelineLayout(device,overlay_layout,nullptr);
        vkDestroyDescriptorSetLayout(device,sample_layout,nullptr);
        vkDestroyDescriptorSetLayout(device,light_layout,nullptr);
        vkDestroyDevice(device,nullptr);
    }
    if (surface) SDL_Vulkan_DestroySurface(instance,surface,nullptr);
    if (messenger) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
        destroy(instance,messenger,nullptr);
    }
    if (instance) vkDestroyInstance(instance,nullptr);
    if (loaded) SDL_Vulkan_UnloadLibrary();
}

}
