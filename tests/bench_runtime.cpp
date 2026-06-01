#include "emberfall/dynamics.hpp"
#include "emberfall/scheduler.hpp"
#include "emberfall/world.hpp"

#include <bit>
#include <chrono>
#include <iostream>

using namespace ember;
using Clock = std::chrono::steady_clock;

namespace {

struct Position { Vec3 value; };
struct Velocity { Vec3 value; };
struct Force { Vec3 acceleration; };
struct Lifetime { std::uint32_t ticks{}; };

void ecs(unsigned workers) {
    World<Position,Velocity,Force,Lifetime> world;
    Scheduler scheduler(workers);
    constexpr std::size_t entities = 100'000;
    for (unsigned i=0; i<entities; ++i) {
        world.spawn(Position{{static_cast<float>(i%100)*0.5f,2,static_cast<float>(i/100)*0.25f}},
                    Velocity{{0,0,0.2f}},Force{{static_cast<float>(i%11)*0.05f,-0.2f,0}},Lifetime{});
    }
    unsigned peak = 0;
    world.chunks<Position,Velocity,Force,Lifetime>([&](auto ids,auto positions,auto velocities,auto forces,auto lives) {
        std::vector<Task> tasks;
        for (unsigned part=0; part<16; ++part) {
            const auto begin = ids.size()*part/16;
            const auto end = ids.size()*(part+1)/16;
            tasks.push_back({{},{{static_cast<ResourceId>(part),true}},[=](std::stop_token stop) {
                for (std::size_t i=begin; i<end && !stop.stop_requested(); ++i) {
                    auto& speed = velocities[i].value;
                    speed += (forces[i].acceleration-speed*0.14f)*(1.0f/60);
                    positions[i].value += speed*(1.0f/60);
                    ++lives[i].ticks;
                }
            }});
        }
        const Schedule schedule(std::move(tasks));
        for (unsigned tick=0; tick<240; ++tick) {
            const auto begin = Clock::now();
            const auto run = scheduler.run(schedule);
            const auto end = Clock::now();
            if (run.completed != 16 || run.cancelled) throw std::runtime_error("Incomplete ECS batch");
            peak = std::max(peak,run.peak_workers);
            if (tick<40) continue;
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();
            std::cout << workers << ',' << tick << ',' << entities << ',' << entities*4 << ',' << entities*3
                      << ',' << run.peak_workers << ',' << ns << '\n';
        }
    });
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&](std::uint32_t value) { hash = (hash^value)*1099511628211ULL; };
    world.each<Position,Velocity,Lifetime>([&](Entity,const Position& p,const Velocity& v,const Lifetime& life) {
        if (!finite(p.value) || !finite(v.value) || life.ticks != 240) throw std::runtime_error("Invalid ECS endpoint");
        for (float f : {p.value.x,p.value.y,p.value.z,v.value.x,v.value.y,v.value.z}) add(std::bit_cast<std::uint32_t>(f));
        add(life.ticks);
    });
    std::cerr << "workers=" << workers << " peak_workers=" << peak << " state=" << hash << '\n';
}

void physics() {
    Dynamics world;
    world.add({.id=1,.position={0,-0.5f,0},.velocity={},.half={24,0.5f,24},.inverse_mass=0});
    for (SceneId index=0; index<1000; ++index) {
        const auto x = static_cast<float>((index%10)*2)-9;
        const auto z = static_cast<float>(((index/10)%10)*2)-9;
        const auto y = static_cast<float>(index/100)*0.95f+0.6f;
        const auto type = index%5 == 0 ? Shape::sphere : Shape::box;
        world.add({.id=index+2,.shape=type,.position={x,y,z},.velocity={},.half={0.42f,0.42f,0.42f},.radius=0.42f});
    }
    std::cout << "tick,bodies,candidates,contacts,sleeping,swept,step_ns\n";
    for (unsigned i=0; i<600; ++i) {
        if (i%60 == 0) {
            for (SceneId id=905; id<=1001; id+=8) world.impulse(id,{0,0.64f,0});
        }
        const auto begin = Clock::now();
        const auto report = world.step(1.0f/60);
        const auto end = Clock::now();
        if (i<60) continue;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count();
        std::cout << i << ',' << report.bodies << ',' << report.candidates << ',' << report.contacts << ','
                  << report.sleeping << ',' << report.swept << ',' << ns << '\n';
    }
    for (SceneId id=2; id<=1001; ++id) {
        if (world.body(id).position.y < -0.5f) throw std::runtime_error("Stress body fell below the supporting floor");
    }
}

}

int main(int argc,char** argv) {
    if (argc != 2) throw std::runtime_error("Pass ecs or physics");
    const std::string_view mode = argv[1];
    if (mode == "ecs") {
        std::cout << "workers,tick,entities,component_reads,component_writes,peak_workers,step_ns\n";
        ecs(1);
        ecs(4);
    } else if (mode == "physics") physics();
    else throw std::runtime_error("Unknown benchmark");
}
