#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace ember {

struct Vec3 {
    float x{}, y{}, z{};

    friend constexpr bool operator==(Vec3, Vec3) = default;
    friend constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
    friend constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
    friend constexpr Vec3 operator/(Vec3 v, float s) { return v * (1.0f / s); }
    constexpr Vec3& operator+=(Vec3 v) { return *this = *this + v; }
};

constexpr float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline float length(Vec3 v) { return std::sqrt(dot(v,v)); }
inline Vec3 unit(Vec3 v) {
    const float size = length(v);
    return size > 1e-7f ? v / size : Vec3{};
}
inline bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a)*t; }

struct Quat {
    float x{}, y{}, z{}, w{1};
    friend constexpr bool operator==(Quat, Quat) = default;
};

Quat normalized(Quat q);
Quat operator*(Quat a,Quat b);
Vec3 rotate(Quat q,Vec3 value);
Quat integrate_rotation(Quat rotation,Vec3 world_omega,float seconds);
Quat yaw_rotation(float radians);
Quat slerp(Quat a, Quat b, float t);

// Column-major layout matches GLSL and keeps affine shear during reparenting.
struct Mat4 {
    std::array<float, 16> data{};
    constexpr float operator()(int row, int column) const {
        return data[static_cast<std::size_t>(column*4 + row)];
    }
    constexpr float& operator()(int row, int column) {
        return data[static_cast<std::size_t>(column*4 + row)];
    }
    static constexpr Mat4 identity() {
        Mat4 m;
        for (int i=0; i<4; ++i) m(i,i) = 1;
        return m;
    }
};

Mat4 operator*(const Mat4& a, const Mat4& b);
Vec3 transform_point(const Mat4& matrix, Vec3 point);
Mat4 transform_matrix(Vec3 position, Quat rotation = {}, Vec3 scale = {1,1,1});
std::optional<Mat4> inverse_affine(const Mat4& m);
Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up);
Mat4 orthographic(float half_width,float half_height,float near_distance,float far_distance);
Mat4 perspective(float vertical_radians, float aspect, float near_distance, float far_distance);

}
