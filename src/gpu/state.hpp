#pragma once

#include "common.hpp"
#include "render_graph.hpp"
#include "emberfall/scheduler.hpp"

#include <atomic>

namespace ember {

struct Renderer::State {
    static constexpr std::size_t slots = 2;
    static constexpr std::uint32_t instance_limit = 32768;
    static constexpr std::uint32_t overlay_limit = 131072;
    static constexpr VkFormat hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    static constexpr std::uint32_t shadow_size = 1536;
    static constexpr std::size_t light_limit = 128;
    struct Light {
        std::array<float,4> position_radius;
        std::array<float,4> color_power;
    };
    struct Lighting {
        Mat4 shadow;
        std::array<float,4> direction_count;
        std::array<Light,light_limit> lights{};
    };
    struct Image {
        VkImage image{};
        VkImageView view{};
        VkDeviceMemory memory{};
    };
    struct Buffer {
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        void* mapped{};
    };
    struct Frame {
        VkCommandPool pool{};
        VkCommandBuffer commands{};
        VkSemaphore acquire{};
        std::uint64_t finished{};
        VkQueryPool timer{};
        Buffer instances, overlay, lighting, skin, skin_instances;
        Image hdr, depth, shadow;
        VkDescriptorSet sample{}, light_set{};
        bool attachments_written{}, shadow_written{};
    };
    struct Screen {
        VkImage image{};
        VkImageView view{};
        VkSemaphore ready{};
        bool presented{};
    };

    ~State();
    void init(SDL_Window* window, std::filesystem::path shaders, bool validation);
    void create_instance(bool validation);
    void create_device();
    void create_pipeline_layouts();
    void create_frames();
    void clear_swapchain();
    bool rebuild_swapchain();
    bool draw(std::span<const RenderItem> items, const View& view, std::span<const OverlayVertex> overlay,
              std::span<const SkinBatch> meshes);
    static constexpr std::size_t skin_vertex_limit = 4096, skin_mesh_limit = 16;
    struct SkinDraw { std::uint32_t first{}, count{}; };
    void record(Frame& frame, Screen& screen, const View& view, const Mat4& camera, const Mat4& light, std::uint32_t count, std::uint32_t shadow_count,
                std::uint32_t overlay_count,std::span<const SkinDraw> meshes);
    void save_capture();
    void wait(std::uint64_t serial) const;
    void make_image(Image& image, VkFormat format, VkImageUsageFlags use, VkImageAspectFlags aspect, VkExtent2D dimensions = {});
    void make_buffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags use);
    void free(Image& image);
    void free(Buffer& buffer);
    VkImageView make_view(VkImage image, VkFormat format, VkImageAspectFlags aspect) const;
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const;

    SDL_Window* window{};
    std::filesystem::path shaders;
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t family{};
    std::uint32_t timestamp_bits{};
    VkSwapchainKHR swapchain{};
    VkFormat screen_format{};
    VkExtent2D extent{};
    std::vector<Screen> screens;
    std::array<Frame,slots> frames;
    VkSemaphore timeline{};
    VkDescriptorPool descriptors{};
    VkDescriptorSetLayout sample_layout{}, light_layout{};
    VkPipelineLayout scene_layout{}, tonemap_layout{}, overlay_layout{}, shadow_layout{};
    VkPipeline scene_pipeline{}, tonemap_pipeline{}, overlay_pipeline{}, shadow_pipeline{};
    VkSampler sampler{}, shadow_sampler{};
    Scheduler culling{4};
    std::vector<std::uint8_t> visibility;
    Buffer geometry, capture;
    std::filesystem::path capture_file;
    bool capture_supported{};
    std::uint64_t serial{};
    bool dirty{true};
    bool loaded{};
    FrameReport report{};
    std::atomic<unsigned> errors{};
};

}
