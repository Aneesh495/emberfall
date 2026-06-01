#pragma once

#include "emberfall/collision.hpp"

namespace ember {

enum class Shape : std::uint8_t { box, sphere, capsule };

struct RigidBody {
    SceneId id{};
    Shape shape{};
    Vec3 position, velocity;
    Vec3 half{0.5f,0.5f,0.5f};
    float radius{0.5f}, half_segment{};
    float inverse_mass{1}, restitution{}, friction{0.6f};
    std::uint16_t layer{1}, mask{0xffff};
    bool kinematic{}, trigger{}, continuous{}, sleeping{};
    unsigned calm_ticks{};
};

struct Contact {
    SceneId first{}, second{};
    Vec3 normal;
    float penetration{};
    bool trigger{};
};

std::optional<Contact> intersect(const RigidBody& first,const RigidBody& second);

struct PhysicsReport {
    std::size_t bodies{}, candidates{}, contacts{}, sleeping{}, swept{};
};

class Dynamics {
public:
    explicit Dynamics(Vec3 gravity = {0,-9.81f,0});
    ~Dynamics();
    Dynamics(const Dynamics&) = delete;
    Dynamics& operator=(const Dynamics&) = delete;

    void add(RigidBody body);
    const RigidBody& body(SceneId id) const;
    void impulse(SceneId id,Vec3 impulse);
    PhysicsReport step(float dt);
    std::span<const Contact> contacts() const { return contacts_; }
private:
    struct Constraint;
    std::size_t index(SceneId id) const;

    Vec3 gravity_;
    std::vector<RigidBody> bodies_;
    std::vector<Contact> contacts_;
    std::vector<Constraint> constraints_, previous_;
    std::vector<std::size_t> order_;
    std::vector<Box> boxes_;
    std::vector<std::uint8_t> supported_;
    std::vector<Collider> obstacles_;
    CollisionSpace static_space_;
};

}
