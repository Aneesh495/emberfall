#pragma once

#include "emberfall/game.hpp"

#include <memory>

namespace ember {

struct Edit {
    std::optional<Node> before, after;
};

class SceneDocument {
public:
    explicit SceneDocument(Scene scene);
    const Scene& scene() const { return scene_; }
    bool dirty() const { return scene_ != saved_; }
    std::size_t history_size() const { return history_.size(); }
    std::size_t cursor() const { return cursor_; }

    void apply(std::vector<Edit> transaction);
    bool undo();
    bool redo();
    void mark_saved();
    SceneId allocate_id() const;
    std::unique_ptr<Game> play(std::shared_ptr<const MeshLibrary> assets = {}) const;
private:
    friend class EditorFile;
    static Scene make_candidate(const Scene& base,const std::vector<Edit>& edits,bool backwards);
    Scene scene_, saved_, history_base_;
    std::vector<std::vector<Edit>> history_;
    std::size_t cursor_{};
};

class EditorFile {
public:
    explicit EditorFile(std::filesystem::path path);
    SceneDocument& document() { return document_; }
    const SceneDocument& document() const { return document_; }
    bool recovered() const { return recovered_; }
    void write_recovery() const;
    void save();
private:
    static SceneDocument recover(const std::filesystem::path& path, const std::filesystem::path& journal,
                                 std::uint64_t& base, bool& recovered);
    std::filesystem::path path_, journal_;
    std::uint64_t base_{};
    bool recovered_{};
    SceneDocument document_;
};

std::vector<Edit> instantiate_prefab(const Scene& prefab, SceneId first, Vec3 offset);
SceneId pick_node(const Scene& scene, Vec3 origin, Vec3 direction, float reach = 150);
std::vector<Edit> shift_nodes(const Scene& scene, std::span<const SceneId> ids, Vec3 delta);

}
