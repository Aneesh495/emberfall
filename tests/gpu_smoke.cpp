#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("usage: gpu_smoke shader-directory [capture.ppm]");
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        const auto shutdown = std::unique_ptr<void,void(*)(void*)>{
            reinterpret_cast<void*>(1), [](void*) { SDL_Quit(); }
        };
        const auto window = std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>{
            SDL_CreateWindow("Emberfall Vulkan smoke",840,540,SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),SDL_DestroyWindow
        };
        if (!window) throw std::runtime_error(SDL_GetError());
        ember::Renderer renderer(window.get(),argv[1],true);
        const std::array scene{
            ember::RenderItem{ember::transform_matrix({0,-0.3f,0},{},{12,0.6f,12}),{0.24f,0.36f,0.49f},0},
            ember::RenderItem{ember::transform_matrix({0,1,0},ember::yaw_rotation(0.7f),{1.7f,2,1.7f}),{0.94f,0.39f,0.12f},0},
            ember::RenderItem{ember::transform_matrix({-2,0.5f,-1},{},{1,1,1}),{0.10f,0.78f,0.70f},0.8f},
            ember::RenderItem{ember::transform_matrix({-600,0,0}),{0.10f,0.78f,0.70f},0.8f}
        };
        constexpr std::array overlay{
            ember::OverlayVertex{0.02f,0.02f,{1,0.1f,0.1f,1}},
            ember::OverlayVertex{0.15f,0.02f,{1,0.1f,0.1f,1}},
            ember::OverlayVertex{0.02f,0.15f,{1,0.1f,0.1f,1}}
        };
        unsigned rendered = 0;
        std::uint32_t before = 0;
        for (unsigned i=0; i<120; ++i) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.resize();
            }
            if (i == 32) {
                before = renderer.report().width;
                if (!SDL_SetWindowSize(window.get(),620,420)) throw std::runtime_error(SDL_GetError());
                renderer.resize();
            }
            if (i == 119 && argc == 3) renderer.capture_next(argv[2]);
            if (renderer.draw(scene,{{5,5,7},{0,0.6f,0}},overlay)) ++rendered;
        }
        const auto report = renderer.report();
        std::cout << renderer.adapter() << '\n'
                  << "frames=" << rendered << " before=" << before << " final=" << report.width << 'x' << report.height
                  << " submitted=" << report.submitted << " culled=" << report.culled
                  << " gpu_ms=" << report.gpu_ms << " validation_errors=" << renderer.validation_errors() << '\n';
        return rendered >= 100 && before != report.width && report.submitted == 3 &&
               report.culled == 1 && renderer.validation_errors() == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
