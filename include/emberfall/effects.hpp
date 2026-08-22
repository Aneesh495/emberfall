#pragma once

#include "emberfall/game.hpp"

namespace ember {

enum class EffectKind : std::uint32_t { burst, ground_ring };

struct Effect {
    Vec3 position;
    float age{};
    Vec3 radiance;
    float radius{};
    std::uint32_t seed{};
    EffectKind kind{};
};

class Effects {
public:
    static constexpr std::size_t limit = 32;
    static constexpr float lifetime = 1.5f;
    void update(float seconds);
    void add(GameEvent event,const Scene& scene,Vec3 player,std::uint64_t tick);
    void clear() { active_.clear(); }
    std::span<const Effect> active() const { return active_; }
private:
    void push(Effect effect);
    std::vector<Effect> active_;
};

}
