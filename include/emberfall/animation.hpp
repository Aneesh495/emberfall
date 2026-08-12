#pragma once

#include "emberfall/math.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ember {

struct JointPose {
    Vec3 translation;
    Quat rotation;
    Vec3 scale{1,1,1};
};

struct Joint {
    int parent{-1};
    JointPose rest;
};

struct Keyframe {
    float second{};
    JointPose pose;
};

using Track = std::vector<Keyframe>;

class Clip {
public:
    Clip(float duration,std::vector<Track> tracks);
    std::vector<JointPose> sample(std::span<const Joint> joints,float second,bool loop) const;
private:
    float duration_{};
    std::vector<Track> tracks_;
};

std::vector<JointPose> blend(std::span<const JointPose> first,std::span<const JointPose> second,float weight);

class Skeleton {
public:
    explicit Skeleton(std::vector<Joint> joints);
    std::span<const Joint> joints() const { return joints_; }
    std::vector<Mat4> palette(std::span<const JointPose> pose) const;
private:
    std::vector<Mat4> world(std::span<const JointPose> pose) const;
    std::vector<Joint> joints_;
    std::vector<Mat4> inverse_bind_;
};

struct MeshVertex {
    Vec3 position, normal;
    std::array<float,2> uv{};
    std::array<float,4> tangent{};
};

struct WeightedVertex {
    MeshVertex vertex;
    std::array<std::uint8_t,4> joints{};
    std::array<float,4> weights{};
};

std::vector<MeshVertex> deform(std::span<const WeightedVertex> vertices,std::span<const Mat4> palette);

struct SkinBatch {
    std::vector<MeshVertex> vertices;
    Mat4 model;
    Vec3 color;
};

}
