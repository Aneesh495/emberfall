#pragma once

#include "emberfall/math.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace ember {

using SceneId = std::uint32_t;
enum class Kind : std::uint8_t {
    decor, solid, spawn, plate, door, exit, checkpoint, mirror, receiver, platform, crate
};

struct Node {
    SceneId id{}, parent{};
    Kind kind{};
    std::string name;
    Vec3 position{};
    Vec3 size{1,1,1};
    float yaw{};
    Vec3 color{0.5f,0.5f,0.5f};
    SceneId link{};
    Vec3 destination{};
    float parameter{};
    friend bool operator==(const Node&, const Node&) = default;
};

struct Scene {
    std::string title, objective;
    std::map<SceneId,Node> nodes;
    friend bool operator==(const Scene&, const Scene&) = default;
};

std::string_view kind_name(Kind kind);
void validate_scene(const Scene& scene);
Scene parse_scene(std::string_view bytes);
Scene load_scene(const std::filesystem::path& file);
std::string write_scene(const Scene& scene);
std::map<SceneId,Mat4> resolve_transforms(const Scene& scene);
std::string read_bounded(const std::filesystem::path& file, std::size_t limit);
void write_atomic(const std::filesystem::path& file, std::string_view bytes);

}
