#pragma once

#include "emberfall/game.hpp"

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
};

struct OverlayVertex {
    float x{}, y{};
    std::array<float,4> color{};
};

struct FrameReport {
    std::uint32_t width{}, height{}, submitted{}, culled{};
    double gpu_ms{};
    std::uint64_t serial{};
    std::uint32_t shadow_casters{}, lights{}, triangles{}, draw_calls{};
    std::uint64_t timed_serial{};
    std::array<std::uint64_t,4> timestamps{};
    double timestamp_period_ns{};
    unsigned timestamp_valid_bits{};
};

class Renderer {
public:
    Renderer(SDL_Window* window, std::filesystem::path shaders, bool validation);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize();
    bool draw(std::span<const RenderItem> items, const View& view, std::span<const OverlayVertex> overlay = {},
              std::span<const SkinBatch> meshes = {});
    void capture_next(std::filesystem::path file);
    const FrameReport& report() const;
    std::string adapter() const;
    unsigned validation_errors() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
