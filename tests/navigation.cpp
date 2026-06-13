#include "emberfall/navigation.hpp"

#include <stdexcept>

int main() {
    using namespace ember;
    CollisionSpace world;
    constexpr Collider floor{1,{{-6,-1,-6},{6,0,6}}};
    constexpr Collider wall{2,{{-6,0,-0.25f},{6,3,0.25f}}};
    constexpr Collider obstruction{3,{{-1,0,-0.25f},{1,2,0.25f}}};
    world.rebuild(std::array{floor,wall});
    if (!find_route(world,{0,0.002f,2},{0,0.002f,-2}).waypoints.empty()) {
        throw std::runtime_error("Closed wall did not invalidate path");
    }
    world.rebuild(std::array{floor,obstruction});
    const auto route = find_route(world,{0,0.002f,2},{0,0.002f,-2});
    if (route.waypoints.empty() || route.visited < 9 || std::abs(route.waypoints.back().z+2)>0.001f ||
        !std::ranges::any_of(route.waypoints,[](Vec3 point) { return std::abs(point.x)>1.25f; })) {
        throw std::runtime_error("Route did not reach goal around solid obstruction");
    }
    world.rebuild(std::array{floor});
    const auto direct = find_route(world,{0,0.002f,2},{0,0.002f,-2});
    if (direct.waypoints.size()!=8) throw std::runtime_error("A* missed the straight shortest route");
}
