#include "emberfall/mesh.hpp"

#include <bit>
#include <stdexcept>

namespace ember {
namespace {

constexpr std::size_t max_mesh_vertices = 16'384;
constexpr std::size_t max_mesh_primitives = 32;
constexpr std::size_t max_mesh_bytes = 12 + max_mesh_primitives*36 + max_mesh_vertices*24;
constexpr std::size_t max_library_vertices = 262'144;

class Reader {
public:
    explicit Reader(std::string_view bytes) : remaining_(bytes) {}

    std::uint32_t u32() {
        if (remaining_.size()<4) throw std::runtime_error("Truncated mesh data");
        std::uint32_t value = 0;
        for (unsigned i=0; i<4; ++i) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(remaining_[i])) << (8*i);
        }
        remaining_.remove_prefix(4);
        return value;
    }

    float real(float low, float high) {
        const float value = std::bit_cast<float>(u32());
        if (!(value>=low && value<=high)) throw std::runtime_error("Mesh value outside format bounds");
        return value;
    }

    Vec3 vector(float low, float high) { return {real(low,high),real(low,high),real(low,high)}; }
    std::size_t remaining() const { return remaining_.size(); }

private:
    std::string_view remaining_;
};

}

MeshAsset read_mesh(std::string_view bytes) {
    if (bytes.size()>max_mesh_bytes || !bytes.starts_with("EMBMSH01")) {
        throw std::runtime_error("Invalid mesh magic or size");
    }
    Reader reader(bytes.substr(8));
    const auto primitives = reader.u32();
    if (!primitives || primitives>max_mesh_primitives) throw std::runtime_error("Invalid mesh primitive count");
    MeshAsset mesh;
    mesh.primitives.reserve(primitives);
    for (std::uint32_t i=0; i<primitives; ++i) {
        const auto count = reader.u32();
        if (!count || count%3 || count>max_mesh_vertices-mesh.vertices) {
            throw std::runtime_error("Invalid mesh vertex count");
        }
        const SurfaceMaterial material{reader.vector(0,1),reader.real(0,1),reader.real(0,1),reader.vector(0,1)};
        if (count > reader.remaining()/24) throw std::runtime_error("Mesh vertex records truncated");
        MeshPrimitive primitive{material,{}};
        primitive.vertices.reserve(count);
        for (std::uint32_t j=0; j<count; ++j) {
            const auto position = reader.vector(-4096,4096);
            const auto normal = reader.vector(-1,1);
            if (std::abs(dot(normal,normal)-1)>0.001f) throw std::runtime_error("Non-unit mesh normal");
            primitive.vertices.push_back({position,normal});
        }
        mesh.vertices += count;
        mesh.primitives.push_back(std::move(primitive));
    }
    if (reader.remaining()) throw std::runtime_error("Trailing mesh data");
    return mesh;
}

void MeshLibrary::load(const Scene& scene, bool allow_missing) {
    std::size_t vertices = 0, primitives = 0;
    for (const auto& [id,node] : scene.nodes) {
        (void)id;
        if (node.mesh.empty()) continue;
        if (!valid_mesh_name(node.mesh)) throw std::runtime_error("Invalid scene mesh name: " + node.mesh);
        auto entry = assets_.find(node.mesh);
        if (entry == assets_.end()) {
            const auto path = directory_/node.mesh;
            if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path)) {
                if (allow_missing) continue;
                throw std::runtime_error("Missing regular cooked mesh: " + path.string());
            }
            MeshAsset mesh;
            try {
                mesh = read_mesh(read_bounded(path,max_mesh_bytes));
            } catch (const std::exception& error) {
                throw std::runtime_error(path.string()+": "+error.what());
            }
            if (mesh.vertices>max_library_vertices-resident_vertices_) {
                throw std::runtime_error("Mesh library residency limit exceeded");
            }
            const auto size = mesh.vertices;
            entry = assets_.emplace(node.mesh,std::move(mesh)).first;
            resident_vertices_ += size;
        }
        vertices += entry->second.vertices;
        primitives += entry->second.primitives.size();
        if (vertices>vertex_limit || primitives>primitive_limit) {
            throw std::runtime_error("Scene imported geometry exceeds its per-frame budget");
        }
    }
}

const MeshAsset* MeshLibrary::find(std::string_view name) const {
    const auto found = assets_.find(name);
    return found == assets_.end() ? nullptr : &found->second;
}

}
