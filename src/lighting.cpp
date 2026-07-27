#include "emberfall/lighting.hpp"

#include <limits>
#include <stdexcept>

namespace ember {
namespace {

struct ScreenBounds {
    float left{}, top{}, right{}, bottom{};
};

std::optional<ScreenBounds> project(const Mat4& view_projection,const LightSphere& light) {
    constexpr auto inf = std::numeric_limits<float>::infinity();
    ScreenBounds bounds{inf,inf,-inf,-inf};
    const auto& p = light.center;
    bool any_front = false, any_behind = false;
    for (const float x : {p.x-light.radius,p.x+light.radius}) {
        for (const float y : {p.y-light.radius,p.y+light.radius}) {
            for (const float z : {p.z-light.radius,p.z+light.radius}) {
                const auto clip = transform_point(view_projection,{x,y,z});
                const float w = view_projection(3,0)*x + view_projection(3,1)*y +
                                view_projection(3,2)*z + view_projection(3,3);
                if (!std::isfinite(w) || !std::isfinite(clip.x) || !std::isfinite(clip.y)) {
                    return ScreenBounds{-1,-1,1,1};
                }
                if (w<=1e-5f) {
                    any_behind = true;
                    continue;
                }
                any_front = true;
                const float u = clip.x/w, v = clip.y/w;
                if (!std::isfinite(u) || !std::isfinite(v)) return ScreenBounds{-1,-1,1,1};
                bounds.left = std::min(bounds.left,u);
                bounds.top = std::min(bounds.top,v);
                bounds.right = std::max(bounds.right,u);
                bounds.bottom = std::max(bounds.bottom,v);
            }
        }
    }
    if (!any_front) return {};
    // Corner extrema are conservative only while the box stays in front of the eye.
    if (any_behind) return ScreenBounds{-1,-1,1,1};
    if (bounds.right < -1 || bounds.left > 1 || bounds.bottom < -1 || bounds.top > 1) return {};
    return bounds;
}

std::uint32_t tile(float normalized,std::uint32_t pixels,std::uint32_t tiles,float padding) {
    const auto pixel = (std::clamp(normalized,-1.0f,1.0f)*0.5f+0.5f)*static_cast<float>(pixels) + padding;
    return std::min(tiles-1,static_cast<std::uint32_t>(std::max(pixel,0.0f))/TileLights::tile_size);
}

}

void TileLights::build(const Mat4& view_projection,std::uint32_t width,std::uint32_t height,
                       std::span<const LightSphere> lights) {
    if (!width || !height || width>max_extent || height>max_extent || lights.size()>light_limit ||
        !std::ranges::all_of(view_projection.data, [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("Invalid light grid parameters");
    }
    columns_ = (width+tile_size-1)/tile_size;
    rows_ = (height+tile_size-1)/tile_size;
    masks_.assign(static_cast<std::size_t>(columns_)*rows_,{});
    entries_ = 0;
    for (std::size_t i=0; i<lights.size(); ++i) {
        const auto& light = lights[i];
        if (!finite(light.center) || !std::isfinite(light.radius) || light.radius<=0) {
            throw std::runtime_error("Invalid local light sphere");
        }
        const auto bounds = project(view_projection,light);
        if (!bounds) continue;
        const auto x0 = tile(bounds->left,width,columns_,-1), x1 = tile(bounds->right,width,columns_,1);
        const auto y0 = tile(bounds->top,height,rows_,-1), y1 = tile(bounds->bottom,height,rows_,1);
        const auto bit = std::uint32_t{1} << (i%32);
        for (std::uint32_t y=y0; y<=y1; ++y) {
            const auto row = static_cast<std::size_t>(y)*columns_;
            for (std::uint32_t x=x0; x<=x1; ++x) masks_[row+x][i/32] |= bit;
        }
        entries_ += (x1-x0+1)*(y1-y0+1);
    }
}

}
