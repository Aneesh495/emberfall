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
}
