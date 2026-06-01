#include "emberfall/dynamics.hpp"

#include <iostream>
#include <stdexcept>

using namespace ember;

void check(bool result, const char* reason) { if (!result) throw std::runtime_error(reason); }

int main() {
    const RigidBody sphere{.id=1,.shape=Shape::sphere,.position={0,0,0},.velocity={},.radius=1};
    const RigidBody capsule{.id=2,.shape=Shape::capsule,.position={1.2f,1,0},.velocity={},.radius=0.5f,.half_segment=1};
    const auto contact = intersect(sphere,capsule);
    check(contact && std::abs(contact->penetration-0.3f)<1e-5f && contact->normal == Vec3{1,0,0},"Sphere-capsule analytical mismatch");

    Dynamics impact({});
    auto first = sphere;
    auto second = sphere;
    first.radius = second.radius = 0.5f;
    first.position = {-0.51f,0,0};
    second.position = {0.51f,0,0};
    first.velocity = {1,0,0};
    second.velocity = {-1,0,0};
    first.restitution = second.restitution = 1;
    second.id = 2;
    impact.add(first);
    impact.add(second);
    impact.step(1.0f/60);
    check(impact.body(1).velocity.x == -1 && impact.body(2).velocity.x == 1,"Elastic impulse or momentum mismatch");

    Dynamics stack;
    stack.add({.id=1,.position={0,-0.5f,0},.velocity={},.half={8,0.5f,8},.inverse_mass=0});
    for (SceneId id=2; id<=6; ++id) stack.add({.id=id,.position={0,0.52f+static_cast<float>(id-2)*1.01f,0},.velocity={}});
    for (unsigned i=0; i<450; ++i) stack.step(1.0f/60);
    check(std::abs(stack.body(6).position.y-4.5f)<0.12f,"Five-box stack drift");
    check(length(stack.body(6).velocity)<0.15f,"Five-box stack failed to settle");

    Dynamics sweep({});
    sweep.add({.id=1,.position={0,0,0},.velocity={},.half={0.03f,5,5},.inverse_mass=0});
    sweep.add({.id=2,.shape=Shape::sphere,.position={-3,0,0},.velocity={600,0,0},.radius=0.15f,.continuous=true});
    const auto report = sweep.step(1.0f/60);
    check(report.swept==1 && sweep.body(2).position.x<0 && sweep.body(2).velocity.x==0,"Continuous sweep tunneled");
    std::cout << "elastic_pair=pass stack_height=" << stack.body(6).position.y << " swept=" << report.swept << '\n';
}
