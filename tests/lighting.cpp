#include "emberfall/lighting.hpp"

#include <bit>
#include <iostream>
#include <random>
#include <stdexcept>

int main() {
    using namespace ember;
    constexpr std::uint32_t width = 1377, height = 819;
    const Vec3 eye{3,6,11};
    const auto camera = perspective(0.94f,static_cast<float>(width)/height,0.1f,130)*look_at(eye,{0,0,0},{0,1,0});
    std::mt19937 random(92871);
    std::uniform_real_distribution<float> coordinate(-35,35), radius(0.1f,7), offset(-1,1);
    std::array<LightSphere,128> lights;
    lights[0] = {eye,2};
    for (std::size_t i=1; i<lights.size(); ++i) lights[i] = {{coordinate(random),coordinate(random),coordinate(random)},radius(random)};
    TileLights grid;
    grid.build(camera,width,height,lights);
    if (grid.columns()!=44 || grid.rows()!=26) throw std::runtime_error("Partial tile coverage mismatch");
    unsigned counted = 0, visible_samples = 0;
    for (const auto& mask : grid.masks()) {
        if (!(mask[0]&1U)) throw std::runtime_error("Eye-intersecting light missing conservative coverage");
        for (auto word : mask) counted += static_cast<unsigned>(std::popcount(word));
    }
    if (counted!=grid.entries() || counted>=grid.masks().size()*lights.size()/2) {
        throw std::runtime_error("Light masks failed accounting or failed to reject offscreen lights");
    }
    for (std::size_t i=0; i<lights.size(); ++i) {
        for (unsigned trial=0; trial<3500; ++trial) {
            const Vec3 unit_offset{offset(random),offset(random),offset(random)};
            if (dot(unit_offset,unit_offset)>1) continue;
            const auto world = lights[i].center+unit_offset*lights[i].radius;
            const auto q = transform_point(camera,world);
            const float w = camera(3,0)*world.x+camera(3,1)*world.y+camera(3,2)*world.z+camera(3,3);
            if (w<=0 || q.z<0 || q.z>=w || q.x<=-w || q.x>=w || q.y<=-w || q.y>=w) continue;
            const auto x = static_cast<std::uint32_t>((q.x/w*0.5f+0.5f)*width)/TileLights::tile_size;
            const auto y = static_cast<std::uint32_t>((q.y/w*0.5f+0.5f)*height)/TileLights::tile_size;
            const auto mask = grid.masks()[static_cast<std::size_t>(y)*grid.columns()+x][i/32];
            if (!(mask&(1U<<(i%32)))) throw std::runtime_error("Visible sphere sample missed by tile mask");
            ++visible_samples;
        }
    }
    if (visible_samples<20'000) throw std::runtime_error("Insufficient visible oracle samples");
    grid.build(camera,33,31,{});
    if (grid.columns()!=2 || grid.rows()!=1 || grid.entries()!=0 || grid.masks()[0]!=TileLights::Mask{}) {
        throw std::runtime_error("Zero-light resize retained stale masks");
    }
    std::cout << "visible_samples=" << visible_samples << " tile_entries=" << counted << '\n';
}
