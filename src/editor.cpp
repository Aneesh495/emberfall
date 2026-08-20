#include "emberfall/editor.hpp"

#include "emberfall/campaign.hpp"
#include "scene_records.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <set>

namespace ember {
namespace {

constexpr std::size_t history_limit = 2048;
constexpr std::size_t journal_limit = 16*1024*1024, root_limit = 2'000'000, transaction_limit = 16'384;

void end_row(std::istream& row) {
    if (!row || (row >> std::ws, !row.eof())) throw std::runtime_error("Malformed recovery journal record");
}

std::string_view line(std::string_view& bytes) {
    const auto end = bytes.find('\n');
    if (end==std::string_view::npos || end>1024) throw std::runtime_error("Truncated or oversized recovery journal record");
    const auto value = bytes.substr(0,end);
    bytes.remove_prefix(end+1);
    return value;
}

std::optional<Node> recovery_node(std::string_view& payload,unsigned index) {
    std::istringstream row{std::string(line(payload))};
    row.imbue(std::locale::classic());
    int present{};
    if (!(row >> present) || (present!=0 && present!=1)) throw std::runtime_error("Invalid recovery node marker");
    std::optional<Node> node;
    if (present) {
        node = scene_records::read_node(row,index);
        row >> std::ws;
        if (row.peek()!='"' || !(row >> std::quoted(node->mesh))) throw std::runtime_error("Invalid recovery mesh field");
    }
    end_row(row);
    return node;
}

void write_recovery_node(std::ostream& row,const std::optional<Node>& node) {
    if (!node) {
        row << "0\n";
        return;
    }
    row << "1 ";
    scene_records::write_node(row,*node);
    row << ' ' << std::quoted(node->mesh) << '\n';
}

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

SceneDocument::SceneDocument(Scene scene) : scene_(std::move(scene)), saved_(scene_), history_base_(scene_) {
    validate_scene(scene_);
    history_.reserve(history_limit);
}

Scene SceneDocument::make_candidate(const Scene& base,const std::vector<Edit>& edits,bool backwards) {
    Scene updated = base;
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
    if (transaction.size()>transaction_limit) throw std::runtime_error("Edit transaction node limit");
    auto updated = make_candidate(scene_,transaction,false);
    if (cursor_==history_limit) {
        auto base = make_candidate(history_base_,history_.front(),false);
        history_.erase(history_.begin());
        history_base_ = std::move(base);
    } else history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
    history_.push_back(std::move(transaction));
    scene_ = std::move(updated);
    cursor_ = history_.size();
}

bool SceneDocument::undo() {
    if (!cursor_) return false;
    scene_ = make_candidate(scene_,history_.at(cursor_-1),true);
    --cursor_;
    return true;
}

bool SceneDocument::redo() {
    if (cursor_ == history_.size()) return false;
    scene_ = make_candidate(scene_,history_.at(cursor_),false);
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

std::unique_ptr<Game> SceneDocument::play(std::shared_ptr<const MeshLibrary> assets) const {
    return std::make_unique<Game>(scene_,true,std::move(assets));
}

SceneDocument EditorFile::recover(const std::filesystem::path& path, const std::filesystem::path& journal,
                                  std::uint64_t& base, bool& recovered) {
    auto saved = load_scene(path);
    base = fingerprint(write_scene(saved));
    if (!std::filesystem::exists(journal)) return SceneDocument(std::move(saved));
    const auto bytes = read_bounded(journal,journal_limit);
    std::string_view payload = bytes;
    std::istringstream header{std::string(line(payload))};
    std::string marker;
    unsigned version{};
    std::uint64_t fingerprint_on_disk{};
    if (!(header >> marker >> version >> fingerprint_on_disk) || marker!="ember-recovery" ||
        fingerprint_on_disk!=base) throw std::runtime_error("Recovery base mismatch. Inspect "+journal.string());
    const auto verify_metadata = [&](const Scene& scene) {
        if (scene.title!=saved.title || scene.objective!=saved.objective) throw std::runtime_error("Recovery changed scene metadata");
    };
    if (version==1) {
        end_row(header);
        const auto revision = parse_scene(payload);
        verify_metadata(revision);
        SceneDocument document(std::move(saved));
        document.apply(difference(document.scene(),revision));
        recovered = true;
        return document;
    }
    std::uint64_t checksum{};
    if (version!=2 || !(header >> checksum) || checksum!=fingerprint(payload)) {
        throw std::runtime_error("Invalid recovery journal version or checksum");
    }
    end_row(header);
    std::istringstream settings{std::string(line(payload))};
    std::size_t cursor{}, count{}, root_bytes{};
    if (!(settings >> marker >> cursor >> count >> root_bytes) || marker!="history" || cursor>count ||
        count>history_limit || root_bytes>root_limit || root_bytes>payload.size()) {
        throw std::runtime_error("Invalid recovery history limits");
    }
    end_row(settings);
    auto root = parse_scene(payload.substr(0,root_bytes));
    verify_metadata(root);
    payload.remove_prefix(root_bytes);
    SceneDocument document(std::move(root));
    Scene visible;
    if (!cursor) visible = document.scene();
    for (std::size_t i=0; i<count; ++i) {
        std::istringstream record{std::string(line(payload))};
        std::size_t edits{};
        if (!(record >> marker >> edits) || marker!="edit" || !edits || edits>transaction_limit) {
            throw std::runtime_error("Invalid recovery transaction size");
        }
        end_row(record);
        std::vector<Edit> transaction;
        transaction.reserve(edits);
        for (std::size_t item=0; item<edits; ++item) {
            const auto index = static_cast<unsigned>(item+1);
            transaction.push_back({recovery_node(payload,index),recovery_node(payload,index)});
        }
        document.apply(std::move(transaction));
        if (document.cursor()!=i+1) throw std::runtime_error("Empty recovery transaction");
        if (document.cursor()==cursor) visible = document.scene();
    }
    if (!payload.empty()) throw std::runtime_error("Trailing recovery history bytes");
    document.scene_ = std::move(visible);
    document.saved_ = std::move(saved);
    document.cursor_ = cursor;
    recovered = true;
    return document;
}

EditorFile::EditorFile(std::filesystem::path path)
    : path_(std::move(path)), journal_(path_.string()+".recover"),
      document_(recover(path_,journal_,base_,recovered_)) {}

void EditorFile::write_recovery() const {
    if (!document_.dirty() && document_.history_.empty()) {
        std::filesystem::remove(journal_);
        return;
    }
    const auto write_snapshot = [&] {
        write_atomic(journal_,"ember-recovery 1 "+std::to_string(base_)+"\n"+write_scene(document_.scene()));
    };
    const auto root = write_scene(document_.history_base_);
    std::ostringstream history;
    history.imbue(std::locale::classic());
    history << std::setprecision(std::numeric_limits<float>::max_digits10);
    history << "history " << document_.cursor_ << ' ' << document_.history_.size() << ' ' << root.size() << '\n' << root;
    for (const auto& transaction : document_.history_) {
        history << "edit " << transaction.size() << '\n';
        for (const auto& edit : transaction) {
            write_recovery_node(history,edit.before);
            write_recovery_node(history,edit.after);
            if (history.tellp()>static_cast<std::streamoff>(journal_limit-128)) {
                write_snapshot();
                return;
            }
        }
    }
    const auto payload = std::move(history).str();
    const auto header = "ember-recovery 2 "+std::to_string(base_)+" "+std::to_string(fingerprint(payload))+"\n";
    write_atomic(journal_,header+payload);
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

std::vector<Edit> instantiate_prefab(const Scene& prefab, SceneId first, Vec3 offset) {
    validate_scene(prefab);
    if (prefab.nodes.empty() || prefab.nodes.size() > 64) throw std::runtime_error("Prefab node limit");
    std::map<SceneId,SceneId> mapping;
    std::vector<Edit> added;
    for (const auto& [id,n] : prefab.nodes) {
        (void)n;
        if (!first) throw std::runtime_error("Prefab ID overflow");
        mapping.emplace(id,first++);
    }
    for (const auto& [id,node] : prefab.nodes) {
        auto instance = node;
        instance.id = mapping.at(id);
        if (node.parent) instance.parent = mapping.at(node.parent);
        else instance.position += offset;
        if (node.link) instance.link = mapping.at(node.link);
        if (node.kind == Kind::platform) instance.destination += offset;
        added.push_back({{},std::move(instance)});
    }
    return added;
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
