#include "emberfall/prefab.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <limits>

#include <sstream>
#include <stdexcept>

namespace ember {
namespace {

constexpr std::size_t byte_limit = 256*1024, patch_limit = 256, source_limit = 8, node_limit = 64;
using Field = std::pair<SceneId,std::string>;

struct Variant {
    std::string base;
    std::map<Field,std::string> overrides;
};

bool prefab_name(std::string_view name) {
    const auto allowed = [](char c) {
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.';
    };
    return name.size()>=8 && name.size()<=80 && name.ends_with(".prefab") &&
        name.front()!='.' && std::ranges::all_of(name,allowed);
}

void end(std::istream& row) {
    row >> std::ws;
    if (!row.eof()) throw std::runtime_error("Unexpected trailing prefab field");
}

std::string quoted(std::istream& row) {
    std::string value;
    row >> std::ws;
    if (row.peek()!='"' || !(row >> std::quoted(value)) || value.size()>240 ||
        value.find_first_of("\r\n")!=std::string::npos) throw std::runtime_error("Invalid quoted prefab value");
    return value;
}

Variant parse_variant(std::string_view bytes) {
    std::istringstream input{std::string(bytes)};
    std::string text, keyword;
    if (!std::getline(input,text) || text!="ember-prefab 2" || !std::getline(input,text)) {
        throw std::runtime_error("Expected ember-prefab 2 and base");
    }
    std::istringstream base_row(text);
    Variant variant;
    if (!(base_row >> keyword) || keyword!="base") throw std::runtime_error("Expected prefab base");
    variant.base = quoted(base_row);
    if (!prefab_name(variant.base)) throw std::runtime_error("Prefab base must be a local .prefab filename");
    end(base_row);
    while (std::getline(input,text)) {
        if (text.empty() || text.starts_with('#')) continue;
        std::istringstream row(text);
        row.imbue(std::locale::classic());
        std::int64_t id{};
        std::string field;
        if (!(row >> keyword >> id >> field) || keyword!="set" ||
            id<=0 || id>std::numeric_limits<SceneId>::max() || field.size()>24) {
            throw std::runtime_error("Invalid prefab override record");
        }
        const auto value = quoted(row);
        end(row);
        if (variant.overrides.size()==patch_limit ||
            !variant.overrides.emplace(Field{static_cast<SceneId>(id),std::move(field)},value).second) {
            throw std::runtime_error("Duplicate prefab override or override limit");
        }
    }
    return variant;
}

void apply(Scene& scene,const Variant& variant) {
    for (const auto& [property,value] : variant.overrides) {
        const auto& [id,field] = property;
        auto found = scene.nodes.find(id);
        if (found==scene.nodes.end()) throw std::runtime_error("Prefab override refers to missing node "+std::to_string(id));
        auto& node = found->second;
        if (field=="name") {
            node.name = value;
            continue;
        }
        if (field=="mesh") {
            node.mesh = value;
            continue;
        }
        std::istringstream input(value);
        input.imbue(std::locale::classic());
        if (field=="yaw" || field=="parameter") {
            float& scalar = field=="yaw" ? node.yaw : node.parameter;
            if (!(input >> scalar)) throw std::runtime_error("Expected one number in prefab override");
        } else {
            Vec3* vector = nullptr;
            if (field=="position") vector = &node.position;
            if (field=="size") vector = &node.size;
            if (field=="color") vector = &node.color;
            if (field=="destination") vector = &node.destination;
            if (!vector) throw std::runtime_error("Unsupported prefab override field: "+field);
            if (!(input >> vector->x >> vector->y >> vector->z)) throw std::runtime_error("Expected three numbers in prefab override");
        }
        end(input);
    }
    validate_scene(scene);
}

std::string read_source(const std::filesystem::path& path) {
    if (!prefab_name(path.filename().string()) || !std::filesystem::is_regular_file(path) ||
        std::filesystem::is_symlink(path)) throw std::runtime_error("Expected regular local prefab: "+path.string());
    return read_bounded(path,byte_limit);
}

Scene resolve(const std::filesystem::path& file,std::vector<std::string>& chain) {
    const auto name = file.filename().string();
    if (chain.size()==source_limit || std::ranges::find(chain,name)!=chain.end()) {
        throw std::runtime_error("Prefab inheritance cycle or depth limit");
    }
    chain.push_back(name);
    const auto bytes = read_source(file);
    if (bytes.starts_with("ember-scene 1\n")) {
        auto scene = parse_scene(bytes);
        if (scene.nodes.empty() || scene.nodes.size()>node_limit) throw std::runtime_error("Prefab node limit");
        return scene;
    }
    const auto variant = parse_variant(bytes);
    auto scene = resolve(file.parent_path()/variant.base,chain);
    apply(scene,variant);
    return scene;
}

std::string encode(const Variant& variant) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "ember-prefab 2\nbase " << std::quoted(variant.base) << '\n';
    for (const auto& [property,value] : variant.overrides) {
        output << "set " << property.first << ' ' << property.second << ' ' << std::quoted(value) << '\n';
    }
    return output.str();
}

void validate_variant(const std::filesystem::path& file,const Variant& variant) {
    const auto bytes = encode(variant);
    (void)parse_variant(bytes);
    std::vector chain{file.filename().string()};
    auto scene = resolve(file.parent_path()/variant.base,chain);
    apply(scene,variant);
}

}

ResolvedPrefab load_prefab(const std::filesystem::path& file) {
    ResolvedPrefab result;
    result.scene = resolve(file,result.sources);
    return result;
}

void derive_prefab(const std::filesystem::path& file,std::string base) {
    if (!prefab_name(file.filename().string()) || !prefab_name(base) ||
        std::filesystem::symlink_status(file).type()!=std::filesystem::file_type::not_found) {
        throw std::runtime_error("Expected a new prefab path and a local base filename");
    }
    const Variant variant{std::move(base),{}};
    validate_variant(file,variant);
    write_atomic(file,encode(variant));
}

void change_prefab(const std::filesystem::path& file,SceneId id,std::string field,std::optional<std::string> value) {
    const auto original = read_source(file);
    auto variant = parse_variant(original);
    const Field property{id,std::move(field)};
    if (value) {
        if (property.second.empty() || property.second.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos ||
            !property.first || value->find_first_of("\r\n")!=std::string::npos) {
            throw std::runtime_error("Invalid prefab override argument");
        }
        variant.overrides.insert_or_assign(property,std::move(*value));
    } else if (!variant.overrides.erase(property)) {
        throw std::runtime_error("Prefab field is already inherited");
    }
    validate_variant(file,variant);
    if (read_source(file)!=original) throw std::runtime_error("Prefab changed while editing");
    write_atomic(file,encode(variant));
}

}
