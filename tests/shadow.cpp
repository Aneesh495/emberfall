#include "emberfall/shadow.hpp"

#include <iostream>
#include <stdexcept>

using namespace ember;

float w(const Mat4& m,Vec3 p) { return m(3,0)*p.x+m(3,1)*p.y+m(3,2)*p.z+m(3,3); }

int main() {
    const Vec3 eye{2,9,16}, forward = unit(-eye), up{0,1,0};
    const ShadowCamera camera{eye,forward,up,0.88f,1.77f,0.1f,260};
    const auto right = unit(cross(forward,up));
    const auto vertical = cross(right,forward);
    const LightSphere lamp{{4,1,-3},3.4f};
    const auto shadows = shadow_views(camera,{-0.45f,0.82f,0.24f},lamp);
    if (shadows.count!=9 || shadows.cascade_ends.back()!=180) throw std::runtime_error("Shadow atlas contract");
    float start = camera.near_plane;
    for (std::size_t i=0; i<shadows.cascade_ends.size(); ++i) {
        const auto end = shadows.cascade_ends[i];
        for (float z : {start,end}) for (float x : {-1.f,1.f}) for (float y : {-1.f,1.f}) {
            const auto h = std::tan(camera.vertical_fov*0.5f)*z;
            const auto corner = eye+forward*z+right*(x*h*camera.aspect)+vertical*(y*h);
            const auto clip = transform_point(shadows.projections[i],corner);
            if (std::max(std::abs(clip.x),std::abs(clip.y))>1 || clip.z<0 || clip.z>1) {
                throw std::runtime_error("Cascade clipped an assigned receiver");
            }
        }
        start = end-(end-start)*0.12f;
    }
    const std::array<Vec3,6> directions{{
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
    }};
    for (std::size_t face=0; face<directions.size(); ++face) {
        const auto point = lamp.center+directions[face];
        const auto& matrix = shadows.projections[ShadowViews::cascade_count+face];
        const auto clip = transform_point(matrix,point);
        if (point_shadow_face(point-lamp.center)!=face ||
            std::max(std::abs(clip.x),std::abs(clip.y))>1e-5f || clip.z<0 || clip.z>w(matrix,point)) {
            throw std::runtime_error("Point shadow dominant face mismatch");
        }
    }
    std::cout << "cascade_receivers=24 point_faces=" << directions.size() << '\n';
}
