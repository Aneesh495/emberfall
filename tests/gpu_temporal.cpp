#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>
#include <iostream>

using namespace ember;

namespace {

std::string_view pixels(const std::string& bytes) {
    std::size_t start = 0;
    for (int i=0; i<3; ++i) {
        start = bytes.find('\n',start);
        if (start == std::string::npos) throw std::runtime_error("Malformed capture");
        ++start;
    }
    return std::string_view(bytes).substr(start);
}

std::size_t changed_pixels(const std::string& first,const std::string& second) {
    const auto a = pixels(first);
    const auto b = pixels(second);
    if (a.size()!=b.size() || a.size()%3) throw std::runtime_error("Capture extent mismatch");
    std::size_t changed = 0;
    for (std::size_t i=0; i<a.size(); i+=3) changed += a.substr(i,3)!=b.substr(i,3);
    return changed;
}

} // namespace

int main(int argc,char** argv) {
    try {
        if (argc!=3 || !SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error("Usage: gpu_temporal SHADERS CAPTURE_DIRECTORY");
        const auto shutdown = std::unique_ptr<void,void(*)(void*)>{reinterpret_cast<void*>(1),[](void*) { SDL_Quit(); }};
        const auto window = std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>{
            SDL_CreateWindow("Emberfall history fixture",640,420,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE),SDL_DestroyWindow};
        if (!window) throw std::runtime_error(SDL_GetError());
        Renderer renderer(window.get(),argv[1],true);
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        std::vector items{
            RenderItem{transform_matrix({0,-0.1f,0},{},{12,0.2f,12}),{0.37f,0.54f,0.72f},0},
            RenderItem{transform_matrix({0,1.0f,0},yaw_rotation(0.35f),{0.15f,2.0f,2.2f}),{1,0.26f,0.15f},0.32f}
        };
        items[0].history_id = 1;
        items[1].history_id = 2;
        View view{{0,3,6},{0,0.8f,0}};
        view.temporal = true;
        view.local_shadows = false;
        const auto draw = [&](bool valid, unsigned motion, const char* capture = nullptr) {
            if (capture) renderer.capture_next(output/capture);
            for (unsigned retries=0; !renderer.draw(items,view); ++retries) {
                if (retries==30) throw std::runtime_error("Temporal fixture window unavailable");
                SDL_PumpEvents();
                SDL_Delay(5);
            }
            const auto& report = renderer.report();
            if (report.history_used!=valid || report.motion_items!=motion) {
                throw std::runtime_error("Unexpected history state or missing motion item");
            }
        };
        const auto capture = [&](const char* name) { return read_bounded(output/name,3'000'000); };
        draw(false,0,"cold.ppm");
        for (int i=0; i<7; ++i) draw(true,2);
        draw(true,2,"warm.ppm");
        const auto accumulated = changed_pixels(capture("cold.ppm"),capture("warm.ppm"));
        if (accumulated < 20) throw std::runtime_error("Temporal resolve did not alter edge coverage");

        items[1].model = transform_matrix({0.8f,1.0f,0},yaw_rotation(0.9f),{0.15f,2.0f,2.2f});
        draw(true,2,"motion.ppm");
        items[1].history_id = 3;
        draw(true,1);
        items.pop_back();
        for (int i=0; i<4; ++i) draw(true,1);
        draw(true,1,"revealed.ppm");

        view.eye.x += 4;
        view.target.x += 4;
        draw(false,0,"cut.ppm");
        renderer.reset_history();
        draw(false,0,"reset.ppm");
        const auto cut = changed_pixels(capture("cut.ppm"),capture("reset.ppm"));
        if (cut) throw std::runtime_error("Camera cut differs from explicit reset");
        draw(true,1);

        if (!SDL_SetWindowSize(window.get(),700,470)) throw std::runtime_error(SDL_GetError());
        SDL_SyncWindow(window.get());
        renderer.resize();
        draw(false,0,"resize.ppm");
        if (renderer.report().width!=700 || renderer.report().height!=470) throw std::runtime_error("Resize did not rebuild history extent");
        draw(true,1);
        view.antialias = false;
        draw(false,0);
        view.antialias = true;
        draw(false,0);
        draw(true,1);
        std::cout << "accumulated_pixels=" << accumulated << " cut_reset_mismatches=" << cut
                  << " validation_errors=" << renderer.validation_errors() << '\n';
        return renderer.validation_errors() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
