#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv) {
    using namespace ember;
    try {
        if (argc != 2) throw std::runtime_error("Pass the shader directory");
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        const auto shutdown = std::unique_ptr<void,void(*)(void*)>{reinterpret_cast<void*>(1),[](void*) { SDL_Quit(); }};
        const auto window = std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>{
            SDL_CreateWindow("Emberfall render diagnostic",1920,1080,SDL_WINDOW_VULKAN),SDL_DestroyWindow};
        if (!window) throw std::runtime_error(SDL_GetError());
        Renderer renderer(window.get(),argv[1],true);
        std::vector<RenderItem> items;
        constexpr std::uint32_t rows = 100, columns = 105;
        for (std::uint32_t r=0; r<rows; ++r) for (std::uint32_t c=0; c<columns; ++c) {
            const auto x = static_cast<float>(c)-52;
            const auto z = static_cast<float>(r)-50;
            const auto height = 0.45f+static_cast<float>((r*29+c*19)%27)*0.055f;
            const bool lamp = (r*columns+c)%82 == 0 && r*columns+c < 82*128;
            const Vec3 tint = lamp ? Vec3{0.33f,0.64f,0.94f} : Vec3{0.15f+height*0.2f,0.26f,0.38f};
            items.push_back({transform_matrix({x,height*0.5f,z},yaw_rotation(height*0.2f),{0.78f,height,0.78f}),
                             tint,lamp ? 1.1f : 0});
        }
        const View view{{0,128,60},{0,0,0},0.92f,1,true,true,260};
        struct Observation { FrameReport report; std::int64_t host_ns{}; };
        std::array<Observation,160> observations{};
        std::size_t drawn = 0;
        std::cerr << renderer.adapter() << '\n';
        std::cout << "serial,width,height,submitted,culled,triangles,casters,lights,draw_calls,host_ns,"
                     "timestamp_bits,period_ns,start,shadow,opaque,display,reported_gpu_ms,light_tiles,tile_light_entries\n";
        std::cout << std::setprecision(12);
        for (unsigned iteration=0; iteration<400 && drawn<observations.size(); ++iteration) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.resize();
            }
            const auto begin = std::chrono::steady_clock::now();
            if (!renderer.draw(items,view)) continue;
            const auto end = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();
            const auto& report = renderer.report();
            observations[drawn++] = {report,elapsed};
            if (report.timed_serial <= 18) continue;
            const auto& [past,host_ns] = observations.at(report.timed_serial-1);
            std::cout << past.serial << ',' << past.width << ',' << past.height << ',' << past.submitted << ','
                      << past.culled << ',' << past.triangles << ',' << past.shadow_casters << ',' << past.lights << ','
                      << past.draw_calls << ',' << host_ns << ',' << report.timestamp_valid_bits << ','
                      << report.timestamp_period_ns;
            for (auto stamp : report.timestamps) std::cout << ',' << stamp;
            std::cout << ',' << report.gpu_ms << ',' << past.light_tiles << ',' << past.tile_light_entries << '\n';
        }
        const auto& result = renderer.report();
        std::cerr << "frames=" << drawn << " validation_errors=" << renderer.validation_errors()
                  << " size=" << result.width << 'x' << result.height << " triangles=" << result.triangles << '\n';
        return drawn == observations.size() && result.width == 1920 && result.height == 1080 &&
               result.submitted >= 10000 && result.triangles >= 2000000 && result.lights == 128 &&
               result.timestamp_valid_bits > 0 && renderer.validation_errors() == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
