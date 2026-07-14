#include "emberfall/game.hpp"

#include <bit>
#include <numbers>

namespace ember {
namespace {

constexpr float pi = std::numbers::pi_v<float>;

float planar_distance(Vec3 a, Vec3 b) {
    const Vec3 difference{a.x-b.x,0,a.z-b.z};
    return length(difference);
}

bool near(Vec3 a, Vec3 b, float radius) {
    return std::abs(a.y-b.y) < 1.1f && planar_distance(a,b) < radius;
}

bool collider(Kind kind) {
    return kind == Kind::solid || kind == Kind::door || kind == Kind::platform || kind == Kind::crate;
}

void append_box(std::vector<RenderItem>& output, Vec3 position, Vec3 size, Vec3 color,
                float yaw = 0, float emission = 0) {
    output.push_back({transform_matrix(position,yaw_rotation(yaw),size),color,emission});
}

void add_player(std::vector<RenderItem>& output, const Player& p, Vec3 feet, Vec3 color) {
    const auto orientation = transform_matrix(feet,yaw_rotation(p.facing));
    const float stride = std::sin(p.stride) * std::min(0.16f, length(Vec3{p.body.velocity.x,0,p.body.velocity.z})*0.04f);
    const float height = p.body.height/1.45f;
    const auto part = [&](Vec3 position, Vec3 size, Vec3 tint, float emission = 0) {
        position.y *= height;
        size.y *= height;
        output.push_back({orientation*transform_matrix(position,{},size),tint,emission});
    };
    part({0,0.78f,0}, {0.46f,0.55f,0.34f}, color);
    part({0,1.18f,0}, {0.34f,0.31f,0.31f}, {0.18f,0.23f,0.3f});
    part({0,1.21f,-0.16f}, {0.27f,0.09f,0.035f}, color,2.2f);
    part({-0.13f,0.28f,stride}, {0.17f,0.52f,0.19f}, {0.14f,0.18f,0.23f});
    part({0.13f,0.28f,-stride}, {0.17f,0.52f,0.19f}, {0.14f,0.18f,0.23f});
    part({0.32f,0.65f,0.03f}, {0.13f,0.24f,0.13f}, color,1.8f);
}

}

Game::Game(const Scene& scene, bool allow_incomplete) {
    validate_scene(scene);
    const auto transforms = resolve_transforms(scene);
    std::size_t spawns = 0;
    bool exit = false;
    for (const auto& [id,node] : scene.nodes) {
        if (node.kind == Kind::spawn) {
            if (spawns < spawn_.size()) spawn_[spawns] = node.position;
            ++spawns;
            continue;
        }
        exit = exit || node.kind == Kind::exit;
        const auto matrix = transforms.at(id);
        const auto position = transform_point(matrix,{});
        const Instance instance{id,node.link,node.kind,node.size,node.color,position,
                                node.destination,node.parameter,matrix};
        const BodyPose pose{position,position,node.yaw};
        const auto entity = world_.spawn(instance,pose,Mechanism{});
        entities_.emplace(id,entity);
    }
    if (!allow_incomplete && (spawns != 2 || !exit)) throw std::runtime_error("Game scene requires two spawns and an exit");
    for (auto& player : players_) player = world_.spawn(BodyPose{},Player{});
    reset_players(spawn_[0],spawn_[1]);
    colliders_.reserve(scene.nodes.size());
    events_.reserve(32);
    rebuild_collision();
}

void Game::reset_players(Vec3 first, Vec3 second) {
    const std::array positions{first,second};
    for (std::size_t i=0; i<players_.size(); ++i) {
        *world_.get<Player>(players_[i]) = Player{Character{positions[i],{},0,1.45f}};
        *world_.get<BodyPose>(players_[i]) = {positions[i],positions[i],0};
    }
}

Vec3 Game::location(SceneId id) const { return world_.get<BodyPose>(entities_.at(id))->position; }

bool Game::powered(SceneId id) const {
    return id != 0 && world_.get<Mechanism>(entities_.at(id))->active;
}

void Game::rebuild_collision() {
    colliders_.clear();
    world_.each<Instance,BodyPose,Mechanism>([&](Entity, const Instance& n, const BodyPose& pose, const Mechanism&) {
        if (!collider(n.kind)) return;
        colliders_.push_back({n.id, transformed_box(transform_matrix(pose.position,yaw_rotation(pose.yaw),n.size))});
    });
    collision_.rebuild(colliders_);
}

void Game::run_switches() {
    world_.each<Instance,Mechanism>([&](Entity, const Instance& n, Mechanism& state) {
        if (n.kind == Kind::plate || n.kind == Kind::receiver) state.active = false;
        if (n.kind != Kind::plate) return;
        for (std::size_t i=0; i<players_.size(); ++i) {
            if (near(player(i).body.feet, n.origin, std::max(0.55f,std::abs(n.size.x)*0.55f))) {
                state.active = true;
            }
        }
        world_.each<Instance,BodyPose>([&](Entity, const Instance& other, const BodyPose& pose) {
            if (other.kind == Kind::crate && near(pose.position,n.origin,0.7f)) state.active = true;
        });
    });
    world_.each<Instance,BodyPose>([&](Entity, const Instance& n, const BodyPose& pose) {
        if (n.kind != Kind::mirror) return;
        const auto delta = location(n.link)-pose.position;
        const Vec3 heading{std::sin(pose.yaw),0,-std::cos(pose.yaw)};
        const Vec3 horizontal{delta.x,0,delta.z};
        const float reach = n.parameter > 0 ? n.parameter : 15;
        if (dot(unit(horizontal),heading) < 0.985f || length(horizontal) > reach) return;
        if (collision_.sweep(pose.position,{0.025f,0.025f,0.025f},delta).surface) return;
        world_.get<Mechanism>(entities_.at(n.link))->active = true;
    });
}

void Game::run_devices() {
    world_.each<Instance,BodyPose,Mechanism>([&](Entity, const Instance& n, BodyPose& pose, Mechanism& state) {
        if (n.kind == Kind::door) {
            bool circuit = powered(n.link);
            const auto& source = *world_.get<Instance>(entities_.at(n.link));
            if (source.kind == Kind::plate && source.parameter > 0) {
                world_.each<Instance,Mechanism>([&](Entity, const Instance& other, const Mechanism& input) {
                    if (other.kind == Kind::plate && other.parameter == source.parameter && !input.active) circuit = false;
                });
            }
            if (circuit) state.ticks = static_cast<std::uint32_t>(std::round(n.parameter*60))+1;
            else if (state.ticks) --state.ticks;
            const bool open = state.ticks > 0;
            if (open && !state.active) events_.push_back({Signal::latch,n.id});
            state.active = open;
            const auto next = std::clamp(state.travel + (open ? 0.035f : -0.035f),0.0f,1.0f);
            Vec3 position = n.origin + Vec3{0,(std::abs(n.size.y)+0.4f)*next,0};
            bool occupied = false;
            if (next < state.travel) {
                const auto gate = transformed_box(transform_matrix(position,yaw_rotation(pose.yaw),n.size));
                for (std::size_t i=0; i<players_.size(); ++i) {
                    const auto& p = player(i).body;
                    const auto feet = p.feet;
                    occupied = occupied || overlaps(gate,{feet-Vec3{0.28f,0,0.28f},feet+Vec3{0.28f,p.height,0.28f}});
                }
            }
            if (!occupied) {
                state.travel = next;
                pose.position = position;
            }
        } else if (n.kind == Kind::platform) {
            const float speed = n.parameter > 0 ? n.parameter : 1.2f;
            const float distance = length(n.destination-n.origin);
            if (distance < 1e-4f) return;
            const float increment = speed*timestep/distance;
            if (n.link) {
                state.travel = std::clamp(state.travel + (powered(n.link) ? increment : -increment),0.0f,1.0f);
            } else {
                state.travel += state.active ? -increment : increment;
                if (state.travel >= 1 || state.travel <= 0) {
                    state.travel = std::clamp(state.travel,0.0f,1.0f);
                    state.active = !state.active;
                }
            }
            pose.position = lerp(n.origin,n.destination,state.travel);
        }
    });
}

void Game::interact(std::size_t index) {
    auto& p = *world_.get<Player>(players_[index]);
    Entity nearest;
    float best = 1.7f;
    world_.each<Instance,BodyPose>([&](Entity e, const Instance& n, const BodyPose& pose) {
        if (n.kind != Kind::mirror && n.kind != Kind::crate) return;
        const auto distance = length(pose.position-(p.body.feet+Vec3{0,0.65f,0}));
        if (distance >= best) return;
        const auto reach = pose.position-(p.body.feet+Vec3{0,0.65f,0});
        const auto hit = collision_.sweep(p.body.feet+Vec3{0,0.65f,0},{},reach,n.id);
        if (hit.surface) return;
        best = distance;
        nearest = e;
    });
    if (!nearest) return;
    const auto& n = *world_.get<Instance>(nearest);
    auto& pose = *world_.get<BodyPose>(nearest);
    if (n.kind == Kind::mirror) {
        pose.yaw = std::remainder(pose.yaw + pi*0.5f,2*pi);
        events_.push_back({Signal::mirror,n.id});
    } else {
        auto direction = pose.position-p.body.feet;
        if (std::abs(direction.x) > std::abs(direction.z)) direction = {std::copysign(1.0f,direction.x),0,0};
        else direction = {0,0,std::copysign(1.0f,direction.z)};
        const auto shape = transformed_box(transform_matrix(pose.position,yaw_rotation(pose.yaw),n.size));
        const auto half = (shape.maximum-shape.minimum)*0.5f;
        const auto movement = direction*0.45f;
        const auto next = pose.position+movement;
        const auto floor = collision_.sweep(next,half,{0,-0.2f,0},n.id);
        if (!collision_.sweep(pose.position,half,movement,n.id).surface &&
            collision_.clear(next,half,n.id) && floor.surface && floor.normal.y > 0) {
            pose.position = next;
            rebuild_collision();
        }
    }
}

void Game::restore_checkpoint(SceneId id) {
    if (id == 0) {
        checkpoint_ = 0;
        reset_players(spawn_[0],spawn_[1]);
        return;
    }
    const auto entry = entities_.find(id);
    if (entry == entities_.end() || world_.get<Instance>(entry->second)->kind != Kind::checkpoint) {
        throw std::runtime_error("Unknown checkpoint");
    }
    const auto origin = location(id);
    checkpoint_ = id;
    const auto feet = origin + Vec3{0,0.16f,0};
    reset_players(feet + Vec3{-0.45f,0,0},feet + Vec3{0.45f,0,0});
}

void Game::progress() {
    bool fallen = false;
    for (std::size_t i=0; i<players_.size(); ++i) fallen = fallen || player(i).body.feet.y < -8;
    if (fallen) {
        restore_checkpoint(checkpoint_);
        events_.push_back({Signal::fall,checkpoint_});
        return;
    }
    world_.each<Instance>([&](Entity, const Instance& n) {
        if (n.kind == Kind::checkpoint && n.id > checkpoint_) {
            for (std::size_t i=0; i<players_.size(); ++i) {
                if (near(player(i).body.feet,n.origin,1.0f)) {
                    checkpoint_ = n.id;
                    events_.push_back({Signal::checkpoint,n.id});
                    break;
                }
            }
        } else if (n.kind == Kind::exit && !completed_ && (!n.link || powered(n.link))) {
            for (std::size_t i=0; i<players_.size(); ++i) {
                if (near(player(i).body.feet,n.origin,0.9f)) {
                    completed_ = true;
                    events_.push_back({Signal::exit,n.id});
                    break;
                }
            }
        }
    });
}

void Game::tick(const std::array<Command,2>& commands) {
    events_.clear();
    if (completed_) return;
    ++ticks_;
    world_.each<BodyPose>([](Entity, BodyPose& pose) { pose.previous = pose.position; });
    run_switches();
    run_devices();
    rebuild_collision();
    for (std::size_t i=0; i<players_.size(); ++i) {
        auto& p = *world_.get<Player>(players_[i]);
        auto& pose = *world_.get<BodyPose>(players_[i]);
        if (p.body.ground) {
            const auto* floor = world_.get<BodyPose>(entities_.at(p.body.ground));
            p.body.feet += floor->position-floor->previous;
        }
        const auto& command = commands[i];
        auto move = finite(command.move) ? Vec3{command.move.x,0,command.move.z} : Vec3{};
        if (dot(move,move) > 1) move = unit(move);
        if (dot(move,move) > 0.005f) p.facing = std::atan2(-move.x,-move.z);
        move_character(p.body,collision_,move*(command.crouch ? 1.85f : 4.2f),command.jump,command.crouch,timestep);
        p.stride = std::remainder(p.stride+length(move)*0.19f,2*pi);
        pose.position = p.body.feet;
        if (command.interact) interact(i);
    }
    progress();
}

std::vector<RenderItem> Game::extract(float alpha, std::optional<Vec3> remote_first) const {
    std::vector<RenderItem> output;
    output.reserve(world_.size()+20);
    world_.each<Instance,BodyPose,Mechanism>([&](Entity, const Instance& n, const BodyPose& pose, const Mechanism& s) {
        const auto position = lerp(pose.previous,pose.position,std::clamp(alpha,0.0f,1.0f));
        if (n.kind == Kind::decor) {
            output.push_back({n.authored,n.color,n.parameter});
            return;
        }
        float emission = 0;
        Vec3 color = n.color;
        if (n.kind == Kind::exit || n.kind == Kind::checkpoint || n.kind == Kind::receiver || n.kind == Kind::plate) {
            const bool lit = s.active || n.id == checkpoint_ || n.kind == Kind::exit;
            emission = lit ? 2.4f : 0.25f;
            if (lit) color = lerp(color,{0.20f,0.92f,0.74f},0.7f);
        }
        auto visual_size = n.size;
        // Keep the ferry deck surface separate from the dock when the two supports overlap.
        if (n.kind == Kind::platform) visual_size.y *= 0.94f;
        append_box(output,position,visual_size,color,pose.yaw,emission);
        if (n.kind == Kind::mirror) {
            const float distance = powered(n.link) ? planar_distance(position,location(n.link)) : 1.2f;
            const Vec3 direction{std::sin(pose.yaw),0,-std::cos(pose.yaw)};
            append_box(output,position+direction*(distance*0.5f),{0.075f,0.075f,distance},{1,0.55f,0.14f},-pose.yaw,3.1f);
        }
    });
    for (std::size_t i=0; i<players_.size(); ++i) {
        const auto& pose = *world_.get<BodyPose>(players_[i]);
        const auto position = i == 0 && remote_first ? *remote_first : lerp(pose.previous,pose.position,alpha);
        add_player(output,player(i),position,i == 0 ? Vec3{0.95f,0.43f,0.10f} : Vec3{0.10f,0.74f,0.83f});
    }
    return output;
}

Route Game::route(Vec3 start,Vec3 goal) const { return find_route(collision_,start,goal); }

SimulationState Game::snapshot() const {
    SimulationState result{ticks_,checkpoint_,completed_,{player(0),player(1)},{}};
    for (const auto& [id,e] : entities_) {
        const auto kind = world_.get<Instance>(e)->kind;
        if (kind == Kind::solid || kind == Kind::decor || kind == Kind::exit || kind == Kind::checkpoint) continue;
        const auto& pose = *world_.get<BodyPose>(e);
        result.devices.push_back({id,pose.position,pose.yaw,*world_.get<Mechanism>(e)});
    }
    return result;
}

void Game::apply_snapshot(const SimulationState& snapshot) {
    const auto expected = this->snapshot();
    const auto finite_position = [](Vec3 p) {
        return finite(p) && std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)}) <= 4096;
    };
    if (snapshot.devices.size() != expected.devices.size()) throw std::runtime_error("Snapshot scene schema mismatch");
    if (snapshot.checkpoint) {
        const auto found = entities_.find(snapshot.checkpoint);
        if (found == entities_.end() || world_.get<Instance>(found->second)->kind != Kind::checkpoint) {
            throw std::runtime_error("Snapshot checkpoint mismatch");
        }
    }
    for (std::size_t i=0; i<snapshot.devices.size(); ++i) {
        const auto& device = snapshot.devices[i];
        if (device.id != expected.devices[i].id || !finite_position(device.position) ||
            !std::isfinite(device.yaw) || std::abs(device.yaw) > 65 ||
            !std::isfinite(device.state.travel) || device.state.travel < 0 || device.state.travel > 1 ||
            device.state.ticks > 216001) throw std::runtime_error("Snapshot device outside contract");
    }
    for (const auto& p : snapshot.players) {
        const auto support = entities_.find(p.body.ground);
        if (!finite_position(p.body.feet) || !finite(p.body.velocity) || length(p.body.velocity) > 40 ||
            !std::isfinite(p.facing) || std::abs(p.facing) > 6.3f || !std::isfinite(p.stride) ||
            std::abs(p.stride) > 6.3f || !std::isfinite(p.body.height) || p.body.height < 0.89f ||
            p.body.height > 1.46f || (p.body.ground && (support == entities_.end() ||
            !collider(world_.get<Instance>(support->second)->kind)))) {
            throw std::runtime_error("Snapshot player outside contract");
        }
    }
    ticks_ = snapshot.ticks;
    checkpoint_ = snapshot.checkpoint;
    completed_ = snapshot.completed;
    events_.clear();
    for (std::size_t i=0; i<players_.size(); ++i) {
        const auto& p = snapshot.players[i];
        *world_.get<Player>(players_[i]) = p;
        *world_.get<BodyPose>(players_[i]) = {p.body.feet,p.body.feet,0};
    }
    for (const auto& device : snapshot.devices) {
        const auto e = entities_.at(device.id);
        *world_.get<BodyPose>(e) = {device.position,device.position,device.yaw};
        *world_.get<Mechanism>(e) = device.state;
    }
    rebuild_collision();
}

std::uint64_t Game::state_hash() const {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&](std::uint64_t value) {
        for (unsigned i=0; i<8; ++i) {
            hash = (hash ^ (value & 255))*1099511628211ULL;
            value >>= 8;
        }
    };
    const auto real = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    const auto vector = [&](Vec3 value) { real(value.x); real(value.y); real(value.z); };
    add(ticks_); add(checkpoint_); add(completed_);
    for (std::size_t i=0; i<players_.size(); ++i) {
        const auto& p = player(i);
        vector(p.body.feet); vector(p.body.velocity); add(p.body.ground);
        real(p.body.height); real(p.facing); real(p.stride);
    }
    for (const auto& [id,e] : entities_) {
        const auto& pose = *world_.get<BodyPose>(e);
        const auto& state = *world_.get<Mechanism>(e);
        add(id); vector(pose.position); real(pose.yaw);
        real(state.travel); add(state.ticks); add(state.active);
    }
    return hash;
}

std::vector<RenderItem> extract_edit_scene(const Scene& scene, SceneId selected) {
    std::vector<RenderItem> output;
    const auto transforms = resolve_transforms(scene);
    output.reserve(scene.nodes.size());
    for (const auto& [id,node] : scene.nodes) {
        const auto& matrix = transforms.at(id);
        output.push_back({matrix,node.color,id == selected ? 1.35f : 0});
    }
    return output;
}

}
