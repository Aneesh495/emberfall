#include "emberfall/editor.hpp"
#include "emberfall/prefab.hpp"

#include <random>
#include <stdexcept>
#include <unistd.h>

int main(int argc,char** argv) {
    using namespace ember;
    if (argc != 2) throw std::runtime_error("Pass fixture path");
    const auto original = load_scene(argv[1]);
    const auto bytes = write_scene(original);
    SceneDocument doc(original);
    std::mt19937 random(88912);
    constexpr std::array<SceneId,3> candidates{20,21,22};
    for (unsigned i=0; i<1000; ++i) {
        const std::array ids{candidates[random()%candidates.size()]};
        const auto dx = static_cast<float>(random()%21+1)*0.005f;
        doc.apply(shift_nodes(doc.scene(),ids,{dx,0,0}));
    }
    const auto revised = write_scene(doc.scene());
    if (!doc.dirty() || doc.cursor() != 1000) throw std::runtime_error("Incomplete edit exercise");
    while (doc.undo()) {}
    if (write_scene(doc.scene()) != bytes) throw std::runtime_error("Undo did not restore canonical scene");
    while (doc.redo()) {}
    if (write_scene(doc.scene()) != revised) throw std::runtime_error("Redo did not restore canonical scene");
    auto play = doc.play();
    for (int i=0; i<120; ++i) play->tick({Command{{0,0,-1}},{}});
    if (write_scene(doc.scene()) != revised) throw std::runtime_error("Play mutated edit world");
    bool rejected = false;
    try { doc.apply({{doc.scene().nodes.at(10),{}}}); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected || write_scene(doc.scene()) != revised) throw std::runtime_error("Invalid edit published");
    if (!pick_node(original,{0,8,8},{0,-1,0})) throw std::runtime_error("Viewport picking failed");

    const auto temporary = std::filesystem::temp_directory_path()/("ember-editor-" + std::to_string(getpid()) + ".scene");
    write_atomic(temporary,bytes);
    {
        EditorFile editor(temporary);
        editor.document().apply(shift_nodes(original,std::span(candidates).first(1),{1,0,0}));
        editor.write_recovery();
    }
    {
        EditorFile editor(temporary);
        if (!editor.recovered() || !editor.document().dirty() ||
            editor.document().scene().nodes.at(20).position.x != original.nodes.at(20).position.x+1) {
            throw std::runtime_error("Recovery did not restore dirty edit");
        }
        editor.save();
        if (editor.document().dirty()) throw std::runtime_error("Saved editor still dirty");
    }
    std::filesystem::remove(temporary);

    const auto folder = std::filesystem::temp_directory_path()/("ember-prefab-"+std::to_string(getpid()));
    std::filesystem::create_directory(folder);
    struct RemoveFixture {
        std::filesystem::path path;
        ~RemoveFixture() { std::error_code error; std::filesystem::remove_all(path,error); }
    } remove{folder};
    Scene base{"Prefab test","Exercise nested overrides",{}};
    base.nodes.emplace(1,Node{.id=1,.kind=Kind::decor,.name="Base"});
    base.nodes.emplace(2,Node{.id=2,.parent=1,.kind=Kind::decor,.name="Child"});
    write_atomic(folder/"base.prefab",write_scene(base));
    const auto middle = folder/"middle.prefab";
    const auto leaf = folder/"leaf.prefab";
    derive_prefab(middle,"base.prefab");
    change_prefab(middle,2,"parameter","3.25");
    change_prefab(middle,1,"color","0.1 0.2 0.3");
    derive_prefab(leaf,"middle.prefab");
    change_prefab(leaf,1,"color","0.7 0.8 0.9");
    const auto resolved = load_prefab(leaf);
    if (resolved.sources != std::vector<std::string>{"leaf.prefab","middle.prefab","base.prefab"} ||
        resolved.scene.nodes.at(2).parameter != 3.25f || resolved.scene.nodes.at(1).color != Vec3{0.7f,0.8f,0.9f}) {
        throw std::runtime_error("Prefab inheritance lost a field");
    }
    change_prefab(leaf,1,"color",{});
    if (load_prefab(leaf).scene.nodes.at(1).color != Vec3{0.1f,0.2f,0.3f}) {
        throw std::runtime_error("Cleared override did not recover inherited field");
    }
    const auto good = read_bounded(leaf,4096);
    rejected = false;
    try { change_prefab(leaf,2,"size","0 0 0"); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected || read_bounded(leaf,4096)!=good) throw std::runtime_error("Invalid override published");
    SceneDocument workshop(original);
    const auto root = workshop.allocate_id();
    workshop.apply(instantiate_prefab(resolved.scene,root,{3,0,-1}));
    if (workshop.scene().nodes.at(root+1).parent!=root || !workshop.undo() ||
        write_scene(workshop.scene())!=bytes || !workshop.redo() ||
        workshop.scene().nodes.at(root).position!=Vec3{3,0,-1}) throw std::runtime_error("Prefab placement transaction failed");
}
