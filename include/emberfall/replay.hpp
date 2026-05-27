#pragma once

#include "emberfall/campaign.hpp"

namespace ember {

struct ReplayTick {
    std::array<Command,2> commands{};
    std::uint64_t state{};
};

struct Replay {
    static constexpr std::size_t tick_limit = 108000;
    std::uint64_t revision{};
    Progress start;
    std::vector<ReplayTick> ticks;
};

Replay read_replay(const std::filesystem::path& path);
void write_replay(const std::filesystem::path& path,const Replay& replay);
std::uint64_t check_replay(const Campaign& campaign,const Replay& replay);

}
