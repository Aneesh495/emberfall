#include "emberfall/animation.hpp"

#include <stdexcept>

namespace ember {
namespace {

constexpr std::size_t joint_limit = 128;

void validate(JointPose pose) {
    const auto q = pose.rotation;
    const float length2 = q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if (!finite(pose.translation) || !finite(pose.scale) ||
        std::min({pose.scale.x,pose.scale.y,pose.scale.z})<0.001f ||
        !std::isfinite(length2) || std::abs(length2-1)>0.002f) {
        throw std::runtime_error("Unsupported joint transform");
    }
}

JointPose mix(JointPose a,JointPose b,float weight) {
    return {lerp(a.translation,b.translation,weight),slerp(a.rotation,b.rotation,weight),lerp(a.scale,b.scale,weight)};
}

}

Clip::Clip(float duration,std::vector<Track> tracks) : duration_(duration), tracks_(std::move(tracks)) {
    if (!std::isfinite(duration_) || duration_<=0 || duration_>3600 || tracks_.size()>joint_limit) {
        throw std::runtime_error("Animation clip outside bounds");
    }
    for (const auto& track : tracks_) {
        if (track.size()>4096) throw std::runtime_error("Animation track key limit");
        float previous = -1;
        for (const auto& key : track) {
            if (!std::isfinite(key.second) || key.second<=previous || key.second<0 || key.second>duration_) {
                throw std::runtime_error("Animation keys must be strictly ordered");
            }
            validate(key.pose);
            previous = key.second;
        }
    }
}

std::vector<JointPose> Clip::sample(std::span<const Joint> joints,float second,bool loop) const {
    if (tracks_.size()>joints.size() || joints.size()>joint_limit || !std::isfinite(second)) {
        throw std::runtime_error("Animation sample outside clip");
    }
    if (loop) {
        second = std::fmod(second,duration_);
        if (second<0) second += duration_;
    } else second = std::clamp(second,0.0f,duration_);
    std::vector<JointPose> pose;
    pose.reserve(joints.size());
    for (std::size_t i=0; i<joints.size(); ++i) {
        if (i>=tracks_.size() || tracks_[i].empty()) {
            pose.push_back(joints[i].rest);
            continue;
        }
        const auto& track = tracks_[i];
        const auto next = std::ranges::upper_bound(track,second,{},&Keyframe::second);
        if (next == track.begin()) pose.push_back(next->pose);
        else if (next == track.end()) pose.push_back(track.back().pose);
        else {
            const auto prev = next-1;
            pose.push_back(mix(prev->pose,next->pose,(second-prev->second)/(next->second-prev->second)));
        }
    }
    return pose;
}

std::vector<JointPose> blend(std::span<const JointPose> a,std::span<const JointPose> b,float weight) {
    if (a.size()!=b.size() || a.size()>joint_limit || !std::isfinite(weight)) throw std::runtime_error("Pose blend mismatch");
    std::vector<JointPose> result;
    result.reserve(a.size());
    for (std::size_t i=0; i<a.size(); ++i) result.push_back(mix(a[i],b[i],std::clamp(weight,0.0f,1.0f)));
    return result;
}

Skeleton::Skeleton(std::vector<Joint> joints) : joints_(std::move(joints)) {
    if (joints_.empty() || joints_.size()>joint_limit) throw std::runtime_error("Skeleton joint limit");
    std::vector<JointPose> poses;
    for (std::size_t i=0; i<joints_.size(); ++i) {
        const auto& joint = joints_[i];
        if (joint.parent<-1 || joint.parent>=static_cast<int>(i)) throw std::runtime_error("Skeleton parent order");
        poses.push_back(joint.rest);
    }
    for (const auto& bind : world(poses)) {
        const auto inverse = inverse_affine(bind);
        if (!inverse) throw std::runtime_error("Singular inverse bind matrix");
        inverse_bind_.push_back(*inverse);
    }
}

std::vector<Mat4> Skeleton::world(std::span<const JointPose> pose) const {
    if (pose.size()!=joints_.size()) throw std::runtime_error("Joint palette size mismatch");
    std::vector<Mat4> matrices;
    matrices.reserve(pose.size());
    for (std::size_t i=0; i<pose.size(); ++i) {
        const auto& joint = joints_[i];
        const auto& local = pose[i];
        validate(local);
        auto matrix = transform_matrix(local.translation,local.rotation,local.scale);
        if (joint.parent>=0) matrix = matrices[static_cast<std::size_t>(joint.parent)]*matrix;
        matrices.push_back(matrix);
    }
    return matrices;
}

std::vector<Mat4> Skeleton::palette(std::span<const JointPose> pose) const {
    auto result = world(pose);
    for (std::size_t i=0; i<result.size(); ++i) result[i] = result[i]*inverse_bind_[i];
    return result;
}

std::vector<MeshVertex> deform(std::span<const WeightedVertex> vertices,std::span<const Mat4> palette) {
    if (vertices.size()>65'536 || palette.empty() || palette.size()>joint_limit) throw std::runtime_error("Skinning budget");
    std::vector<Mat4> normals;
    normals.reserve(palette.size());
    for (const auto& matrix : palette) {
        const auto inverse = inverse_affine(matrix);
        if (!inverse) throw std::runtime_error("Singular normal matrix");
        normals.push_back(*inverse);
    }
    std::vector<MeshVertex> out;
    out.reserve(vertices.size());
    for (const auto& source : vertices) {
        if (!finite(source.vertex.position) || !finite(source.vertex.normal)) throw std::runtime_error("Invalid skin vertex");
        Vec3 p, n;
        float sum = 0;
        for (std::size_t i=0; i<source.weights.size(); ++i) {
            const float weight = source.weights[i];
            if (!std::isfinite(weight) || weight<0 || weight>1) throw std::runtime_error("Invalid skin weight");
            if (weight==0) continue;
            const auto index = source.joints[i];
            if (index>=palette.size()) throw std::runtime_error("Skin joint outside palette");
            const auto& m = normals[index];
            const auto original = source.vertex.normal;
            p += transform_point(palette[index],source.vertex.position)*weight;
            n += Vec3{m(0,0)*original.x+m(1,0)*original.y+m(2,0)*original.z,
                      m(0,1)*original.x+m(1,1)*original.y+m(2,1)*original.z,
                      m(0,2)*original.x+m(1,2)*original.y+m(2,2)*original.z}*weight;
            sum += weight;
        }
        if (std::abs(sum-1)>0.002f) throw std::runtime_error("Skin weights must sum to one");
        out.push_back({p/sum,unit(n)});
    }
    return out;
}

}
