#pragma once

#include "platform.hpp"

namespace ember::native {

enum class InspectorPage : unsigned { closed, frame, physics, assets };

struct AssetLinks {
    struct Model {
        std::string source, guid;
        std::vector<std::string> inputs;
    };
    std::map<std::string,Model,std::less<>> models;
    std::vector<std::string> placed_prefabs;
};

AssetLinks load_asset_links(const std::filesystem::path& scene);
void paint_frame_inspector(Canvas& canvas,const Renderer& renderer,std::size_t pass_index);
std::size_t paint_collider_overlay(Canvas& canvas,const Game& game,const View& view,float aspect);
void paint_physics_inspector(Canvas& canvas,const Game* game,std::size_t drawn);
void paint_asset_inspector(Canvas& canvas,const AssetLinks& links,std::string_view name,const MeshAsset* mesh);

}
