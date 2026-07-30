#include "emberfall/dynamics.hpp"

#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace ember;

void require(bool condition,const char* label) {
    if (!condition) throw std::runtime_error(label);
}

int main() {
    const auto hull = std::make_shared<const ConvexHull>(
        std::vector<Vec3>{{0,0,0},{2,0,0},{0,2,0},{0,0,2}},
        std::vector<ConvexHull::Face>{{0,1,2},{0,1,3},{0,2,3},{1,2,3}});
    require(hull->edges().size()==6,"Tetrahedron edge count");
    try {
        const ConvexHull open({{0,0,0},{1,0,0},{0,1,0},{0,0,1}},{{0,1,2},{0,1,3},{1,2,3},{1,2,3}});
        (void)open;
        throw std::logic_error("Accepted nonmanifold convex topology");
    } catch (const std::runtime_error&) {}

    const RigidBody solid{.id=1,.shape=Shape::convex,.position={},.velocity={},.inverse_mass=0,.hull=hull};
    RigidBody sphere{.id=2,.shape=Shape::sphere,.position={0.5f,0.5f,-0.25f},.velocity={},.radius=0.5f};
    const auto face = intersect(sphere,solid);
    require(face && length(face->normal-Vec3{0,0,1})<1e-6f,"Sphere to hull normal");
    require(std::abs(face->penetration-0.25f)<1e-6f &&
            length(face->point-Vec3{0.5f,0.5f,0.125f})<1e-6f,"Sphere to hull depth or witness");
    const auto reversed = intersect(solid,sphere);
    require(reversed && reversed->normal==-face->normal && reversed->point==face->point,"Contact symmetry");
    sphere.position = {0.5f,0.5f,-0.7f};
    require(!intersect(sphere,solid),"Separated sphere and hull");

    const RigidBody capsule{.id=3,.shape=Shape::capsule,.position={0.5f,0.45f,-0.2f},.velocity={},
                            .radius=0.3f,.half_segment=0.4f,.orientation={0,0,0.70710678f,0.70710678f}};
    const auto capsule_hit = intersect(capsule,solid);
    require(capsule_hit && std::abs(capsule_hit->penetration-0.1f)<1e-5f,"Rotated capsule to hull");
    RigidBody shifted = solid;
    shifted.id = 4;
    shifted.position = {0,0,-0.9f};
    const auto overlap = intersect(solid,shifted);
    require(overlap && overlap->penetration>0 && finite(overlap->point),"Hull SAT overlap");
    shifted.position.z = -2.1f;
    require(!intersect(solid,shifted),"Hull SAT separation");

    Dynamics physics(Vec3{});
    physics.add({.id=5,.position={},.velocity={},.inverse_mass=0.5f,.angular=true});
    physics.impulse_at(5,{2,0,0},{0,0.5f,0});
    require(physics.body(5).velocity==Vec3{1,0,0},"Linear momentum from off-center impulse");
    require(length(physics.body(5).angular_velocity-Vec3{0,0,-3})<1e-6f,"Box inertia from off-center impulse");
    physics.step(0.02f);
    const auto point = rotate(physics.body(5).orientation,{1,0,0});
    require(length(point-Vec3{std::cos(0.06f),-std::sin(0.06f),0})<1e-6f,"Quaternion angular integration");

    Dynamics precession(Vec3{});
    precession.add({.id=6,.position={},.velocity={},.half={0.4f,0.6f,0.9f},
                    .angular_velocity={0.7f,1.1f,2.0f},.angular=true});
    const auto momentum = [](const RigidBody& body) {
        const auto q = body.orientation;
        const auto local = rotate({-q.x,-q.y,-q.z,q.w},body.angular_velocity);
        // Analytic principal moments of a uniform unit-mass cuboid.
        return rotate(q,{local.x*(0.36f+0.81f)/3,local.y*(0.16f+0.81f)/3,local.z*(0.16f+0.36f)/3});
    };
    const auto initial_momentum = momentum(precession.body(6));
    const auto initial_energy = dot(initial_momentum,precession.body(6).angular_velocity)*0.5f;
    for (unsigned tick=0; tick<360; ++tick) precession.step(1.0f/120);
    const auto& free_body = precession.body(6);
    require(length(momentum(free_body)-initial_momentum)<0.0004f,"Torque-free angular momentum drift");
    require(std::abs(dot(momentum(free_body),free_body.angular_velocity)*0.5f-initial_energy)<0.002f,
            "Torque-free rotational energy drift");

    Dynamics rolling;
    rolling.add({.id=1,.position={0,-0.5f,0},.velocity={},.half={6,0.5f,6},.inverse_mass=0});
    rolling.add({.id=2,.shape=Shape::sphere,.position={0,0.497f,0},.velocity={2,0,0},.angular=true});
    for (unsigned tick=0; tick<35; ++tick) rolling.step(1.0f/60);
    const auto& ball = rolling.body(2);
    require(ball.angular_velocity.z<-0.2f && ball.velocity.x>0 && ball.velocity.x<2 &&
            ball.position.y>0.45f, "Friction did not exchange linear momentum for angular motion");
    std::cout << "hull_contact_depth=" << face->penetration << " rolling_omega=" << ball.angular_velocity.z << '\n';
}
