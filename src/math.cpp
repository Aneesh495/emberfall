#include "emberfall/math.hpp"

namespace ember {

Quat yaw_rotation(float radians) {
    return {0, std::sin(radians*0.5f), 0, std::cos(radians*0.5f)};
}

Quat slerp(Quat a, Quat b, float t) {
    float cosine = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (cosine < 0) {
        b = {-b.x,-b.y,-b.z,-b.w};
        cosine = -cosine;
    }
    float u = 1-t;
    float v = t;
    if (cosine < 0.9995f) {
        const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));
        const float reciprocal = 1/std::sin(angle);
        u = std::sin((1-t)*angle)*reciprocal;
        v = std::sin(t*angle)*reciprocal;
    }
    Quat q{a.x*u+b.x*v, a.y*u+b.y*v, a.z*u+b.z*v, a.w*u+b.w*v};
    const float norm = std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return norm > 1e-7f ? Quat{q.x/norm,q.y/norm,q.z/norm,q.w/norm} : Quat{};
}

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int c=0; c<4; ++c) {
        for (int r=0; r<4; ++r) {
            for (int i=0; i<4; ++i) result(r,c) += a(r,i)*b(i,c);
        }
    }
    return result;
}

Vec3 transform_point(const Mat4& m, Vec3 v) {
    return {
        m(0,0)*v.x + m(0,1)*v.y + m(0,2)*v.z + m(0,3),
        m(1,0)*v.x + m(1,1)*v.y + m(1,2)*v.z + m(1,3),
        m(2,0)*v.x + m(2,1)*v.y + m(2,2)*v.z + m(2,3)
    };
}

Mat4 transform_matrix(Vec3 p, Quat q, Vec3 s) {
    const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    const float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    const float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    Mat4 m = Mat4::identity();
    m(0,0)=(1-2*(yy+zz))*s.x; m(0,1)=2*(xy-wz)*s.y; m(0,2)=2*(xz+wy)*s.z;
    m(1,0)=2*(xy+wz)*s.x; m(1,1)=(1-2*(xx+zz))*s.y; m(1,2)=2*(yz-wx)*s.z;
    m(2,0)=2*(xz-wy)*s.x; m(2,1)=2*(yz+wx)*s.y; m(2,2)=(1-2*(xx+yy))*s.z;
    m(0,3)=p.x; m(1,3)=p.y; m(2,3)=p.z;
    return m;
}

std::optional<Mat4> inverse_affine(const Mat4& m) {
    const Vec3 a{m(0,0),m(1,0),m(2,0)};
    const Vec3 b{m(0,1),m(1,1),m(2,1)};
    const Vec3 c{m(0,2),m(1,2),m(2,2)};
    const Vec3 p{m(0,3),m(1,3),m(2,3)};
    const float determinant = dot(a,cross(b,c));
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-10f) return std::nullopt;
    const std::array rows{cross(b,c)/determinant,cross(c,a)/determinant,cross(a,b)/determinant};
    Mat4 inverse = Mat4::identity();
    for (int i=0; i<3; ++i) {
        const auto row = rows[static_cast<std::size_t>(i)];
        inverse(i,0)=row.x; inverse(i,1)=row.y; inverse(i,2)=row.z;
        inverse(i,3)=-dot(row,p);
    }
    return inverse;
}

Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 z = unit(eye-target);
    const Vec3 x = unit(cross(up,z));
    const Vec3 y = cross(z,x);
    Mat4 m = Mat4::identity();
    m(0,0)=x.x; m(0,1)=x.y; m(0,2)=x.z; m(0,3)=-dot(x,eye);
    m(1,0)=y.x; m(1,1)=y.y; m(1,2)=y.z; m(1,3)=-dot(y,eye);
    m(2,0)=z.x; m(2,1)=z.y; m(2,2)=z.z; m(2,3)=-dot(z,eye);
    return m;
}

Mat4 orthographic(float half_width,float half_height,float near_distance,float far_distance) {
    auto m = Mat4::identity();
    m(0,0) = 1/half_width;
    m(1,1) = -1/half_height;
    m(2,2) = 1/(near_distance-far_distance);
    m(2,3) = near_distance/(near_distance-far_distance);
    return m;
}

Mat4 perspective(float fov, float aspect, float near_distance, float far_distance) {
    const float cotangent = 1/std::tan(fov*0.5f);
    const float depth = near_distance - far_distance;
    Mat4 m;
    m(0,0)=cotangent/aspect;
    m(1,1)=-cotangent;
    m(2,2)=far_distance/depth;
    m(2,3)=far_distance*near_distance/depth;
    m(3,2)=-1;
    return m;
}

}
