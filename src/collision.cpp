#include "emberfall/collision.hpp"

namespace ember {
namespace {

constexpr float skin = 0.001f;
constexpr float step_height = 0.24f;
constexpr float radius = 0.28f;

std::array<float,3> axes(Vec3 v) { return {v.x,v.y,v.z}; }

Vec3 slide(Vec3 center, Vec3 half, Vec3 movement, const CollisionSpace& space) {
    for (unsigned iteration = 0; iteration < 4 && dot(movement,movement) > 1e-10f; ++iteration) {
        const auto hit = space.sweep(center, half, movement);
        if (!hit.surface) return center + movement;
        center += movement*hit.fraction + hit.normal*skin;
        movement = movement*(1-hit.fraction);
        movement = movement - hit.normal*std::min(0.0f, dot(hit.normal,movement));
    }
    return center;
}

}

Box transformed_box(const Mat4& transform) {
    const Vec3 center = transform_point(transform, {});
    std::array<float,3> half{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) half[static_cast<std::size_t>(r)] += std::abs(transform(r,c))*0.5f;
    }
    const Vec3 extent{half[0],half[1],half[2]};
    return {center-extent,center+extent};
}

bool overlaps(Box a, Box b) {
    return a.minimum.x < b.maximum.x && a.maximum.x > b.minimum.x &&
           a.minimum.y < b.maximum.y && a.maximum.y > b.minimum.y &&
           a.minimum.z < b.maximum.z && a.maximum.z > b.minimum.z;
}

std::optional<Hit> sweep_point(Vec3 start, Vec3 movement, Box target) {
    const auto s = axes(start);
    const auto d = axes(movement);
    const auto low = axes(target.minimum);
    const auto high = axes(target.maximum);
    constexpr std::array<Vec3,3> basis{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
    float enter = 0;
    float leave = 1;
    Vec3 normal;
    for (std::size_t i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-8f) {
            if (s[i] <= low[i] || s[i] >= high[i]) return std::nullopt;
            continue;
        }
        const bool positive = d[i] > 0;
        const float near = ((positive ? low[i] : high[i])-s[i])/d[i];
        const float far = ((positive ? high[i] : low[i])-s[i])/d[i];
        if (near >= enter) {
            enter = near;
            normal = basis[i]*(positive ? -1.0f : 1.0f);
        }
        leave = std::min(leave, far);
        if (enter > leave) return std::nullopt;
    }
    if (normal == Vec3{} || leave <= 0) return std::nullopt;
    return Hit{enter,normal,0};
}

void CollisionSpace::rebuild(std::span<const Collider> colliders) {
    ordered_.assign(colliders.begin(),colliders.end());
    std::ranges::sort(ordered_, [](const auto& a, const auto& b) {
        if (a.bounds.minimum.x == b.bounds.minimum.x) return a.id < b.id;
        return a.bounds.minimum.x < b.bounds.minimum.x;
    });
}

Hit CollisionSpace::sweep(Vec3 center, Vec3 half, Vec3 movement, SceneId ignore) const {
    Hit best;
    const auto end = center + movement;
    const auto low = std::min(center.x,end.x)-half.x;
    const auto high = std::max(center.x,end.x)+half.x;
    for (const auto& collider : ordered_) {
        if (collider.bounds.minimum.x > high) break;
        if (collider.id == ignore || collider.bounds.maximum.x < low) continue;
        const auto hit = sweep_point(center,movement,collider.bounds.expanded(half));
        if (!hit || hit->fraction > best.fraction) continue;
        if (!best.surface || hit->fraction < best.fraction || collider.id < best.surface) {
            best = {hit->fraction,hit->normal,collider.id};
        }
    }
    return best;
}

bool CollisionSpace::clear(Vec3 center, Vec3 half, SceneId ignore) const {
    const Box query{center-half,center+half};
    for (const auto& collider : ordered_) {
        if (collider.bounds.minimum.x >= query.maximum.x) break;
        if (collider.id != ignore && overlaps(query,collider.bounds)) return false;
    }
    return true;
}

void move_character(Character& c, const CollisionSpace& space, Vec3 planar, bool jump, bool crouch, float dt) {
    constexpr float standing_height = 1.45f;
    constexpr float crouching_height = 0.90f;
    const Vec3 standing{radius, standing_height*0.5f, radius};
    if (crouch) {
        c.height = crouching_height;
    } else if (space.clear(c.feet + Vec3{0,standing.y,0}, standing)) {
        c.height = standing_height;
    }
    const Vec3 half{radius,c.height*0.5f,radius};
    const Vec3 offset{0,half.y,0};
    Vec3 center = c.feet + offset;
    const Vec3 move{planar.x*dt,0,planar.z*dt};
    Vec3 walked = slide(center, half, move, space);
    const bool grounded = c.ground != 0;
    if (grounded && length(walked-center) + 2*skin < length(move)) {
        const Vec3 lift{0,step_height,0};
        if (!space.sweep(center,half,lift).surface) {
            const auto stepped = slide(center + lift, half, move, space);
            const auto landing = space.sweep(stepped,half,-lift-Vec3{0,0.02f,0});
            if (landing.surface && landing.normal.y > 0 && length(stepped-center-lift) > length(walked-center)) {
                walked = stepped + (-lift-Vec3{0,0.02f,0})*landing.fraction + Vec3{0,skin,0};
            }
        }
    }
    if (grounded && jump) c.velocity.y = 5.0f;
    c.velocity.y = std::max(-24.0f, c.velocity.y - 12.0f*dt);
    const float fall = c.velocity.y*dt;
    const float sample = fall > 0 ? fall : std::min(fall,-0.035f);
    const auto hit = space.sweep(walked,half,{0,sample,0});
    c.ground = 0;
    if (hit.surface) {
        walked.y += sample*hit.fraction + hit.normal.y*skin;
        c.velocity.y = 0;
        if (hit.normal.y > 0) c.ground = hit.surface;
    } else {
        walked.y += fall;
    }
    c.velocity.x = planar.x;
    c.velocity.z = planar.z;
    c.feet = walked-offset;
}

}
