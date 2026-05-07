#include "emberfall/campaign.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace ember {
namespace {

constexpr std::array<std::string_view,Campaign::level_count> names{
    "01-aperture.scene", "02-prism.scene", "03-meridian.scene"
};

std::istringstream checked_payload(const std::filesystem::path& path) {
    const auto raw = read_bounded(path,4096);
    const auto separator = raw.rfind('\n',raw.empty() ? 0 : raw.size()-2);
    if (raw.size() < 3 || raw.back() != '\n' || separator == std::string::npos) {
        throw std::runtime_error("Truncated " + path.string());
    }
    const auto payload = raw.substr(0,separator+1);
    const auto expected = std::to_string(fingerprint(payload));
    if (raw.substr(separator+1,raw.size()-separator-2) != expected) {
        throw std::runtime_error("Checksum mismatch in " + path.string());
    }
    std::istringstream in(payload);
    in.imbue(std::locale::classic());
    return in;
}

void store(const std::filesystem::path& path, std::string payload) {
    payload += std::to_string(fingerprint(payload)) + "\n";
    write_atomic(path,payload);
}

void strict(std::istream& in) {
    if (!in || (in >> std::ws, !in.eof())) throw std::runtime_error("Invalid save record");
}

bool valid(Settings s) {
    return std::isfinite(s.volume) && std::isfinite(s.exposure) &&
           s.volume >= 0 && s.volume <= 1 && s.exposure >= 0.4f && s.exposure <= 2.5f;
}

}

std::uint64_t fingerprint(std::string_view bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : bytes) hash = (hash ^ static_cast<unsigned char>(byte))*1099511628211ULL;
    return hash;
}

Campaign::Campaign(const std::filesystem::path& directory) {
    std::string source;
    for (std::size_t i=0; i<names.size(); ++i) {
        scenes_[i] = load_scene(directory / names[i]);
        source += write_scene(scenes_[i]);
    }
    revision_ = fingerprint(source);
}

void Campaign::validate(Progress progress) const {
    if (progress.level >= scenes_.size() ||
        (progress.complete && (progress.level != level_count-1 || progress.checkpoint))) {
        throw std::runtime_error("Save level outside campaign bounds");
    }
    if (!progress.checkpoint) return;
    const auto& nodes = scene(progress.level).nodes;
    const auto node = nodes.find(progress.checkpoint);
    if (node == nodes.end() || node->second.kind != Kind::checkpoint) {
        throw std::runtime_error("Save references an unknown checkpoint");
    }
}

std::unique_ptr<Game> Campaign::load(Progress progress) const {
    validate(progress);
    auto game = std::make_unique<Game>(scene(progress.level));
    game->restore_checkpoint(progress.checkpoint);
    return game;
}

Progress Campaign::read_progress(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) return {};
    auto in = checked_payload(path);
    std::string magic;
    unsigned version = 0;
    std::uint64_t scene_revision = 0;
    Progress save;
    in >> magic >> version >> scene_revision >> save.level >> save.checkpoint;
    if (version == 2) in >> save.complete;
    strict(in);
    if (magic != "ember-progress" || (version != 1 && version != 2)) {
        throw std::runtime_error("Unsupported progress schema");
    }
    if (scene_revision != revision_) throw std::runtime_error("Save belongs to another level revision");
    validate(save);
    return save;
}

void Campaign::save_progress(const std::filesystem::path& path, Progress progress) const {
    validate(progress);
    std::string payload = "ember-progress 2 " + std::to_string(revision_) + " " + std::to_string(progress.level) +
        " " + std::to_string(progress.checkpoint) + " " + (progress.complete ? "1\n" : "0\n");
    store(path,std::move(payload));
}

Settings read_settings(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    auto in = checked_payload(path);
    std::string magic;
    unsigned version = 0;
    Settings settings;
    in >> magic >> version >> settings.volume >> settings.exposure >> settings.antialias
       >> settings.fullscreen >> settings.high_contrast;
    strict(in);
    if (magic != "ember-settings" || version != 1 || !valid(settings)) {
        throw std::runtime_error("Unsupported or invalid settings record");
    }
    return settings;
}

void save_settings(const std::filesystem::path& path, Settings settings) {
    if (!valid(settings)) throw std::runtime_error("Settings outside bounds");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "ember-settings 1 " << settings.volume << ' ' << settings.exposure << ' ' << settings.antialias
        << ' ' << settings.fullscreen << ' ' << settings.high_contrast << '\n';
    store(path,out.str());
}

}
