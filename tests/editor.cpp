#include "emberfall/editor.hpp"

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
}
