#include "emberfall/shadow.hpp"

#include <numbers>
#include <stdexcept>

namespace ember {
namespace {

constexpr float pi = std::numbers::pi_v<float>;
constexpr std::array<Vec3,6> point_directions{{
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
}};
constexpr std::array<Vec3,6> point_up{{
    {0,1,0},{0,1,0},{0,0,1},{0,0,-1},{0,1,0},{0,1,0}
}};

Mat4 fit_directional(const ShadowCamera& camera,Vec3 direction,float first,float last) {
    const auto tan_half = std::tan(camera.vertical_fov*0.5f);
    const auto center_distance = (first+last)*0.5f;
    const auto side = last*tan_half;
    const auto width = side*camera.aspect;
    const auto thickness = (last-first)*0.5f;
    const auto radius = std::ceil(std::sqrt(width*width+side*side+thickness*thickness)*16)/16 + 0.125f;
    auto center = camera.eye+camera.forward*center_distance;
    const auto world_up = std::abs(direction.y)<0.94f ? Vec3{0,1,0} : Vec3{0,0,1};
    const auto right = unit(cross(world_up,direction));
    const auto up = cross(direction,right);
    const auto texel = 2*radius/static_cast<float>(ShadowViews::tile_pixels);
    const auto snap = [texel](float coordinate) { return std::round(coordinate/texel)*texel-coordinate; };
    center += right*snap(dot(center,right)) + up*snap(dot(center,up));

    constexpr float caster_margin = 32;
    const auto eye = center+direction*(radius+caster_margin);
    return orthographic(radius,radius,0.1f,2*radius+2*caster_margin)*look_at(eye,center,world_up);
}

}

std::size_t point_shadow_face(Vec3 from_light) {
    const auto [x,y,z] = from_light;
    if (std::abs(x)>=std::abs(y) && std::abs(x)>=std::abs(z)) return x>=0 ? 0U : 1U;
    if (std::abs(y)>=std::abs(z)) return y>=0 ? 2U : 3U;
    return z>=0 ? 4U : 5U;
}

ShadowViews shadow_views(const ShadowCamera& camera,Vec3 light_direction,std::optional<LightSphere> point) {
    if (!finite(camera.eye) || !finite(camera.forward) || !finite(camera.up) ||
        std::abs(dot(camera.forward,camera.forward)-1)>1e-4f ||
        !std::isfinite(camera.aspect) || !std::isfinite(camera.vertical_fov) || !std::isfinite(camera.near_plane) ||
        !std::isfinite(camera.far_plane) || camera.aspect<=0 || camera.aspect>4096 ||
        camera.vertical_fov<=0.05f || camera.vertical_fov>=pi-0.05f || camera.near_plane<=0 ||
        camera.far_plane<=camera.near_plane || !finite(light_direction) || length(light_direction)<0.001f) {
        throw std::runtime_error("Invalid shadow camera");
    }
    ShadowViews result;
    const auto direction = unit(light_direction);
    const auto far = std::min(camera.far_plane,180.0f);
    if (far<=camera.near_plane) throw std::runtime_error("Shadow distance is shorter than camera near plane");
    const auto range = far-camera.near_plane;
    result.cascade_ends = {camera.near_plane+range*0.12f,camera.near_plane+range*0.34f,far};
    auto start = camera.near_plane;
    for (std::size_t i=0; i<result.cascade_ends.size(); ++i) {
        const auto end = result.cascade_ends[i];
        result.projections[i] = fit_directional(camera,direction,start,end);
        start = end-(end-start)*0.12f;
    }
    if (point) {
        if (!finite(point->center) || !std::isfinite(point->radius) || point->radius<=0.06f) {
            throw std::runtime_error("Invalid point light shadow extent");
        }
        const auto projection = perspective(pi*0.5f,1,0.045f,point->radius);
        for (std::size_t face=0; face<point_directions.size(); ++face) {
            const auto center = point->center;
            result.projections[result.count++] = projection*look_at(center,center+point_directions[face],point_up[face]);
        }
    }
    return result;
}

}
