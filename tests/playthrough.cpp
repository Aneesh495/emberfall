#include "emberfall/replay.hpp"

#include <iostream>
#include <stdexcept>

using namespace ember;

class RecordedGame : public Game {
public:
    RecordedGame(const Scene& scene, Replay* tape) : Game(scene), tape_(tape) {}
    void tick(const std::array<Command,2>& commands) {
        if (completed()) return;
        Game::tick(commands);
        if (tape_) tape_->ticks.push_back({commands,state_hash()});
    }
private:
    Replay* tape_;
};

void wait(RecordedGame& game, unsigned ticks) {
    for (unsigned i=0; i<ticks; ++i) game.tick({});
}

void go(RecordedGame& game, std::size_t player, Vec3 target) {
    for (int i=0; i<480; ++i) {
        auto movement = target-game.player(player).body.feet;
        movement.y = 0;
        if (length(movement) < 0.09f || game.completed()) return;
        std::array<Command,2> commands{};
        commands.at(player).move = unit(movement);
        game.tick(commands);
    }
    const auto p = game.player(player).body.feet;
    throw std::runtime_error("Player " + std::to_string(player) + " blocked at " +
                             std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) +
                             " moving to " + std::to_string(target.x) + ", " + std::to_string(target.z));
}

std::uint64_t exercise(const Scene& scene, unsigned level, Replay* tape = nullptr) {
    RecordedGame game(scene,tape);
    if (level == 0) {
        go(game,0,{-2,0,3});
        go(game,1,{2,0,3});
        wait(game,40);
        go(game,0,{0,0,1.4f});
        go(game,0,{0,0,-9});
        if (game.checkpoint() != 12) throw std::runtime_error("Expected transit checkpoint");
    } else if (level == 1) {
        go(game,1,{2,0,6.8f});
        wait(game,40);
        go(game,0,{-1.3f,0,3.2f});
        go(game,0,{-1.3f,0,2.2f});
        std::array<Command,2> commands{};
        commands[0].interact = true;
        game.tick(commands);
        wait(game,40);
        go(game,0,{0,0,-9.8f});
    } else {
        go(game,0,{0,0,3.55f});
        if (game.player(0).body.ground != 3) throw std::runtime_error("Ferry failed to board");
        go(game,1,{2.8f,0,7});
        wait(game,260);
        if (game.player(0).body.feet.z > -4.7f || game.player(0).body.ground != 2) throw std::runtime_error("Ferry passenger z=" + std::to_string(game.player(0).body.feet.z) + " y=" + std::to_string(game.player(0).body.feet.y) + " support=" + std::to_string(game.player(0).body.ground));
        go(game,0,{0,0,-11});
    }
    if (!game.completed()) throw std::runtime_error("Expected completion");
    return game.state_hash();
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) throw std::runtime_error("Pass the level directory and optional replay output directory");
    const std::filesystem::path levels = argv[1];
    const Campaign campaign(levels);
    unsigned index = 0;
    for (const auto name : {"01-aperture.scene","02-prism.scene","03-meridian.scene"}) {
        const auto scene = load_scene(levels/name);
        Replay tape{campaign.revision(),{index,0,false},{}};
        const auto first = exercise(scene,index,&tape);
        if (check_replay(campaign,tape) != first) throw std::runtime_error("Replay verification diverged");
        if (argc == 3) write_replay(std::filesystem::path(argv[2]) / (std::string(name)+".replay"),tape);
        if (exercise(scene,index) != first) throw std::runtime_error("Same-build repeated input diverged");
        std::cout << scene.title << " completed, hash " << first << '\n';
        ++index;
    }
    Game falling(load_scene(levels/"03-meridian.scene"));
    bool recovered = false;
    for (int i=0; i<220 && !recovered; ++i) {
        falling.tick({Command{{-1,0,0}},Command{}});
        for (auto event : falling.events()) recovered = recovered || event.signal == Signal::fall;
    }
    if (!recovered || falling.player(0).body.feet != Vec3{-1,0.02f,9}) {
        throw std::runtime_error("Fall recovery did not restore initial spawn");
    }
}
