#include "emberfall/convex.hpp"

#include <map>
#include <stdexcept>

namespace ember {

ConvexHull::ConvexHull(std::vector<Vec3> vertices,std::vector<Face> faces)
    : vertices_(std::move(vertices)), faces_(std::move(faces)) {
    if (vertices_.size()<4 || vertices_.size()>vertex_limit || faces_.size()<4 || faces_.size()>face_limit) {
        throw std::runtime_error("Convex hull exceeds topology bounds");
    }
    Vec3 center{};
    for (const auto p : vertices_) {
        if (!finite(p) || std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>100) {
            throw std::runtime_error("Hull vertex outside geometry bounds");
        }
        center += p;
    }
    center = center/static_cast<float>(vertices_.size());
    std::map<Edge,std::pair<unsigned,int>> edges;
    std::array<bool,vertex_limit> used{};
    for (auto& face : faces_) {
        if (std::ranges::any_of(face,[&](auto index) { return index>=vertices_.size(); })) {
            throw std::runtime_error("Hull triangle references a missing vertex");
        }
        for (auto index : face) used[index] = true;
        const auto a = vertices_[face[0]], b = vertices_[face[1]], c = vertices_[face[2]];
        auto n = unit(cross(b-a,c-a));
        if (n==Vec3{} || std::abs(dot(n,center-a))<1e-5f) {
            throw std::runtime_error("Hull contains a degenerate face or volume");
        }
        if (dot(n,center-a)>0) {
            n = -n;
            std::swap(face[1],face[2]);
        }
        if (std::ranges::any_of(vertices_,[&](Vec3 p) { return dot(n,p-a)>1e-4f; })) {
            throw std::runtime_error("Hull face cuts through the vertex set");
        }
        for (std::size_t i=0; i<3; ++i) {
            const auto u = face[i], v = face[(i+1)%3];
            auto& entry = edges[{std::min(u,v),std::max(u,v)}];
            ++entry.first;
            entry.second += u<v ? 1 : -1;
        }
    }
    for (std::size_t i=0; i<vertices_.size(); ++i) {
        if (!used[i]) throw std::runtime_error("Hull contains an unused vertex");
    }
    if (vertices_.size()+faces_.size()!=edges.size()+2) throw std::runtime_error("Invalid hull boundary topology");
    for (const auto& [edge,uses] : edges) {
        if (uses.first!=2 || uses.second!=0) throw std::runtime_error("Hull boundary is not an oriented closed mesh");
        edges_.push_back(edge);
    }
}

}
