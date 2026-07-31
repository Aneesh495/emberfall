#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>
#include <iostream>

using namespace ember;

namespace {
std::pair<int,int> screen(const View& view,Vec3 world,int width,int height) {
    const auto vp = perspective(view.fov,static_cast<float>(width)/static_cast<float>(height),0.1f,view.far_plane)*
                    look_at(view.eye,view.target,{0,1,0});
    const auto p = transform_point(vp,world);
    const float w = vp(3,0)*world.x+vp(3,1)*world.y+vp(3,2)*world.z+vp(3,3);
    return {static_cast<int>((p.x/w*0.5f+0.5f)*static_cast<float>(width)),
            static_cast<int>((p.y/w*0.5f+0.5f)*static_cast<float>(height))};
}

int region(std::string_view bytes, int width, int x, int y) {
    std::size_t start = 0;
    for (int i=0; i<3; ++i) {
        start = bytes.find('\n',start);
        if (start==std::string_view::npos) throw std::runtime_error("Invalid captured PPM");
        ++start;
    }
    int brightness = 0;
    for (int dy=-2; dy<=2; ++dy) for (int dx=-2; dx<=2; ++dx) {
        const auto index = start+static_cast<std::size_t>((y+dy)*width+x+dx)*3;
        for (unsigned c=0; c<3; ++c) brightness += static_cast<unsigned char>(bytes.at(index+c));
    }
    return brightness;
}
}

int main(int argc,char** argv) {
    try {
        if (argc!=3 || !SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error("Usage: gpu_shadow SHADERS CAPTURE_DIRECTORY");
        const auto shutdown = std::unique_ptr<void,void(*)(void*)>{reinterpret_cast<void*>(1),[](void*) { SDL_Quit(); }};
        const auto window = std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>{
            SDL_CreateWindow("Emberfall shadow fixture",720,480,SDL_WINDOW_VULKAN),SDL_DestroyWindow};
        if (!window) throw std::runtime_error(SDL_GetError());
        Renderer renderer(window.get(),argv[1],true);
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        const std::array items{
            RenderItem{transform_matrix({0,-0.1f,0},{},{8,0.2f,8}),{0.46f,0.46f,0.46f},0},
            RenderItem{transform_matrix({0,0.7f,0},{},{0.3f,1.4f,1.4f}),{0.3f,0.3f,0.3f},0},
            RenderItem{transform_matrix({-1.25f,0.78f,0},{},{0.13f,0.13f,0.13f}),{1,0.65f,0.26f},4.0f}
        };
        View view{{0,5,6},{0,0,0},0.8f,1,false};
        for (const bool enabled : {false,true}) {
            view.local_shadows = enabled;
            renderer.capture_next(output/(enabled ? "point-on.ppm" : "point-off.ppm"));
            for (unsigned retries=0; !renderer.draw(items,view); ++retries) {
                if (retries==30) throw std::runtime_error("Shadow fixture window unavailable");
                SDL_PumpEvents();
                SDL_Delay(5);
            }
            if (renderer.report().shadow_maps!=(enabled ? 9U : 3U)) throw std::runtime_error("Shadow pass count mismatch");
        }
        const auto on = read_bounded(output/"point-on.ppm",3'000'000);
        const auto off = read_bounded(output/"point-off.ppm",3'000'000);
        const int width = static_cast<int>(renderer.report().width), height = static_cast<int>(renderer.report().height);
        const auto [x,y] = screen(view,{0.95f,0,0},width,height);
        const auto [ax,ay] = screen(view,{-1.45f,0,0.55f},width,height);
        const auto change = region(off,width,x,y)-region(on,width,x,y);
        const auto open_change = region(off,width,ax,ay)-region(on,width,ax,ay);
        std::cout << "occluded_region_delta=" << change << " open_region_delta=" << open_change
                  << " validation_errors=" << renderer.validation_errors() << '\n';
        if (change<200 || std::abs(open_change)>35 || renderer.validation_errors()!=0) {
            throw std::runtime_error("Point shadow did not darken the expected region");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
