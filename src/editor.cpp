#include "emberfall/editor.hpp"

#include "emberfall/campaign.hpp"

#include <limits>
#include <set>

namespace ember {
namespace {

constexpr std::size_t history_limit = 2048;

SceneId identity(const Edit& edit) {
    if (!edit.before && !edit.after) throw std::runtime_error("Empty edit");
    if (edit.before && edit.after && edit.before->id != edit.after->id) {
        throw std::runtime_error("Edit changes stable ID");
    }
    return edit.before ? edit.before->id : edit.after->id;
}

std::vector<Edit> difference(const Scene& base, const Scene& revision) {
    std::vector<Edit> result;
    for (const auto& [id,node] : base.nodes) {
        const auto latest = revision.nodes.find(id);
        if (latest == revision.nodes.end()) result.push_back({node,{}});
        else if (latest->second != node) result.push_back({node,latest->second});
    }
    for (const auto& [id,node] : revision.nodes) {
        if (!base.nodes.contains(id)) result.push_back({{},node});
    }
    return result;
}

}

SceneDocument::SceneDocument(Scene scene) : scene_(std::move(scene)), saved_(scene_) {
    validate_scene(scene_);
    history_.reserve(history_limit);
}

Scene SceneDocument::make_candidate(const std::vector<Edit>& edits, bool backwards) const {
    Scene updated = scene_;
    std::set<SceneId> touched;
    for (const auto& edit : edits) {
        const auto id = identity(edit);
        if (!touched.insert(id).second) throw std::runtime_error("Transaction writes the same node twice");
        const auto& before = backwards ? edit.after : edit.before;
        const auto& after = backwards ? edit.before : edit.after;
        const auto current = updated.nodes.find(id);
        if ((before && (current == updated.nodes.end() || current->second != *before)) ||
            (!before && current != updated.nodes.end())) {
            throw std::runtime_error("Edit expected a different node revision");
        }
        if (after) updated.nodes.insert_or_assign(id,*after);
        else updated.nodes.erase(id);
    }
    validate_scene(updated);
    return updated;
}

void SceneDocument::apply(std::vector<Edit> transaction) {
    std::erase_if(transaction, [](const auto& edit) { return edit.before == edit.after; });
    if (transaction.empty()) return;
    auto updated = make_candidate(transaction,false);
    history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
    if (history_.size() == history_limit) history_.erase(history_.begin());
    history_.push_back(std::move(transaction));
    scene_ = std::move(updated);
    cursor_ = history_.size();
}

bool SceneDocument::undo() {
    if (!cursor_) return false;
    scene_ = make_candidate(history_.at(cursor_-1),true);
    --cursor_;
    return true;
}

bool SceneDocument::redo() {
    if (cursor_ == history_.size()) return false;
    scene_ = make_candidate(history_.at(cursor_),false);
    ++cursor_;
    return true;
}

void SceneDocument::mark_saved() { saved_ = scene_; }

SceneId SceneDocument::allocate_id() const {
    if (scene_.nodes.empty()) return 1;
    const auto largest = scene_.nodes.rbegin()->first;
    if (largest == std::numeric_limits<SceneId>::max()) throw std::runtime_error("Scene ID limit");
    return largest+1;
}

std::unique_ptr<Game> SceneDocument::play() const { return std::make_unique<Game>(scene_,true); }

SceneDocument EditorFile::recover(const std::filesystem::path& path, const std::filesystem::path& journal,
                                  std::uint64_t& base, bool& recovered) {
    auto scene = load_scene(path);
    base = fingerprint(write_scene(scene));
    SceneDocument document(std::move(scene));
    if (!std::filesystem::exists(journal)) return document;
    const auto bytes = read_bounded(journal,2'001'000);
    const auto split = bytes.find('\n');
    if (split == std::string::npos || bytes.substr(0,split) != "ember-recovery 1 " + std::to_string(base)) {
        throw std::runtime_error("Recovery base mismatch. Inspect " + journal.string());
    }
    const auto revision = parse_scene(std::string_view(bytes).substr(split+1));
    if (revision.title != document.scene().title || revision.objective != document.scene().objective) {
        throw std::runtime_error("Recovery changed unsupported metadata");
    }
    document.apply(difference(document.scene(),revision));
    recovered = true;
    return document;
}

EditorFile::EditorFile(std::filesystem::path path)
    : path_(std::move(path)), journal_(path_.string()+".recover"),
      document_(recover(path_,journal_,base_,recovered_)) {}

void EditorFile::write_recovery() const {
    if (!document_.dirty()) {
        std::filesystem::remove(journal_);
        return;
    }
    const auto bytes = "ember-recovery 1 " + std::to_string(base_) + "\n" + write_scene(document_.scene());
    write_atomic(journal_,bytes);
}

void EditorFile::save() {
    const auto disk = load_scene(path_);
    if (fingerprint(write_scene(disk)) != base_) throw std::runtime_error("Scene changed outside this editor");
    const auto bytes = write_scene(document_.scene());
    write_atomic(path_,bytes);
    base_ = fingerprint(bytes);
    document_.mark_saved();
    std::filesystem::remove(journal_);
}

SceneId pick_node(const Scene& scene, Vec3 origin, Vec3 direction, float reach) {
    direction = unit(direction);
    const Box cube{{-0.5f,-0.5f,-0.5f},{0.5f,0.5f,0.5f}};
    const auto transforms = resolve_transforms(scene);
    SceneId best{};
    float closest = 1;
    for (const auto& [id,world] : transforms) {
        const auto inverse = inverse_affine(world);
        if (!inverse) continue;
        const auto local = transform_point(*inverse,origin);
        const auto end = transform_point(*inverse,origin+direction*reach);
        const auto hit = sweep_point(local,end-local,cube);
        if (hit && hit->fraction < closest) {
            best = id;
            closest = hit->fraction;
        }
    }
    return best;
}

std::vector<Edit> shift_nodes(const Scene& scene, std::span<const SceneId> ids, Vec3 delta) {
    const auto matrices = resolve_transforms(scene);
    std::vector<Edit> edits;
    edits.reserve(ids.size());
    for (const auto id : ids) {
        const auto& node = scene.nodes.at(id);
        auto updated = node;
        SceneId ancestor = node.parent;
        bool inherited = false;
        while (ancestor) {
            if (std::ranges::find(ids,ancestor) != ids.end()) { inherited = true; break; }
            ancestor = scene.nodes.at(ancestor).parent;
        }
        if (inherited) continue;
        auto local_delta = delta;
        if (node.parent) {
            const auto inverse = inverse_affine(matrices.at(node.parent));
            if (!inverse) throw std::runtime_error("Cannot edit relative to a singular parent");
            local_delta = transform_point(*inverse,delta)-transform_point(*inverse,{});
        }
        updated.position += local_delta;
        edits.push_back({node,std::move(updated)});
    }
    return edits;
}

}
