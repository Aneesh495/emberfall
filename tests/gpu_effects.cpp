#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>
#include <iostream>

using namespace ember;

namespace {
std::size_t difference(std::string_view a,std::string_view b) {
    constexpr std::string_view header{"P6\n640 420\n255\n"};
    if (!a.starts_with(header) || !b.starts_with(header) || a.size()!=header.size()+640*420*3 || b.size()!=a.size()) {
        throw std::runtime_error("Effects framebuffer extent");
    }
    std::size_t count = 0;
    for (auto i=header.size(); i<a.size(); i+=3) count += a.substr(i,3)!=b.substr(i,3);
    return count;
}
}

int main(int argc,char** argv) {
    try {
        if (argc!=3 || !SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error("Usage: gpu_effects SHADERS CAPTURE_DIRECTORY");
        const auto shutdown = std::unique_ptr<void,void(*)(void*)>{reinterpret_cast<void*>(1),[](void*) { SDL_Quit(); }};
        const auto window = std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>{
            SDL_CreateWindow("Emberfall effect fixture",640,420,SDL_WINDOW_VULKAN),SDL_DestroyWindow};
        if (!window) throw std::runtime_error(SDL_GetError());
        Renderer renderer(window.get(),argv[1],true);
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        const std::array blocks{
            RenderItem{transform_matrix({0,-0.1f,0},{},{12,0.2f,12}),{0.27f,0.31f,0.36f},0},
            RenderItem{transform_matrix({2.1f,1,0},{},{2.1f,3.1f,2.1f}),{0.2f,0.30f,0.43f},0}
        };
        View view{{0,5,7},{0,0.65f,0},0.8f,1,false};
        view.local_shadows = false;
        const Effect burst{{-1.4f,0.6f,0},0.24f,{2.2f,0.72f,0.14f},0.10f,137,EffectKind::burst};
        const Effect ring{{-1.4f,0.02f,0},0.35f,{0.1f,1.25f,1.4f},0.92f,19,EffectKind::ground_ring};
        auto hidden = burst;
        hidden.position = {2.1f,0.8f,0};
        const auto frame = [&](std::span<const Effect> effects,const char* file,std::uint32_t particles,std::uint32_t decals) {
            renderer.capture_next(output/file);
            for (unsigned retries=0; !renderer.draw(blocks,view,{}, {},{},effects); ++retries) {
                if (retries==30) throw std::runtime_error("Effects fixture window unavailable");
                SDL_PumpEvents();
                SDL_Delay(5);
            }
            if (renderer.report().particles!=particles || renderer.report().decals!=decals) {
                throw std::runtime_error("Effects counters did not match submission");
            }
            return read_bounded(output/file,900'000);
        };
        const auto base = frame({},"reference.ppm",0,0);
        const auto spark = difference(base,frame(std::span(&burst,1),"burst.ppm",24,0));
        const auto decal = difference(base,frame(std::span(&ring,1),"ring.ppm",0,1));
        const auto occluded = difference(base,frame(std::span(&hidden,1),"occluded.ppm",24,0));
        std::cout << "burst_changed_pixels=" << spark << " ring_changed_pixels=" << decal
                  << " hidden_changed_pixels=" << occluded << " validation_errors=" << renderer.validation_errors() << '\n';
        if (spark<100 || decal<100 || occluded || renderer.validation_errors()) {
            throw std::runtime_error("Effects framebuffer outside expected bounds");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
