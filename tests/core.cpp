#include "emberfall/math.hpp"

#include <cstdlib>
#include <iostream>
#include <numbers>
#include <source_location>

void expect(bool condition, std::source_location where = std::source_location::current()) {
    if (!condition) {
        std::cerr << where.file_name() << ':' << where.line() << " check failed\n";
        std::exit(EXIT_FAILURE);
    }
}

int main() {
    using namespace ember;
    constexpr float half_pi = std::numbers::pi_v<float>/2;
    const Mat4 m = transform_matrix({4,-2,6}, yaw_rotation(half_pi), {-2,3,4});
    const Vec3 actual = transform_point(m, {1,2,3});
    expect(length(actual - Vec3{16,4,8}) < 1e-4f);
    const auto inverse = inverse_affine(m);
    expect(inverse.has_value());
    expect(length(transform_point(*inverse, actual) - Vec3{1,2,3}) < 1e-5f);
    expect(!inverse_affine(transform_matrix({}, {}, {0,1,1})));
    const auto q = slerp({}, yaw_rotation(half_pi), 0.5f);
    const float diagonal = std::sqrt(0.5f);
    expect(length(transform_point(transform_matrix({}, q), {0,0,-1}) - Vec3{-diagonal,0,-diagonal}) < 1e-5f);
    const auto proj = perspective(half_pi, 1, 1, 100);
    expect(std::abs(transform_point(proj, {0,0,-1}).z) < 1e-5f);
    expect(std::abs(transform_point(proj, {0,0,-100}).z/100 - 1) < 1e-5f);
}
