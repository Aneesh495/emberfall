#pragma once

#include "emberfall/collision.hpp"

namespace ember {

struct Route {
    std::vector<Vec3> waypoints;
    std::uint32_t visited{};
};

// Bounded, current-geometry ground navigation for a single near-horizontal floor.
Route find_route(const CollisionSpace& space,Vec3 start,Vec3 goal);

}
