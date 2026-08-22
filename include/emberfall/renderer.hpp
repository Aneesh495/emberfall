#pragma once

#include "emberfall/effects.hpp"

#include <memory>

struct SDL_Window;

namespace ember {

struct View {
    Vec3 eye, target;
    float fov{0.88f};
    float exposure{1};
    bool antialias{true};
    bool dense_mesh{};
    float far_plane{160};
    bool local_shadows{true};
    bool temporal{};
};

struct OverlayVertex {
    float x{}, y{};
    std::array<float,4> color{};
};

struct ImageTransition {
    std::string_view resource, previous_layout, layout;
    std::uint64_t source_stage{}, destination_stage{}, source_access{}, destination_access{};
    std::uint32_t levels{}, layers{};
};

struct PassReport {
    std::string_view name;
    std::array<ImageTransition,8> images;
    std::size_t image_count{};
};

struct GraphReport {
    std::array<PassReport,8> passes;
    std::size_t pass_count{};
};

struct FrameReport {
    std::uint32_t width{}, height{}, submitted{}, culled{};
    double gpu_ms{};
    std::uint64_t serial{};
    std::uint32_t shadow_casters{}, lights{}, triangles{}, draw_calls{};
    std::uint64_t timed_serial{};
    std::array<std::uint64_t,6> timestamps{};
    double timestamp_period_ns{};
    unsigned timestamp_valid_bits{};
    std::uint32_t imported_triangles{}, light_tiles{}, tile_light_entries{};
    std::uint32_t shadow_maps{}, shadow_draws{};
    std::uint32_t motion_items{}, animated_triangles{};
    bool history_used{};
    std::uint32_t texture_layers{}, uploaded_texture_layers{};
    std::array<double,5> pass_gpu_ms{};
    std::uint64_t owned_bytes{};
    std::uint32_t particles{}, decals{};
};

class Renderer {
public:
    Renderer(SDL_Window* window, std::filesystem::path shaders, bool validation);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize();
    void reset_history();
    bool draw(std::span<const RenderItem> items, const View& view, std::span<const OverlayVertex> overlay = {},
              std::span<const SkinBatch> meshes = {}, std::span<const MeshInstance> imported = {},
              std::span<const Effect> effects = {});
    void capture_next(std::filesystem::path file);
    const FrameReport& report() const;
    const GraphReport& graph_report() const;
    std::string adapter() const;
    unsigned validation_errors() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
