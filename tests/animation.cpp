#include "emberfall/animation.hpp"

#include <numbers>
#include <stdexcept>

int main() {
    using namespace ember;
    const Skeleton rig({{-1,{}},{0,{{0,2,0},{},{1,1,1}}}});
    const JointPose neutral{{0,2,0},{},{1,1,1}};
    const JointPose turned{{0,2,0},yaw_rotation(std::numbers::pi_v<float>/2),{1,1,1}};
    const Clip clip(2,{{},{{0,neutral},{2,turned}}});
    const auto state = clip.sample(rig.joints(),1,false);
    const WeightedVertex vertex{{{0,2,-1},{0,0,-1}},{0,1,0,0},{0.25f,0.75f,0,0}};
    const auto transformed = deform(std::span(&vertex,1),rig.palette(state)).front();
    const float diagonal = std::sqrt(0.5f);
    const Vec3 expected{-diagonal*0.75f,2,-0.25f-diagonal*0.75f};
    if (length(transformed.position-expected)>1e-5f) throw std::runtime_error("Independent skin vertex mismatch");
    if (std::abs(length(transformed.normal)-1)>1e-5f) throw std::runtime_error("Skin normal was not normalized");
    const auto loop = clip.sample(rig.joints(),3,true);
    if (loop[1].rotation != state[1].rotation) throw std::runtime_error("Loop phase mismatch");
    const auto static_pose = clip.sample(rig.joints(),0,false);
    if (length(deform(std::span(&vertex,1),rig.palette(static_pose)).front().position-vertex.vertex.position)>1e-5f) {
        throw std::runtime_error("Inverse bind altered the rest vertex");
    }

    const Skeleton imported({{-1,{}},{0,{{0,2,0},{},{1,1,1}}}},
                            {Mat4::identity(),transform_matrix({0,-1,0})});
    std::vector<JointChannels> channels(2);
    channels[0].translation.keys = {{0,{}},{2,{2,0,0}}};
    channels[1].rotation.keys = {{0,{}},{2,turned.rotation}};
    channels[1].rotation.stepped = true;
    channels[1].scale.keys = {{0,{1,1,1}},{1,{2,1,1}},{2,{1,1,1}}};
    const Clip independent(2,std::move(channels));
    const WeightedVertex textured{{{1,1,0},{0,0,1},{0.25f,0.75f},{1,0,0,-1}},
                                  {1,0,0,0},{1,0,0,0}};
    const auto midpoint = independent.sample(imported.joints(),1,false);
    const auto endpoint = independent.sample(imported.joints(),2,false);
    const auto skin = [&](const auto& pose) {
        return deform(std::span(&textured,1),imported.palette(pose)).front();
    };
    const auto middle = skin(midpoint);
    const auto end = skin(endpoint);
    if (length(middle.position-Vec3{3,2,0})>1e-5f || length(end.position-Vec3{2,2,-1})>1e-5f ||
        length(end.normal-Vec3{1,0,0})>1e-5f || middle.uv!=textured.vertex.uv ||
        middle.tangent!=textured.vertex.tangent || std::abs(end.tangent[2]+1)>1e-5f || end.tangent[3]!=-1) {
        throw std::runtime_error("Authored bind, independent channels or tangent basis mismatch");
    }
}
