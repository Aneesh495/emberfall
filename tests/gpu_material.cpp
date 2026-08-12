#include "emberfall/renderer.hpp"

#include <SDL3/SDL.h>
#include <iostream>

using namespace ember;

namespace {

MeshTexture solid(std::array<unsigned char,4> pixel) {
    MeshTexture result;
    result.rgba.reserve(MeshTexture::bytes);
    for (std::size_t i=0; i<MeshTexture::bytes/4; ++i) {
        result.rgba.append(reinterpret_cast<const char*>(pixel.data()),pixel.size());
    }
    return result;
}

std::array<int,3> patch(const std::filesystem::path& file) {
    constexpr std::string_view header{"P6\n512 320\n255\n"};
    const auto bytes = read_bounded(file,600'000);
    if (!bytes.starts_with(header) || bytes.size()!=header.size()+512*320*3) {
        throw std::runtime_error("Material fixture framebuffer extent");
    }
    std::array<int,3> sum{};
    for (std::size_t y=158; y<=162; ++y) {
        for (std::size_t x=254; x<=258; ++x) {
            for (std::size_t c=0; c<3; ++c) sum[c] += static_cast<unsigned char>(bytes[header.size()+(y*512+x)*3+c]);
        }
    }
    return sum;
}

MeshVertex point(float x,float z) {
    return {{x,0,z},{0,1,0},{(x+2)*0.25f,(z+2)*0.25f},{1,0,0,-1}};
}

}

int main(int argc,char** argv) {
    try {
        if (argc!=3 || !SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error("Usage: gpu_material SHADERS CAPTURE_DIRECTORY");
        const auto shutdown = std::unique_ptr<void,void(*)(void*)>{reinterpret_cast<void*>(1),[](void*) { SDL_Quit(); }};
        const auto window = std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>{
            SDL_CreateWindow("Emberfall material fixture",512,320,SDL_WINDOW_VULKAN),SDL_DestroyWindow};
        if (!window) throw std::runtime_error(SDL_GetError());
        Renderer renderer(window.get(),argv[1],true);
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        const View view{{0,4,1.5f},{},0.8f,1,false};
        MeshAsset asset;
        asset.vertices = 6;
        asset.primitives.push_back({{}, {point(-2,-2),point(-2,2),point(2,2),point(-2,-2),point(2,2),point(2,-2)}});
        auto& material = asset.primitives.front().material;
        material.roughness = 0.68f;
        material.base_color = {0.2158605f,0.05126946f,0.5271151f};
        const MeshInstance model{&asset,Mat4::identity()};
        const auto draw = [&](unsigned uploads, const char* name = nullptr) {
            if (name) renderer.capture_next(output/name);
            for (unsigned retries=0; !renderer.draw({},view,{}, {},std::span(&model,1)); ++retries) {
                if (retries==30) throw std::runtime_error("Material fixture window unavailable");
                SDL_PumpEvents();
                SDL_Delay(5);
            }
            if (renderer.report().uploaded_texture_layers!=uploads ||
                renderer.report().texture_layers!=asset.textures.size()) throw std::runtime_error("Unexpected texture residency");
        };
        draw(0,"factor.ppm");
        material.base_color = {1,1,1};
        material.textures = {1,0,0};
        asset.textures = {solid({128,64,192,255})};
        draw(1,"srgb.ppm");
        draw(1);
        draw(0);
        material.textures[2] = 2;
        asset.textures.push_back(solid({250,128,165,255}));
        draw(1,"normal.ppm");
        draw(1);
        draw(0);
        asset.textures[1] = solid({128,128,255,255});
        draw(1,"reloaded.ppm");
        draw(1);
        draw(0);

        const auto factor = patch(output/"factor.ppm");
        const auto color = patch(output/"srgb.ppm");
        const auto normal = patch(output/"normal.ppm");
        const auto reloaded = patch(output/"reloaded.ppm");
        int color_error = 0, normal_delta = 0, reload_delta = 0;
        for (std::size_t c=0; c<3; ++c) {
            color_error = std::max(color_error,std::abs(factor[c]-color[c]));
            normal_delta += color[c]-normal[c];
            reload_delta += reloaded[c]-normal[c];
        }
        std::cout << "srgb_region_error=" << color_error << " normal_region_delta=" << normal_delta
                  << " reload_region_delta=" << reload_delta << " validation_errors=" << renderer.validation_errors() << '\n';
        if (color_error>50 || normal_delta<500 || reload_delta<500 || renderer.validation_errors()!=0) {
            throw std::runtime_error("Material readback outside expected bounds");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
