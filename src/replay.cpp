#include "emberfall/replay.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace ember {
namespace {

constexpr std::size_t byte_limit = 16'000'000;

void valid(const Command& c) {
    if (!finite(c.move) || c.move.y != 0 || std::abs(c.move.x)>1.000001f || std::abs(c.move.z)>1.000001f) {
        throw std::runtime_error("Replay movement outside bounds");
    }
}

void validate(const Replay& replay) {
    if (!replay.revision || replay.start.level >= Campaign::level_count || replay.start.complete ||
        replay.ticks.empty() || replay.ticks.size() > Replay::tick_limit) {
        throw std::runtime_error("Replay header outside bounds");
    }
}

}

Replay read_replay(const std::filesystem::path& path) {
    const auto bytes = read_bounded(path,byte_limit);
    if (bytes.empty() || bytes.back() != '\n') throw std::runtime_error("Truncated replay");
    std::istringstream in(bytes);
    in.imbue(std::locale::classic());
    std::string magic;
    unsigned version{};
    std::size_t count{};
    Replay replay;
    if (!(in >> magic >> version >> replay.revision >> replay.start.level >> replay.start.checkpoint >> count) ||
        magic != "ember-replay" || version != 1 || count == 0 || count > Replay::tick_limit) {
        throw std::runtime_error("Unsupported replay header");
    }
    replay.ticks.reserve(count);
    for (std::size_t i=0; i<count; ++i) {
        ReplayTick row;
        for (auto& command : row.commands) {
            in >> command.move.x >> command.move.z >> command.jump >> command.interact >> command.crouch;
            valid(command);
        }
        if (!(in >> row.state)) throw std::runtime_error("Malformed replay record " + std::to_string(i+1));
        replay.ticks.push_back(row);
    }
    if ((in >> std::ws, !in.eof())) throw std::runtime_error("Trailing replay records");
    validate(replay);
    return replay;
}

void write_replay(const std::filesystem::path& path,const Replay& replay) {
    validate(replay);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "ember-replay 1 " << replay.revision << ' ' << replay.start.level << ' ' << replay.start.checkpoint
        << ' ' << replay.ticks.size() << '\n';
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const auto& row : replay.ticks) {
        for (const auto& c : row.commands) {
            valid(c);
            out << c.move.x << ' ' << c.move.z << ' ' << c.jump << ' ' << c.interact << ' ' << c.crouch << ' ';
        }
        out << row.state << '\n';
    }
    const auto result = std::move(out).str();
    if (result.size() > byte_limit) throw std::runtime_error("Replay byte budget");
    write_atomic(path,result);
}

std::uint64_t check_replay(const Campaign& campaign,const Replay& replay) {
    validate(replay);
    if (campaign.revision() != replay.revision) throw std::runtime_error("Replay campaign revision mismatch");
    auto game = campaign.load(replay.start);
    for (std::size_t i=0; i<replay.ticks.size(); ++i) {
        if (game->completed()) throw std::runtime_error("Replay continues after level completion");
        const auto& record = replay.ticks[i];
        game->tick(record.commands);
        const auto observed = game->state_hash();
        if (observed != record.state) {
            throw std::runtime_error("Replay divergence at tick " + std::to_string(i+1) +
                ", recorded " + std::to_string(record.state) + ", observed " + std::to_string(observed));
        }
    }
    return game->state_hash();
}

}
