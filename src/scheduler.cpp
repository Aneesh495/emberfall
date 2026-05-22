#include "emberfall/scheduler.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <bitset>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace ember {

Schedule::Schedule(std::vector<Task> tasks)
    : tasks_(std::move(tasks)), successors_(tasks_.size()), indegrees_(tasks_.size()) {
    if (tasks_.size() > task_limit) throw std::length_error("Schedule task limit");
    std::vector<TaskId> queue;
    for (std::size_t i=0; i<tasks_.size(); ++i) {
        auto& task = tasks_[i];
        if (!task.body || task.access.size() > 64) throw std::runtime_error("Task body or access list invalid");
        std::ranges::sort(task.after);
        if (std::ranges::adjacent_find(task.after) != task.after.end()) throw std::runtime_error("Duplicate dependency");
        std::ranges::sort(task.access,{},&Access::resource);
        if (std::ranges::adjacent_find(task.access,{},&Access::resource) != task.access.end()) {
            throw std::runtime_error("Duplicate access declaration");
        }
        indegrees_[i] = static_cast<unsigned>(task.after.size());
        for (auto after : task.after) {
            if (after >= tasks_.size() || after == i) throw std::runtime_error("Invalid task dependency");
            successors_[after].push_back(static_cast<TaskId>(i));
        }
        if (!indegrees_[i]) queue.push_back(static_cast<TaskId>(i));
    }
    auto remaining = indegrees_;
    std::vector<std::bitset<task_limit>> ancestors(tasks_.size());
    for (std::size_t i=0; i<queue.size(); ++i) {
        const auto current = queue[i];
        for (auto next : successors_[current]) {
            ancestors[next] |= ancestors[current];
            ancestors[next].set(current);
            if (--remaining[next]==0) queue.push_back(next);
        }
    }
    if (queue.size() != tasks_.size()) throw std::runtime_error("Schedule contains a dependency cycle");
    for (std::size_t a=0; a<tasks_.size(); ++a) {
        for (std::size_t b=a+1; b<tasks_.size(); ++b) {
            if (ancestors[a][b] || ancestors[b][a]) continue;
            auto first = tasks_[a].access.begin();
            auto second = tasks_[b].access.begin();
            while (first != tasks_[a].access.end() && second != tasks_[b].access.end()) {
                if (first->resource < second->resource) ++first;
                else if (second->resource < first->resource) ++second;
                else {
                    if (first->write || second->write) throw std::runtime_error("Unordered task resource hazard");
                    ++first;
                    ++second;
                }
            }
        }
    }
}

struct Scheduler::State {
    struct Batch {
        explicit Batch(const Schedule& plan)
            : plan(plan), remaining(plan.indegrees()) {
            for (std::size_t i=0; i<remaining.size(); ++i) {
                if (!remaining[i]) push(static_cast<TaskId>(i));
            }
        }

        void push(TaskId task) { ready[end++] = task; }
        const Schedule& plan;
        std::vector<unsigned> remaining;
        std::array<TaskId,Schedule::task_limit> ready{};
        std::size_t begin{}, end{}, completed{};
        unsigned running{}, peak{};
        std::stop_source stop;
        std::exception_ptr failure;
    };

    explicit State(unsigned count) {
        if (!count || count > 16) throw std::runtime_error("Scheduler worker limit is 1..16");
        threads.reserve(count);
        try {
            for (unsigned i=0; i<count; ++i) threads.emplace_back([this] { worker(); });
        } catch (...) {
            shutdown();
            throw;
        }
    }

    ~State() { shutdown(); }

    void shutdown() {
        {
            std::scoped_lock lock(mutex);
            done = true;
        }
        signal.notify_all();
        threads.clear();
    }

    void worker() {
        std::unique_lock lock(mutex);
        while (true) {
            signal.wait(lock,[&] { return done || (batch && batch->begin<batch->end && !batch->stop.stop_requested()); });
            if (done) return;
            auto& work = *batch;
            const auto task = work.ready[work.begin++];
            work.peak = std::max(work.peak,++work.running);
            std::exception_ptr error;
            lock.unlock();
            try { work.plan.tasks()[task].body(work.stop.get_token()); }
            catch (...) { error = std::current_exception(); }
            if (error) work.stop.request_stop();
            lock.lock();
            --work.running;
            if (error && !work.failure) work.failure = error;
            if (!error) ++work.completed;
            if (!work.stop.stop_requested()) {
                for (const auto next : work.plan.successors()[task]) {
                    if (--work.remaining[next]==0) work.push(next);
                }
            }
            signal.notify_all();
        }
    }

    RunReport run(const Schedule& schedule,std::stop_token cancel) {
        Batch work(schedule);
        std::stop_callback callback(cancel,[&] {
            work.stop.request_stop();
            // Serialize with wait predicate checks to prevent a missed wake.
            std::scoped_lock lock(mutex);
            signal.notify_all();
        });
        std::unique_lock lock(mutex);
        if (batch) throw std::logic_error("Scheduler already running a batch");
        batch = &work;
        signal.notify_all();
        signal.wait(lock,[&] {
            return work.completed==schedule.tasks().size() || (work.running==0 && work.stop.stop_requested());
        });
        batch = nullptr;
        lock.unlock();
        if (work.failure) std::rethrow_exception(work.failure);
        return {work.completed,work.peak,work.stop.stop_requested()};
    }

    std::mutex mutex;
    std::condition_variable signal;
    std::vector<std::jthread> threads;
    Batch* batch{};
    bool done{};
};

Scheduler::Scheduler(unsigned workers) : state_(std::make_unique<State>(workers)) {}
Scheduler::~Scheduler() = default;
RunReport Scheduler::run(const Schedule& schedule,std::stop_token cancel) { return state_->run(schedule,cancel); }
unsigned Scheduler::workers() const { return static_cast<unsigned>(state_->threads.size()); }

}
