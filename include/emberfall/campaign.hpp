#pragma once

#include "emberfall/game.hpp"

#include <memory>

namespace ember {

struct Progress {
    unsigned level{};
    SceneId checkpoint{};
    bool complete{};
    friend bool operator==(Progress, Progress) = default;
};

struct Settings {
    float volume{0.65f}, exposure{1};
    bool antialias{true}, fullscreen{}, high_contrast{};
    friend bool operator==(Settings, Settings) = default;
};

class Campaign {
public:
    static constexpr std::size_t level_count = 3;
    explicit Campaign(const std::filesystem::path& directory);

    const Scene& scene(std::size_t index) const { return *scenes_.at(index); }
    std::unique_ptr<Game> load(Progress progress) const;
    Progress read_progress(const std::filesystem::path& path) const;
    void save_progress(const std::filesystem::path& path, Progress progress) const;
    std::uint64_t revision() const { return revision_; }
private:
    void validate(Progress progress) const;
    std::array<std::shared_ptr<const Scene>,level_count> scenes_;
    std::uint64_t revision_{};
};

std::uint64_t fingerprint(std::string_view bytes);
Settings read_settings(const std::filesystem::path& file);
void save_settings(const std::filesystem::path& file, Settings settings);

}
