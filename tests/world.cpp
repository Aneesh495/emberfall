#include "emberfall/world.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>

using ember::Entity;
struct Position { int value{}; };
struct Health { int value{}; };
using World = ember::World<Position,Health>;

void check(bool ok, unsigned step) {
    if (!ok) throw std::runtime_error("World differs from reference at step " + std::to_string(step));
}

int main() {
    World world;
    std::mt19937 random(50419);
    struct Record {
        Entity entity;
        Entity retired;
        std::optional<Position> position;
        std::optional<Health> health;
    };
    std::array<Record,257> model{};
    constexpr unsigned trials = 1'000'000;
    for (unsigned step = 0; step < trials; ++step) {
        auto& record = model[random() % model.size()];
        const auto operation = random() % 9;
        if (!record.entity) {
            record.position = Position{static_cast<int>(step)};
            record.entity = world.spawn(*record.position);
        } else if (operation == 0) {
            check(world.destroy(record.entity), step);
            record.retired = std::exchange(record.entity, {});
            record.position.reset();
            record.health.reset();
        } else if (operation == 1) {
            record.position.reset();
            world.remove<Position>(record.entity);
        } else if (operation == 2) {
            record.health.reset();
            world.remove<Health>(record.entity);
        } else if (operation == 3) {
            record.health = Health{static_cast<int>(step % 97)};
            check(world.set(record.entity, *record.health), step);
        } else if (operation == 4) {
            record.position = Position{static_cast<int>(step)};
            check(world.set(record.entity, *record.position), step);
        } else if (operation == 5) {
            const auto* p = world.get<Position>(record.entity);
            check(bool(p) == bool(record.position), step);
            if (p) check(p->value == record.position->value, step);
        } else if (operation == 6) {
            const auto* h = world.get<Health>(record.entity);
            check(bool(h) == bool(record.health), step);
            if (h) check(h->value == record.health->value, step);
        } else if (operation == 7) {
            check(!world.set(record.retired, Health{1}), step);
        } else {
            std::int64_t expected = 0;
            std::size_t expected_matches = 0;
            std::size_t expected_alive = 0;
            for (const auto& entry : model) {
                expected_alive += bool(entry.entity);
                if (entry.health && entry.position) {
                    expected += entry.health->value + entry.position->value;
                    ++expected_matches;
                }
            }
            std::size_t matches = 0;
            std::int64_t actual = 0;
            std::as_const(world).each<Position,Health>([&](Entity e, const Position& p, const Health& h) {
                check(world.alive(e), step);
                actual += p.value + h.value;
                ++matches;
            });
            check(actual == expected && matches == expected_matches && world.size() == expected_alive, step);
        }
        check(!world.alive(record.retired), step);
    }
    bool blocked = false;
    world.each<Position>([&](Entity e, Position&) {
        try { world.destroy(e); }
        catch (const std::logic_error&) { blocked = true; }
        world.defer([e](World& w) { w.destroy(e); });
    });
    const auto before = world.size();
    const auto removed = world.flush();
    check(blocked && removed > 0 && world.size() + removed == before, trials);
    std::cout << "world oracle: " << trials << " operations, seed 50419\n";
}
