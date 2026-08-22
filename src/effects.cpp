#include "emberfall/effects.hpp"

namespace ember {

void Effects::update(float seconds) {
    for (auto& effect : active_) effect.age += std::max(0.0f,seconds);
    std::erase_if(active_,[](const auto& effect) { return effect.age>=lifetime; });
}

void Effects::push(Effect effect) {
    if (active_.size()==limit) active_.erase(active_.begin());
    active_.push_back(effect);
}

void Effects::add(GameEvent event,const Scene& scene,Vec3 player,std::uint64_t tick) {
    const auto source = scene.nodes.find(event.subject);
    const auto position = source==scene.nodes.end() ? player : source->second.position;
    const auto seed = static_cast<std::uint32_t>(tick)^event.subject*2246822519U;
    Vec3 color;
    switch (event.signal) {
    case Signal::latch: color = {2.1f,0.64f,0.16f}; break;
    case Signal::mirror: color = {2.2f,0.91f,0.27f}; break;
    case Signal::checkpoint: color = {0.20f,1.9f,1.4f}; break;
    case Signal::fall: color = {0.3f,0.8f,1.65f}; break;
    case Signal::exit: color = {0.27f,1.7f,1.12f}; break;
    }
    const auto emitter = event.signal==Signal::fall ? player : position;
    push({emitter+Vec3{0,0.25f,0},0,color,0.10f,seed,EffectKind::burst});
    if (event.signal==Signal::checkpoint || event.signal==Signal::fall) {
        const float ground = event.signal==Signal::checkpoint && source!=scene.nodes.end() ?
            position.y+std::abs(source->second.size.y)*0.5f : player.y;
        push({{emitter.x,ground+0.018f,emitter.z},0,color*0.7f,0.70f,seed,EffectKind::ground_ring});
    }
}

}
