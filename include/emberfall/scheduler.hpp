#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace ember {

using TaskId = std::uint16_t;
using ResourceId = std::uint16_t;

struct Access {
    ResourceId resource{};
    bool write{};
};

struct Task {
    std::vector<TaskId> after;
    std::vector<Access> access;
    std::function<void(std::stop_token)> body;
};

struct RunReport {
    std::size_t completed{};
    unsigned peak_workers{};
    bool cancelled{};
};

// A resource ID denotes one independently owned memory range for this run.
class Schedule {
public:
    explicit Schedule(std::vector<Task> tasks);
    const auto& tasks() const { return tasks_; }
    const auto& successors() const { return successors_; }
    const auto& indegrees() const { return indegrees_; }
    static constexpr std::size_t task_limit = 256;
private:
    std::vector<Task> tasks_;
    std::vector<std::vector<TaskId>> successors_;
    std::vector<unsigned> indegrees_;
};

class Scheduler {
public:
    explicit Scheduler(unsigned workers);
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    RunReport run(const Schedule& schedule,std::stop_token cancel = {});
    unsigned workers() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
