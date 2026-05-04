#pragma once

#include "emberfall/campaign.hpp"
#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>

namespace ember::native {

struct Input {
    Vec3 movement;
    bool quit{}, resized{}, pause{}, enter{}, select{}, jump{}, interact{}, crouch{}, fullscreen{};
    bool up{}, down{}, left{}, right{}, lost_focus{}, restart{};
};

class Platform {
public:
    Platform(const char* title, int width, int height, bool audio);
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Input input();
    void set_fullscreen(bool enabled) const;
    SDL_Window* window() const { return window_; }
private:
    SDL_Window* window_{};
    SDL_Gamepad* pad_{};
    bool focused_{true};
};

struct Color {
    float r{},g{},b{},a{1};
};

class Canvas {
public:
    explicit Canvas(float logical_width) : width_(logical_width) {}
    void box(float x,float y,float w,float h,Color c);
    void text(float x,float y,float size,std::string_view content,Color c);
    void wrapped(float x,float y,float size,std::string_view content,Color c);
    std::span<const OverlayVertex> vertices() const { return vertices_; }
    float width() const { return width_; }
private:
    float width_;
    std::vector<OverlayVertex> vertices_;
};

std::filesystem::path executable_directory();
std::filesystem::path profile_directory();

}
