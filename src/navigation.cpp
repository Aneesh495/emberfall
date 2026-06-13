#include "emberfall/navigation.hpp"

#include <limits>
#include <queue>

namespace ember {
namespace {

constexpr float cell_size = 0.5f;
constexpr int margin = 8;
constexpr int axis_limit = 128;
constexpr auto missing = std::numeric_limits<std::uint32_t>::max();
constexpr Vec3 half{0.28f,0.725f,0.28f};

struct Cell {
    int x{}, z{};
    friend bool operator==(Cell,Cell) = default;
};

Cell cell(Vec3 point) {
    return {static_cast<int>(std::round(point.x/cell_size)),static_cast<int>(std::round(point.z/cell_size))};
}

std::optional<float> floor_at(const CollisionSpace& space,float x,float z,float elevation) {
    constexpr float raise = 0.26f;
    constexpr float sweep = 0.57f;
    const Vec3 center{x,elevation+half.y+raise,z};
    if (!space.clear(center,half)) return {};
    const auto ground = space.sweep(center,half,{0,-sweep,0});
    if (!ground.surface || ground.normal.y < 0.85f) return {};
    return elevation+raise-ground.fraction*sweep+0.003f;
}

}

Route find_route(const CollisionSpace& space,Vec3 start,Vec3 goal) {
    Route result;
    if (!finite(start) || !finite(goal) || length(start-goal)>60 || std::abs(start.y-goal.y)>0.45f ||
        std::max({std::abs(start.x),std::abs(start.z),std::abs(goal.x),std::abs(goal.z)})>4096) return result;
    const auto from = cell(start), to = cell(goal);
    const Cell low{std::min(from.x,to.x)-margin,std::min(from.z,to.z)-margin};
    const int width = std::max(from.x,to.x)-low.x+margin+1;
    const int height = std::max(from.z,to.z)-low.z+margin+1;
    if (width>axis_limit || height>axis_limit) return result;
    const auto encode = [&](Cell position) {
        return static_cast<std::uint32_t>((position.z-low.z)*width+(position.x-low.x));
    };
    const auto decode = [&](std::uint32_t id) {
        const auto index = static_cast<int>(id);
        return Cell{low.x+index%width,low.z+index/width};
    };
    const auto inside = [&](Cell position) {
        return position.x>=low.x && position.x<low.x+width && position.z>=low.z && position.z<low.z+height;
    };
    struct Record {
        std::uint32_t steps{missing}, previous{missing};
        float y{};
        bool sampled{}, walkable{};
    };
    std::vector<Record> records(static_cast<std::size_t>(width*height));
    const auto elevation = [&](Cell position) -> Record& {
        auto& entry = records[encode(position)];
        if (entry.sampled) return entry;
        entry.sampled = true;
        const auto floor = floor_at(space,static_cast<float>(position.x)*cell_size,
                                   static_cast<float>(position.z)*cell_size,start.y);
        if (floor) {
            entry.walkable = true;
            entry.y = *floor;
        }
        return entry;
    };
    const auto first = encode(from), last = encode(to);
    if (!elevation(from).walkable || !elevation(to).walkable) return result;
    using Candidate = std::pair<std::uint32_t,std::uint32_t>;
    std::priority_queue<Candidate,std::vector<Candidate>,std::greater<>> queue;
    const auto heuristic = [&](Cell position) {
        return static_cast<std::uint32_t>(std::abs(position.x-to.x)+std::abs(position.z-to.z));
    };
    records[first].steps = 0;
    queue.emplace(heuristic(from),first);
    constexpr std::array directions{Cell{1,0},Cell{-1,0},Cell{0,1},Cell{0,-1}};
    while (!queue.empty()) {
        const auto [cost,current] = queue.top();
        queue.pop();
        const auto position = decode(current);
        const auto& here = records[current];
        if (cost != here.steps+heuristic(position)) continue;
        ++result.visited;
        if (current == last) break;
        for (const auto direction : directions) {
            const Cell adjacent{position.x+direction.x,position.z+direction.z};
            if (!inside(adjacent)) continue;
            const auto id = encode(adjacent);
            auto& next = elevation(adjacent);
            if (!next.walkable || here.steps+1 >= next.steps || std::abs(here.y-next.y)>0.21f) continue;
            const Vec3 origin{static_cast<float>(position.x)*cell_size,
                std::max(here.y,next.y)+half.y,static_cast<float>(position.z)*cell_size};
            const Vec3 step{static_cast<float>(direction.x)*cell_size,0,static_cast<float>(direction.z)*cell_size};
            if (space.sweep(origin,half,step).surface ||
                !floor_at(space,origin.x+step.x*0.5f,origin.z+step.z*0.5f,start.y)) continue;
            next.steps = here.steps+1;
            next.previous = current;
            queue.emplace(next.steps+heuristic(adjacent),id);
        }
    }
    if (records[last].steps == missing) return result;
    auto cursor = last;
    while (cursor != first) {
        const auto position = decode(cursor);
        result.waypoints.push_back({static_cast<float>(position.x)*cell_size,records[cursor].y,
                                   static_cast<float>(position.z)*cell_size});
        cursor = records[cursor].previous;
    }
    std::ranges::reverse(result.waypoints);
    return result;
}

}
