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
};

struct FrameReport {
    std::uint32_t width{}, height{}, submitted{}, culled{};
    double gpu_ms{};
    std::uint64_t serial{};
};

class Renderer {
public:
    Renderer(SDL_Window* window, std::filesystem::path shaders, bool validation);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize();
    bool draw(std::span<const RenderItem> items, const View& view);
    const FrameReport& report() const;
    std::string adapter() const;
    unsigned validation_errors() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
