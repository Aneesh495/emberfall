#pragma once

#include "emberfall/scene.hpp"

#include <optional>
#include <vector>

namespace ember {

struct ResolvedPrefab {
    Scene scene;
    std::vector<std::string> sources;
};

ResolvedPrefab load_prefab(const std::filesystem::path& file);
void derive_prefab(const std::filesystem::path& file,std::string base);
void change_prefab(const std::filesystem::path& file,SceneId id,std::string field,std::optional<std::string> value);

}
