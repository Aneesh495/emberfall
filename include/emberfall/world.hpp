#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ember {

struct Entity {
    std::uint32_t index{};
    std::uint32_t generation{};
    explicit constexpr operator bool() const { return generation != 0; }
    friend constexpr bool operator==(Entity, Entity) = default;
};

// Tables own component columns. References are valid only until the next
// structural operation, or for the lifetime of a query callback.
template<class... Components>
class World {
    static_assert(sizeof...(Components) <= 63);
    static_assert((std::is_trivially_copyable_v<Components> && ...));

    using Mask = std::uint64_t;
    template<class T>
    static consteval Mask bit() {
        static_assert((std::same_as<T, Components> || ...), "Component missing from schema");
        Mask candidate = 1;
        Mask result = 0;
        ((std::same_as<T, Components> ? (void)(result = candidate) : (void)0, candidate <<= 1), ...);
        return result;
    }

    template<class... C>
    static constexpr Mask mask = (Mask{0} | ... | bit<C>());
    static constexpr std::size_t entity_limit = 1'000'000;
    static constexpr std::size_t command_limit = 131'072;

    struct Table {
        Mask signature{};
        std::vector<Entity> entities;
        std::tuple<std::vector<Components>...> columns;

        template<class T>
        auto& column() { return std::get<std::vector<T>>(columns); }
        template<class T>
        const auto& column() const { return std::get<std::vector<T>>(columns); }
        void prepare() {
            if (entities.size() != entities.capacity()) return;
            const auto count = std::min(entity_limit, std::max(std::size_t{8}, entities.size()*2));
            ((signature & bit<Components>() ? (void)column<Components>().reserve(count) : (void)0), ...);
            entities.reserve(count);
        }
        void pop_row(std::size_t row) {
            const auto compact = [this, row]<class T>() {
                if (!(signature & bit<T>())) return;
                auto& values = column<T>();
                values[row] = values.back();
                values.pop_back();
            };
            (compact.template operator()<Components>(), ...);
            entities[row] = entities.back();
            entities.pop_back();
        }
    };

    struct Slot {
        Table* table{};
        std::size_t row{};
        std::uint32_t generation{1};
    };

    struct QueryScope {
        explicit QueryScope(std::size_t& count) : count_(count) { ++count_; }
        ~QueryScope() { --count_; }
        std::size_t& count_;
    };

public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    bool alive(Entity e) const {
        return e && e.index < slots_.size() && slots_[e.index].table &&
               slots_[e.index].generation == e.generation;
    }
    std::size_t size() const { return live_; }

    template<class... C>
    Entity spawn(C... values) {
        require_barrier();
        auto& target = table(mask<C...>);
        target.prepare();
        std::uint32_t index;
        if (free_.empty()) {
            if (slots_.size() == entity_limit) throw std::length_error("World slot limit");
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        } else {
            index = free_.back();
            free_.pop_back();
        }
        auto& slot = slots_[index];
        Entity e{index,slot.generation};
        slot.table = &target;
        slot.row = target.entities.size();
        target.entities.push_back(e);
        (target.template column<C>().push_back(values), ...);
        ++live_;
        return e;
    }

    bool destroy(Entity e) {
        require_barrier();
        if (!alive(e)) return false;
        auto& slot = slots_[e.index];
        if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
            free_.push_back(e.index);
            ++slot.generation;
        }
        remove_row(*slot.table, slot.row);
        slot.table = nullptr;
        --live_;
        return true;
    }

    template<class T>
    T* get(Entity e) {
        return const_cast<T*>(std::as_const(*this).template get<T>(e));
    }

    template<class T>
    const T* get(Entity e) const {
        if (!alive(e)) return nullptr;
        const auto& slot = slots_[e.index];
        if (!(slot.table->signature & bit<T>())) return nullptr;
        return &slot.table->template column<T>()[slot.row];
    }

    template<class T>
    bool set(Entity e, T value) {
        require_barrier();
        if (!alive(e)) return false;
        if (auto* existing = get<T>(e)) {
            *existing = value;
        } else {
            migrate(e, slots_[e.index].table->signature | bit<T>(), value);
        }
        return true;
    }

    template<class T>
    bool remove(Entity e) {
        require_barrier();
        if (!get<T>(e)) return false;
        migrate(e, slots_[e.index].table->signature & ~bit<T>(), T{});
        return true;
    }

    template<class... C, class F>
    void each(F&& action) {
        run_query<World, C...>(*this, std::forward<F>(action));
    }

    template<class... C, class F>
    void each(F&& action) const {
        run_query<const World, C...>(*this, std::forward<F>(action));
    }

    template<class... C, class F>
    void chunks(F&& action) {
        QueryScope scope(queries_);
        for (auto& [signature,group] : tables_) {
            if ((signature & mask<C...>) != mask<C...> || group.entities.empty()) continue;
            std::invoke(action,std::span<const Entity>(group.entities),std::span<C>(group.template column<C>())...);
        }
    }

    template<class F>
    void defer(F&& action) {
        if (commands_.size() == command_limit) throw std::length_error("World deferred command limit");
        commands_.emplace_back(std::forward<F>(action));
    }

    std::size_t flush() {
        require_barrier();
        if (flushing_) throw std::logic_error("Nested world command flush");
        auto pending = std::exchange(commands_, {});
        flushing_ = true;
        try {
            for (auto& command : pending) command(*this);
        } catch (...) {
            flushing_ = false;
            throw;
        }
        flushing_ = false;
        return pending.size();
    }

private:
    template<class Self, class... C, class F>
    static void run_query(Self& self, F&& action) {
        QueryScope scope(self.queries_);
        for (auto& [signature, group] : self.tables_) {
            if ((signature & mask<C...>) != mask<C...>) continue;
            for (std::size_t row = 0; row < group.entities.size(); ++row) {
                std::invoke(action, group.entities[row], group.template column<C>()[row]...);
            }
        }
    }

    void require_barrier() const {
        if (queries_) throw std::logic_error("Structural world mutation during a query");
    }

    Table& table(Mask signature) {
        auto [entry, created] = tables_.try_emplace(signature);
        if (created) entry->second.signature = signature;
        return entry->second;
    }

    void remove_row(Table& source, std::size_t row) {
        slots_[source.entities.back().index].row = row;
        source.pop_row(row);
    }

    template<class Changed>
    void migrate(Entity e, Mask signature, Changed value) {
        auto& slot = slots_[e.index];
        auto& source = *slot.table;
        auto& target = table(signature);
        target.prepare();
        const auto append = [&]<class T>() {
            if (!(signature & bit<T>())) return;
            if constexpr (std::same_as<T, Changed>) {
                target.template column<T>().push_back(value);
            } else {
                target.template column<T>().push_back(source.template column<T>()[slot.row]);
            }
        };
        (append.template operator()<Components>(), ...);
        const auto row = target.entities.size();
        target.entities.push_back(e);
        remove_row(source, slot.row);
        slot.table = &target;
        slot.row = row;
    }

    std::map<Mask, Table> tables_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    std::vector<std::function<void(World&)>> commands_;
    mutable std::size_t queries_{};
    std::size_t live_{};
    bool flushing_{};
};

}
