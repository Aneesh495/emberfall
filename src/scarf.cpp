#include "emberfall/game.hpp"

#include <numbers>

namespace ember {
namespace {

JointPose scarf_pose(Vec3 position,float angle) {
    return {position,{std::sin(angle*0.5f),0,0,std::cos(angle*0.5f)},{1,1,1}};
}

const Skeleton& skeleton() {
    static const Skeleton rig({
        {-1,scarf_pose({},0)}, {0,scarf_pose({0,0.96f,0.19f},0)}, {1,scarf_pose({0,-0.28f,0.11f},0)}
    });
    return rig;
}

const Clip& cloth() {
    static const Clip clip(1,{
        {},
        {{0,scarf_pose({0,0.96f,0.19f},-0.15f)},{0.5f,scarf_pose({0,0.96f,0.19f},-0.55f)},
         {1,scarf_pose({0,0.96f,0.19f},-0.15f)}},
        {{0,scarf_pose({0,-0.28f,0.11f},0.18f)},{0.5f,scarf_pose({0,-0.28f,0.11f},0.05f)},
         {1,scarf_pose({0,-0.28f,0.11f},0.18f)}}
    });
    return clip;
}

const std::array<WeightedVertex,24>& scarf() {
    static const auto vertices = [] {
        std::array<WeightedVertex,24> result;
        std::size_t offset = 0;
        for (unsigned row=0; row<4; ++row) {
            const auto make = [](unsigned r,float side) {
                const auto t = static_cast<float>(r)*0.25f;
                return WeightedVertex{{{side*(0.20f-t*0.025f),0.96f-t*0.52f,0.187f+t*0.20f},{0,0.1f,1}},
                                      {1,2,0,0},{1-t,t,0,0}};
            };
            const auto a = make(row,-1), b = make(row,1), c = make(row+1,1), d = make(row+1,-1);
            for (auto vertex : {a,b,c,a,c,d}) result[offset++] = vertex;
        }
        return result;
    }();
    return vertices;
}

}

std::vector<SkinBatch> Game::skin(float alpha,std::optional<Vec3> remote_first) const {
    std::vector<SkinBatch> batches;
    batches.reserve(players_.size());
    const auto& rig = skeleton();
    for (std::size_t i=0; i<players_.size(); ++i) {
        const auto& player_data = player(i);
        const auto& body = *world_.get<BodyPose>(players_[i]);
        const auto movement = length(Vec3{player_data.body.velocity.x,0,player_data.body.velocity.z});
        const auto flapping = cloth().sample(rig.joints(),player_data.stride/(2*std::numbers::pi_v<float>),true);
        const auto rest = std::array{rig.joints()[0].rest,rig.joints()[1].rest,rig.joints()[2].rest};
        const auto pose = blend(rest,flapping,std::clamp(movement*0.31f,0.0f,1.0f));
        const auto palette = rig.palette(pose);
        const auto position = i == 0 && remote_first ? *remote_first : lerp(body.previous,body.position,alpha);
        batches.push_back({deform(scarf(),palette),transform_matrix(position,yaw_rotation(player_data.facing),
                          {1,player_data.body.height/1.45f,1}),
                          i == 0 ? Vec3{0.63f,0.18f,0.06f} : Vec3{0.08f,0.48f,0.56f}});
    }
    return batches;
}

}
