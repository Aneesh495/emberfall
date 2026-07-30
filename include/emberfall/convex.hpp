#pragma once

#include "emberfall/math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ember {

class ConvexHull {
public:
    using Face = std::array<std::uint8_t,3>;
    using Edge = std::array<std::uint8_t,2>;
    static constexpr std::size_t vertex_limit = 64;
    static constexpr std::size_t face_limit = 124;

    ConvexHull(std::vector<Vec3> vertices,std::vector<Face> faces);
    std::span<const Vec3> vertices() const { return vertices_; }
    std::span<const Face> faces() const { return faces_; }
    std::span<const Edge> edges() const { return edges_; }

private:
    std::vector<Vec3> vertices_;
    std::vector<Face> faces_;
    std::vector<Edge> edges_;
};

}
