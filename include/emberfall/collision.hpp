#pragma once

#include "emberfall/math.hpp"
#include "emberfall/scene.hpp"

#include <span>
#include <vector>

namespace ember {

struct Box {
    Vec3 minimum, maximum;
    Box expanded(Vec3 half) const { return {minimum-half,maximum+half}; }
};

struct Collider {
    SceneId id{};
    Box bounds;
};

struct Hit {
    float fraction{1};
    Vec3 normal;
    SceneId surface{};
};

Box transformed_box(const Mat4& transform);
bool overlaps(Box a, Box b);
std::optional<Hit> sweep_point(Vec3 start, Vec3 movement, Box target);

class CollisionSpace {
public:
    void rebuild(std::span<const Collider> colliders);
    Hit sweep(Vec3 center, Vec3 half, Vec3 movement, SceneId ignore = {}) const;
    bool clear(Vec3 center, Vec3 half, SceneId ignore = {}) const;
private:
    std::vector<Collider> ordered_;
};

struct Character {
    Vec3 feet, velocity;
    SceneId ground{};
    float height{1.45f};
};

void move_character(Character& character, const CollisionSpace& space,
                    Vec3 planar_velocity, bool jump, bool crouch, float timestep);

}
