#pragma once

#include "emberfall/animation.hpp"
#include "emberfall/scene.hpp"

#include <memory>

namespace ember {

struct SurfaceMaterial {
    Vec3 base_color{1,1,1};
    float metallic{}, roughness{1};
    Vec3 emissive{};
};

struct MeshPrimitive {
    SurfaceMaterial material;
    std::vector<MeshVertex> vertices;
};

struct MeshAsset {
    std::vector<MeshPrimitive> primitives;
    std::size_t vertices{};
};

MeshAsset read_mesh(std::string_view bytes);

class MeshLibrary {
public:
    static constexpr std::size_t vertex_limit = 32'768;
    static constexpr std::size_t primitive_limit = 112;
    explicit MeshLibrary(std::filesystem::path directory) : directory_(std::move(directory)) {}

    void load(const Scene& scene, bool allow_missing = false);
    const MeshAsset* find(std::string_view name) const;

private:
    std::filesystem::path directory_;
    std::map<std::string, MeshAsset, std::less<>> assets_;
    std::size_t resident_vertices_{};
};

struct MeshInstance {
    const MeshAsset* asset{};
    Mat4 model;
    Vec3 tint{1,1,1};
    std::uint64_t history_id{};
};

}
