#include "emberfall/scene.hpp"

#include <stdexcept>

int main() {
    using namespace ember;
    Scene scene{"Scene", "Follow the beam", {}};
    scene.nodes[1] = Node{.id=1,.name="Anchor",.position={1,2,3},.size={-2,3,4}};
    scene.nodes[2] = Node{.id=2,.parent=1,.name="Arm",.position={2,3,4}};
    const auto canonical = write_scene(scene);
    if (parse_scene(canonical) != scene) throw std::runtime_error("Scene round trip");
    const auto matrices = resolve_transforms(scene);
    if (transform_point(matrices.at(2),{}) != Vec3{-3,11,19}) throw std::runtime_error("Parent transform");

    const auto reject = [](auto&& f) {
        try { f(); }
        catch (const std::runtime_error&) { return; }
        throw std::logic_error("Invalid scene accepted");
    };
    scene.nodes.at(1).parent = 2;
    reject([&] { validate_scene(scene); });
    scene.nodes.at(1).parent = 3;
    reject([&] { validate_scene(scene); });
    reject([&] { parse_scene(canonical + "node 1\n"); });
    reject([&] { parse_scene(canonical + "title \"second\"\n"); });
    reject([&] { parse_scene("ember-scene 2\n"); });
}
