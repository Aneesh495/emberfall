#pragma once

#include "emberfall/renderer.hpp"

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace ember::gpu {

template<class T>
T vk(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}

inline void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(result) + ")");
    }
}

template<class T, class Enumerate>
std::vector<T> enumerate(Enumerate&& call) {
    std::uint32_t count = 0;
    check(call(&count,static_cast<T*>(nullptr)), "Enumerate Vulkan properties");
    std::vector<T> out(count);
    if (!count) return out;
    check(call(&count,out.data()), "Read Vulkan properties");
    out.resize(count);
    return out;
}

using Vertex = MeshVertex;
std::vector<Vertex> cube(unsigned subdivisions);
bool visible(const Mat4& view_projection, const Mat4& model);
enum class Pipeline { scene, temporal, tonemap, overlay, shadow };
VkPipeline pipeline(VkDevice device, const std::filesystem::path& dir,
                    VkPipelineLayout layout, VkFormat color, VkFormat depth, Pipeline kind);

}
