#include "emberfall/mesh.hpp"

#include <bit>
#include <stdexcept>
#include <set>

namespace ember {
namespace {

constexpr std::size_t max_mesh_vertices = 16'384;
constexpr std::size_t max_mesh_primitives = 32;
constexpr std::size_t max_skin_keys = 16'384;
constexpr std::size_t max_mesh_bytes =
    2'000'000;
constexpr std::size_t max_library_vertices = 262'144;
constexpr std::size_t max_library_textures = 128;
constexpr std::size_t max_library_bytes = 64'000'000;

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
    std::string_view bytes(std::size_t count) {
        if (remaining_.size()<count) throw std::runtime_error("Truncated mesh payload");
        const auto result = remaining_.substr(0,count);
        remaining_.remove_prefix(count);
        return result;
    }

    std::size_t remaining() const { return remaining_.size(); }

private:
    std::string_view remaining_;
};

Quat rotation(Reader& reader) {
    return {reader.real(-1.001f,1.001f),reader.real(-1.001f,1.001f),
            reader.real(-1.001f,1.001f),reader.real(-1.001f,1.001f)};
}

Vec3 scale(Reader& reader) { return reader.vector(0.75f,1.25f); }

bool orthonormal(const Mat4& matrix) {
    const auto axis = [&](int i) { return Vec3{matrix(0,i),matrix(1,i),matrix(2,i)}; };
    const auto x = axis(0), y = axis(1), z = axis(2);
    return std::abs(dot(x,x)-1)<0.002f && std::abs(dot(y,y)-1)<0.002f && std::abs(dot(z,z)-1)<0.002f &&
           std::abs(dot(x,y))<0.002f && std::abs(dot(x,z))<0.002f && std::abs(dot(y,z))<0.002f;
}

template<class Value,class Read>
void read_channel(Reader& reader,Channel<Value>& channel,std::size_t& budget,Read read_value) {
    const auto count = reader.u32();
    if (count>4096 || count>budget) throw std::runtime_error("Mesh animation key budget");
    budget -= count;
    if (!count) return;
    const auto interpolation = reader.u32();
    if (interpolation>1) throw std::runtime_error("Invalid mesh animation interpolation");
    channel.stepped = interpolation == 1;
    channel.keys.reserve(count);
    for (std::uint32_t i=0; i<count; ++i) {
        channel.keys.push_back({reader.real(0,3600),read_value(reader)});
    }
}

MeshRig read_rig(Reader& reader) {
    const auto count = reader.u32(), clip_count = reader.u32();
    if (!count || count>128 || clip_count>8) throw std::runtime_error("Mesh rig count outside bounds");
    std::vector<Joint> joints;
    std::vector<unsigned> depths;
    std::vector<Mat4> inverse_binds;
    for (std::uint32_t i=0; i<count; ++i) {
        const auto parent = reader.u32();
        if (parent>i) throw std::runtime_error("Invalid mesh joint parent order");
        const unsigned depth = parent ? 1+depths[parent-1] : 1;
        if (depth>16) throw std::runtime_error("Mesh joint hierarchy too deep");
        depths.push_back(depth);
        joints.push_back({static_cast<int>(parent)-1,{reader.vector(-4096,4096),rotation(reader),scale(reader)}});
        Mat4 inverse_bind;
        for (auto& component : inverse_bind.data) component = reader.real(-4096,4096);
        if (!orthonormal(inverse_bind)) throw std::runtime_error("Mesh inverse bind must be rigid");
        inverse_binds.push_back(inverse_bind);
    }
    MeshRig rig{Skeleton(std::move(joints),std::move(inverse_binds)),{}};
    std::size_t keys_left = max_skin_keys;
    for (std::uint32_t i=0; i<clip_count; ++i) {
        const auto duration = reader.real(0,3600);
        std::vector<JointChannels> channels(count);
        for (auto& joint : channels) {
            read_channel(reader,joint.translation,keys_left,[](Reader& r) { return r.vector(-4096,4096); });
            read_channel(reader,joint.rotation,keys_left,rotation);
            read_channel(reader,joint.scale,keys_left,scale);
        }
        rig.clips.emplace_back(duration,std::move(channels));
    }
    return rig;
}

}

MeshAsset read_mesh(std::string_view bytes) {
    const bool skinned = bytes.starts_with("EMBMSH03");
    const bool textured = skinned || bytes.starts_with("EMBMSH02");
    if (bytes.size()>max_mesh_bytes || (!textured && !bytes.starts_with("EMBMSH01"))) {
        throw std::runtime_error("Invalid mesh magic or size");
    }
    Reader reader(bytes.substr(8));
    const auto primitives = reader.u32();
    if (!primitives || primitives>max_mesh_primitives) throw std::runtime_error("Invalid mesh primitive count");
    MeshAsset mesh;
    if (textured) {
        const auto count = reader.u32();
        if ((!count && !skinned) || count>MeshTexture::limit) throw std::runtime_error("Invalid mesh texture count");
        mesh.textures.reserve(count);
        for (std::uint32_t i=0; i<count; ++i) {
            mesh.textures.push_back({std::string(reader.bytes(MeshTexture::bytes))});
        }
    }
    if (skinned) mesh.rig.emplace(read_rig(reader));
    mesh.primitives.reserve(primitives);
    for (std::uint32_t i=0; i<primitives; ++i) {
        const auto count = reader.u32();
        if (!count || count%3 || count>max_mesh_vertices-mesh.vertices) {
            throw std::runtime_error("Invalid mesh vertex count");
        }
        SurfaceMaterial material{reader.vector(0,1),reader.real(0,1),reader.real(0,1),reader.vector(0,1)};
        if (textured) {
            for (auto& texture : material.textures) {
                texture = reader.u32();
                if (texture>mesh.textures.size()) throw std::runtime_error("Mesh texture index outside range");
            }
            material.normal_scale = reader.real(0,4);
        }
        const auto vertex_bytes = skinned ? 68U : textured ? 48U : 24U;
        if (count > reader.remaining()/vertex_bytes) throw std::runtime_error("Mesh vertex records truncated");
        MeshPrimitive primitive{material,{}};
        if (skinned) primitive.weighted.reserve(count);
        else primitive.vertices.reserve(count);
        for (std::uint32_t j=0; j<count; ++j) {
            const auto position = reader.vector(-4096,4096);
            const auto normal = reader.vector(-1,1);
            if (std::abs(dot(normal,normal)-1)>0.001f) throw std::runtime_error("Non-unit mesh normal");
            MeshVertex vertex{position,normal};
            if (textured) {
                vertex.uv = {reader.real(-1024,1024),reader.real(-1024,1024)};
                const auto tangent = reader.vector(-1,1);
                const auto handedness = reader.real(-1,1);
                if ((handedness!=1 && handedness!=-1) ||
                    (material.textures[2] && (std::abs(dot(tangent,tangent)-1)>0.001f ||
                                              std::abs(dot(tangent,normal))>0.011f))) {
                    throw std::runtime_error("Invalid normal-map tangent basis");
                }
                vertex.tangent = {tangent.x,tangent.y,tangent.z,handedness};
            }
            if (mesh.rig) {
                WeightedVertex weighted{vertex};
                for (auto& joint : weighted.joints) {
                    joint = static_cast<std::uint8_t>(reader.bytes(1).front());
                    if (joint>=mesh.rig->skeleton.joints().size()) throw std::runtime_error("Mesh joint index outside rig");
                }
                float sum = 0;
                for (auto& weight : weighted.weights) {
                    weight = reader.real(0,1);
                    sum += weight;
                }
                if (std::abs(sum-1)>0.002f) throw std::runtime_error("Invalid mesh skin weight sum");
                primitive.weighted.push_back(weighted);
            } else {
                primitive.vertices.push_back(vertex);
            }
        }
        mesh.vertices += count;
        mesh.primitives.push_back(std::move(primitive));
    }
    if (reader.remaining()) throw std::runtime_error("Trailing mesh data");
    return mesh;
}

void MeshLibrary::load(const Scene& scene, bool allow_missing) {
    std::size_t vertices = 0, primitives = 0, textures = 0;
    std::set<std::string_view> seen;
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
            std::size_t bytes = 0;
            try {
                const auto data = read_bounded(path,max_mesh_bytes);
                bytes = data.size();
                mesh = read_mesh(data);
            } catch (const std::exception& error) {
                throw std::runtime_error(path.string()+": "+error.what());
            }
            if (mesh.vertices>max_library_vertices-resident_vertices_ ||
                mesh.textures.size()>max_library_textures-resident_textures_ ||
                bytes>max_library_bytes-resident_bytes_) {
                throw std::runtime_error("Mesh library residency limit exceeded");
            }
            const auto size = mesh.vertices;
            entry = assets_.emplace(node.mesh,std::move(mesh)).first;
            resident_vertices_ += size;
            resident_textures_ += entry->second.textures.size();
            resident_bytes_ += bytes;
        }
        vertices += entry->second.vertices;
        primitives += entry->second.primitives.size();
        if (seen.insert(node.mesh).second) textures += entry->second.textures.size();
        if (vertices>vertex_limit || primitives>primitive_limit || textures>MeshTexture::limit) {
            throw std::runtime_error("Scene imported geometry exceeds its per-frame budget");
        }
    }
}

const MeshAsset* MeshLibrary::find(std::string_view name) const {
    const auto found = assets_.find(name);
    return found == assets_.end() ? nullptr : &found->second;
}

}
