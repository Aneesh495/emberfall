#pragma once

#include "common.hpp"
#include "render_graph.hpp"
#include "emberfall/scheduler.hpp"
#include "emberfall/lighting.hpp"
#include "emberfall/shadow.hpp"

#include <atomic>
#include <unordered_map>

namespace ember {

struct Renderer::State {
    static constexpr std::size_t slots = 2;
    static constexpr std::uint32_t instance_limit = 32768;
    static constexpr std::uint32_t overlay_limit = 131072;
    static constexpr VkFormat hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    static constexpr std::uint32_t shadow_instance_limit = instance_limit*4;
    static constexpr auto light_limit = TileLights::light_limit;
    struct Light {
        std::array<float,4> position_radius;
        std::array<float,4> color_power;
    };
    struct Lighting {
        Mat4 previous_camera;
        std::array<Mat4,ShadowViews::limit> shadow;
        std::array<float,4> camera_forward, cascade_ends, direction_count;
        std::array<std::uint32_t,4> tile_grid, shadow_options;
        std::array<float,4> viewport;
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
    using Motion = Mat4;
    struct Frame {
        VkCommandPool pool{};
        VkCommandBuffer commands{};
        VkSemaphore acquire{};
        std::uint64_t finished{};
        VkQueryPool timer{};
        Buffer instances, overlay, lighting, skin, skin_instances, light_tiles;
        Buffer motions, mesh_motions, texture_staging;
        Image hdr, depth, shadow, motion, history, textures;
        VkImageView color_textures{};
        std::uint32_t resident_layers{};
        bool textures_initialized{};
        VkDescriptorSet sample{}, history_sample{}, resolve_set{}, resolve_first_set{}, light_set{};
        bool attachments_written{}, shadow_written{}, history_written{}, motion_sampled{};
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
              std::span<const SkinBatch> meshes, std::span<const MeshInstance> imported);
    static constexpr std::size_t skin_vertex_limit = 4096, skin_mesh_limit = 16;
    static constexpr auto uploaded_vertex_limit = skin_vertex_limit+MeshLibrary::vertex_limit;
    static constexpr auto uploaded_mesh_limit = skin_mesh_limit+MeshLibrary::primitive_limit;
    struct SkinDraw { std::uint32_t first{}, count{}; };
    struct ShadowDraw { Mat4 projection; std::uint32_t first{}, count{}; };
    void record(Frame& frame, const Frame& previous, Screen& screen, const View& view, const Mat4& camera,
                bool temporal, bool history, std::uint32_t count, std::uint32_t dirty_layers,
                std::span<const ShadowDraw> shadows,std::uint32_t overlay_count,std::span<const SkinDraw> meshes);
    void save_capture();
    void wait(std::uint64_t serial) const;
    void make_image(Image& image, VkFormat format, VkImageUsageFlags use, VkImageAspectFlags aspect,
                    VkExtent2D dimensions = {}, std::uint32_t levels = 1, std::uint32_t layers = 1,
                    VkImageCreateFlags flags = 0);
    void make_buffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags use);
    void free(Image& image);
    void free(Buffer& buffer);
    VkImageView make_view(VkImage image, VkFormat format, VkImageAspectFlags aspect,
                          std::uint32_t levels = 1, std::uint32_t layers = 1) const;
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
    VkDescriptorSetLayout sample_layout{}, light_layout{}, resolve_layout{};
    VkPipelineLayout scene_layout{}, tonemap_layout{}, overlay_layout{}, shadow_layout{}, temporal_layout{};
    VkPipeline scene_pipeline{}, tonemap_pipeline{}, overlay_pipeline{}, shadow_pipeline{}, temporal_pipeline{};
    VkSampler sampler{}, shadow_sampler{}, material_sampler{};
    Scheduler culling{4};
    TileLights tile_lights;
    std::vector<std::uint16_t> visibility;
    Buffer geometry, capture;
    std::filesystem::path capture_file;
    bool capture_supported{};
    std::uint64_t serial{};
    bool dirty{true};
    bool loaded{};
    std::unordered_map<std::uint64_t,Mat4> previous_models, next_models;
    Mat4 previous_camera;
    View previous_view{};
    Vec3 previous_forward{};
    std::uint32_t jitter_index{};
    bool history_active{};
    FrameReport report{};
    std::atomic<unsigned> errors{};
};

}
