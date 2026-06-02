#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>

namespace ember::gpu {

enum class ImageUse { hdr_output, depth_output, sampled, shadow_sampled, transfer_read, present };
struct Resource {
    VkImage image{};
    VkImageAspectFlags aspect{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkPipelineStageFlags2 stage{};
    VkAccessFlags2 access{};
};
struct Use {
    Resource& image;
    ImageUse use;
};

// Ordered single-queue graph. Image allocations are dedicated to a frame slot.
// Reads require a writer or an explicit initialized import.
class RenderGraph {
public:
    explicit RenderGraph(VkCommandBuffer commands) : commands_(commands) {}

    template<class Record>
    void pass(std::initializer_list<Use> uses, Record&& record) const {
        std::array<VkImageMemoryBarrier2,8> barriers{};
        std::size_t count = 0;
        if (uses.size() > barriers.size()) throw std::logic_error("Pass image limit");
        for (const auto& [image, usage] : uses) {
            const auto next = state(usage);
            if ((usage == ImageUse::sampled || usage == ImageUse::shadow_sampled || usage == ImageUse::transfer_read || usage == ImageUse::present) &&
                image.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
                throw std::logic_error("Graph read of unwritten image");
            }
            for (std::size_t i=0; i<count; ++i) {
                if (barriers[i].image == image.image) throw std::logic_error("Duplicate pass image usage");
            }
            auto& barrier = barriers[count++];
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = image.stage;
            barrier.srcAccessMask = image.access;
            barrier.dstStageMask = next.stage;
            barrier.dstAccessMask = next.access;
            barrier.oldLayout = image.layout;
            barrier.newLayout = next.layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange = {image.aspect,0,1,0,1};
            image.layout = next.layout;
            image.stage = next.stage;
            image.access = next.access;
        }
        VkDependencyInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.imageMemoryBarrierCount = static_cast<std::uint32_t>(count);
        info.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(commands_, &info);
        record();
    }

private:
    static Resource state(ImageUse use) {
        switch (use) {
        case ImageUse::hdr_output:
            return {nullptr,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case ImageUse::depth_output:
            return {nullptr,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case ImageUse::sampled:
            return {nullptr,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        case ImageUse::shadow_sampled:
            return {nullptr,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        case ImageUse::transfer_read:
            return {nullptr,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT,VK_ACCESS_2_TRANSFER_READ_BIT};
        case ImageUse::present:
            return {nullptr,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,0,0};
        }
        throw std::logic_error("Unknown graph use");
    }
    VkCommandBuffer commands_;
};

}
