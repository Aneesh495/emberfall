#pragma once

#include "emberfall/math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ember {

struct LightSphere {
    Vec3 center;
    float radius{};
};

class TileLights {
public:
    static constexpr std::uint32_t tile_size = 32;
    static constexpr std::uint32_t max_extent = 4096;
    static constexpr std::size_t light_limit = 128;
    using Mask = std::array<std::uint32_t,light_limit/32>;
    static constexpr std::size_t max_tiles = (max_extent/tile_size)*(max_extent/tile_size);

    void build(const Mat4& view_projection,std::uint32_t width,std::uint32_t height,
               std::span<const LightSphere> lights);
    std::span<const Mask> masks() const { return masks_; }
    std::uint32_t columns() const { return columns_; }
    std::uint32_t rows() const { return rows_; }
    std::uint32_t entries() const { return entries_; }

private:
    std::vector<Mask> masks_;
    std::uint32_t columns_{}, rows_{}, entries_{};
};

}
