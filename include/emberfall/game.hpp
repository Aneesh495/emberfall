#pragma once

#include "emberfall/animation.hpp"
#include "emberfall/collision.hpp"
#include "emberfall/navigation.hpp"
#include "emberfall/mesh.hpp"
#include "emberfall/world.hpp"

namespace ember {

struct Command {
    Vec3 move;
    bool jump{}, interact{}, crouch{};
};

struct RenderItem {
    Mat4 model;
    Vec3 color;
    float emission{};
    std::array<float,4> surface{0,0,0,-1};
    float metallic{};
    std::uint64_t history_id{};
    std::array<std::uint32_t,3> texture_layers{};
    float normal_scale{1};
};

struct BodyPose {
    Vec3 previous, position;
    float yaw{};
};

struct Instance {
    SceneId id{}, link{};
    Kind kind{};
    Vec3 size, color, origin, destination;
    float parameter{};
    Mat4 authored;
};

struct Mechanism {
    float travel{};
    std::uint32_t ticks{};
    bool active{};
};

struct Player {
    Character body;
    float facing{};
    float stride{};
};

enum class Signal : std::uint8_t { latch, mirror, checkpoint, fall, exit };
struct GameEvent {
    Signal signal{};
    SceneId subject{};
};

struct DeviceState {
    SceneId id{};
    Vec3 position;
    float yaw{};
    Mechanism state;
};

struct SimulationState {
    std::uint64_t ticks{};
    SceneId checkpoint{};
    bool completed{};
    std::array<Player,2> players;
    std::vector<DeviceState> devices;
};

class Game {
public:
    static constexpr float timestep = 1.0f/60;
    explicit Game(const Scene& scene, bool allow_incomplete = false, std::shared_ptr<const MeshLibrary> assets = {});
    std::span<const MeshInstance> imported_meshes() const { return imported_; }

    void tick(const std::array<Command,2>& commands);
    std::vector<RenderItem> extract(float alpha, std::optional<Vec3> remote_first = {}) const;
    std::vector<SkinBatch> skin(float alpha,std::optional<Vec3> remote_first = {}) const;
    const Player& player(std::size_t slot) const { return *world_.get<Player>(players_.at(slot)); }
    std::uint64_t ticks() const { return ticks_; }
    bool completed() const { return completed_; }
    SceneId checkpoint() const { return checkpoint_; }
    std::span<const GameEvent> events() const { return events_; }
    std::span<const Collider> colliders() const { return colliders_; }
    std::uint64_t state_hash() const;
    void restore_checkpoint(SceneId id);
    SimulationState snapshot() const;
    Route route(Vec3 start,Vec3 goal) const;
    void apply_snapshot(const SimulationState& snapshot);

private:
    void rebuild_collision();
    void reset_players(Vec3 first, Vec3 second);
    bool powered(SceneId id) const;
    Vec3 location(SceneId id) const;
    void run_switches();
    void run_devices();
    void interact(std::size_t player);
    void progress();

    std::shared_ptr<const MeshLibrary> assets_;
    std::vector<MeshInstance> imported_;
    std::map<SceneId,const MeshAsset*> imported_nodes_;
    World<BodyPose, Instance, Mechanism, Player> world_;
    std::array<Entity,2> players_{};
    std::array<Vec3,2> spawn_{};
    std::map<SceneId,Entity> entities_;
    std::vector<Collider> colliders_;
    CollisionSpace collision_;
    std::vector<GameEvent> events_;
    std::uint64_t ticks_{};
    SceneId checkpoint_{};
    bool completed_{};
};

std::vector<RenderItem> extract_edit_scene(const Scene& scene, SceneId selected = {});

}
