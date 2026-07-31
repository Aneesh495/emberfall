#pragma once

#include "emberfall/lighting.hpp"

namespace ember {

struct ShadowCamera {
    Vec3 eye, forward, up;
    float vertical_fov{}, aspect{}, near_plane{}, far_plane{};
};

struct ShadowViews {
    static constexpr std::size_t cascade_count = 3;
    static constexpr std::size_t point_face_count = 6;
    static constexpr std::size_t limit = cascade_count+point_face_count;
    static constexpr std::uint32_t atlas_columns = 3;
    static constexpr std::uint32_t tile_pixels = 768;
    static constexpr std::uint32_t atlas_pixels = atlas_columns*tile_pixels;

    std::array<Mat4,limit> projections{};
    std::array<float,cascade_count> cascade_ends{};
    std::size_t count{cascade_count};
};

ShadowViews shadow_views(const ShadowCamera& camera,Vec3 light_direction,std::optional<LightSphere> point = {});
std::size_t point_shadow_face(Vec3 from_light);

}
