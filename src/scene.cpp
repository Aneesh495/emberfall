#include "emberfall/scene.hpp"

#include <array>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace ember {
namespace {

constexpr std::array<std::string_view,11> kinds{
    "decor", "solid", "spawn", "plate", "door", "exit", "checkpoint",
    "mirror", "receiver", "platform", "crate"
};
constexpr std::size_t scene_limit = 2'000'000;
constexpr std::size_t node_limit = 8192;

[[noreturn]] void invalid(std::string reason) {
    throw std::runtime_error("Scene: " + reason);
}

bool valid_text(std::string_view text, std::size_t limit) {
    return !text.empty() && text.size() <= limit &&
           std::ranges::all_of(text, [](unsigned char c) { return c >= 32 && c != 127; });
}

bool bounded(Vec3 value, float bound) {
    return finite(value) && std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)}) <= bound;
}

std::istream& operator>>(std::istream& in, Vec3& v) { return in >> v.x >> v.y >> v.z; }
std::ostream& operator<<(std::ostream& out, Vec3 v) { return out << v.x << ' ' << v.y << ' ' << v.z; }

void exact_end(std::istream& line, unsigned number) {
    if (!line || (line >> std::ws, !line.eof())) invalid("malformed record on line " + std::to_string(number));
}

bool linked(Kind kind) {
    return kind == Kind::door || kind == Kind::platform || kind == Kind::mirror || kind == Kind::exit;
}

}

bool valid_mesh_name(std::string_view name) {
    const auto alnum = [](char value) { return (value>='a' && value<='z') || (value>='0' && value<='9'); };
    return name.size()>5 && name.size()<=80 && name.ends_with(".mesh") && alnum(name.front()) &&
           std::ranges::all_of(name,[&](char value) {
               return alnum(value) || value=='-' || value=='_' || value=='.';
           });
}

std::string_view kind_name(Kind kind) {
    return kinds.at(static_cast<std::size_t>(kind));
}

std::map<SceneId,Mat4> resolve_transforms(const Scene& scene) {
    std::map<SceneId,Mat4> matrices;
    for (const auto& [id,node] : scene.nodes) {
        (void)node;
        std::array<SceneId,65> chain{};
        std::size_t depth = 0;
        auto cursor = id;
        while (cursor && !matrices.contains(cursor)) {
            if (depth == chain.size() || std::find(chain.begin(), chain.begin() + static_cast<long>(depth), cursor)
                                          != chain.begin() + static_cast<long>(depth)) {
                invalid("cyclic or too-deep hierarchy at node " + std::to_string(id));
            }
            const auto entry = scene.nodes.find(cursor);
            if (entry == scene.nodes.end()) invalid("missing parent " + std::to_string(cursor));
            chain[depth++] = cursor;
            cursor = entry->second.parent;
        }
        Mat4 parent = cursor ? matrices.at(cursor) : Mat4::identity();
        while (depth) {
            cursor = chain[--depth];
            const auto& child = scene.nodes.at(cursor);
            parent = parent * transform_matrix(child.position, yaw_rotation(child.yaw), child.size);
            if (!std::ranges::all_of(parent.data, [](float v) { return std::isfinite(v) && std::abs(v) < 1e8f; })) {
                invalid("transform exceeds world bounds at node " + std::to_string(cursor));
            }
            matrices.emplace(cursor, parent);
        }
    }
    return matrices;
}

void validate_scene(const Scene& scene) {
    if (!valid_text(scene.title, 80) || !valid_text(scene.objective, 240)) invalid("invalid title or objective");
    if (scene.nodes.size() > node_limit) invalid("too many nodes");
    for (const auto& [id,n] : scene.nodes) {
        if (!id || id != n.id || static_cast<std::size_t>(n.kind) >= kinds.size() ||
            !valid_text(n.name, 80) || !bounded(n.position, 2000) || !bounded(n.size, 256) ||
            std::min({std::abs(n.size.x), std::abs(n.size.y), std::abs(n.size.z)}) < 0.01f ||
            !std::isfinite(n.yaw) || std::abs(n.yaw) > 64 || !bounded(n.destination, 2000) ||
            !finite(n.color) || std::min({n.color.x,n.color.y,n.color.z}) < 0 ||
            std::max({n.color.x,n.color.y,n.color.z}) > 8 || !std::isfinite(n.parameter) ||
            n.parameter < 0 || n.parameter > 3600) {
            invalid("invalid values at node " + std::to_string(id));
        }
        if (!n.mesh.empty() && (n.kind!=Kind::decor || !valid_mesh_name(n.mesh))) {
            invalid("mesh reference requires a decorative node and a local .mesh name");
        }
        if (n.parent && n.kind != Kind::decor) invalid("gameplay and solid nodes must be roots");
        if (n.link) {
            const auto source = scene.nodes.find(n.link);
            if (!linked(n.kind) || source == scene.nodes.end() ||
                (n.kind == Kind::mirror && source->second.kind != Kind::receiver) ||
                (n.kind != Kind::mirror && source->second.kind != Kind::plate &&
                 source->second.kind != Kind::receiver)) {
                invalid("invalid circuit link at node " + std::to_string(id));
            }
        }
        if ((n.kind == Kind::mirror || n.kind == Kind::door) && !n.link) invalid("circuit link required");
    }
    resolve_transforms(scene);
}

Scene parse_scene(std::string_view bytes) {
    if (bytes.size() > scene_limit) invalid("byte limit");
    std::istringstream source{std::string(bytes)};
    std::string text;
    if (!std::getline(source, text) || text != "ember-scene 1") invalid("unsupported scene header");
    Scene scene;
    std::map<SceneId,std::string> models;
    unsigned number = 1;
    bool seen_title = false;
    bool seen_objective = false;
    while (std::getline(source,text)) {
        ++number;
        if (text.empty() || text.starts_with('#')) continue;
        if (text.size() > 1024) invalid("line too long");
        std::istringstream line(text);
        line.imbue(std::locale::classic());
        std::string type;
        line >> type;
        if (type == "title" && !seen_title) {
            line >> std::quoted(scene.title);
            seen_title = true;
        } else if (type == "objective" && !seen_objective) {
            line >> std::quoted(scene.objective);
            seen_objective = true;
        } else if (type == "node") {
            if (scene.nodes.size() == node_limit) invalid("node limit");
            Node n;
            std::string kind;
            line >> n.id >> n.parent >> kind >> std::quoted(n.name)
                 >> n.position >> n.size >> n.yaw >> n.color >> n.link >> n.destination >> n.parameter;
            const auto found = std::ranges::find(kinds, kind);
            if (found == kinds.end()) invalid("unknown node kind on line " + std::to_string(number));
            n.kind = static_cast<Kind>(found - kinds.begin());
            if (!scene.nodes.emplace(n.id, n).second) invalid("duplicate node ID");
        } else if (type == "model") {
            SceneId id{};
            std::string asset;
            line >> id >> asset;
            if (!valid_mesh_name(asset) || models.size()>=node_limit || !models.emplace(id,std::move(asset)).second) {
                invalid("invalid or repeated model reference on line " + std::to_string(number));
            }
        } else {
            invalid("unknown record on line " + std::to_string(number));
        }
        exact_end(line, number);
    }
    for (const auto& [id,asset] : models) {
        const auto node = scene.nodes.find(id);
        if (node==scene.nodes.end()) invalid("model reference has no node " + std::to_string(id));
        node->second.mesh = asset;
    }
    validate_scene(scene);
    return scene;
}

Scene load_scene(const std::filesystem::path& file) {
    return parse_scene(read_bounded(file, scene_limit));
}

std::string write_scene(const Scene& scene) {
    validate_scene(scene);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "ember-scene 1\ntitle " << std::quoted(scene.title)
        << "\nobjective " << std::quoted(scene.objective) << '\n';
    for (const auto& [id,n] : scene.nodes) {
        out << "node " << id << ' ' << n.parent << ' ' << kind_name(n.kind) << ' ' << std::quoted(n.name)
            << ' ' << n.position << ' ' << n.size << ' ' << n.yaw << ' ' << n.color
            << ' ' << n.link << ' ' << n.destination << ' ' << n.parameter << '\n';
        if (!n.mesh.empty()) out << "model " << id << ' ' << n.mesh << '\n';
    }
    return out.str();
}

}
