#include "emberfall/collision.hpp"

#include <stdexcept>

int main() {
    using namespace ember;
    const auto check = [](bool ok) {
        if (!ok) throw std::runtime_error("Collision oracle mismatch");
    };
    const Box target{{2,-1,-1},{3,1,1}};
    const auto hit = sweep_point({}, {8,0,0}, target);
    check(hit && hit->fraction == 0.25f && hit->normal == Vec3{-1,0,0});
    check(!sweep_point({0,2,0},{8,0,0},target));
    check(!sweep_point({2,0,0},{-8,0,0},target));
    check(sweep_point({2,0,0},{8,0,0},target).value().fraction == 0);

    CollisionSpace space;
    const std::array shapes{
        Collider{1, {{-5,-1,-5},{5,0,5}}},
        Collider{2, {{1,0,-1},{2,0.18f,1}}},
        Collider{3, {{2,0,-1},{3,2,1}}}
    };
    space.rebuild(shapes);
    Character actor{};
    for (int i = 0; i < 36; ++i) move_character(actor, space, {3,0,0}, false, false, 1.0f/60);
    check(actor.ground == 2 && std::abs(actor.feet.y - 0.181f) < 0.002f);
    for (int i = 0; i < 24; ++i) move_character(actor, space, {3,0,0}, false, false, 1.0f/60);
    check(actor.feet.x < 1.721f && actor.ground == 2);
    move_character(actor, space, {}, true, false, 1.0f/60);
    check(!actor.ground && actor.feet.y > 0.23f);
}
