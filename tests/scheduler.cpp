#include "emberfall/scheduler.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <latch>
#include <random>
#include <stdexcept>

using namespace ember;

void check(bool value) { if (!value) throw std::runtime_error("Scheduler property failed"); }

int main() {
    Scheduler pool(4);
    std::mt19937 random(73822);
    for (unsigned trial=0; trial<160; ++trial) {
        std::array<unsigned,80> values{}, expected{};
        std::vector<Task> tasks;
        for (TaskId index=0; index<values.size(); ++index) {
            std::vector<TaskId> prerequisites;
            std::vector<Access> access{{index,true}};
            expected[index] = 1;
            for (TaskId j=0; j<index; ++j) {
                if (random()%27 != 0) continue;
                prerequisites.push_back(j);
                access.push_back({j,false});
                expected[index] += expected[j];
            }
            tasks.push_back({prerequisites,access,[&,index,prerequisites](std::stop_token) {
                unsigned sum = 1;
                for (auto id : prerequisites) sum += values[id];
                values[index] = sum;
            }});
        }
        check(pool.run(Schedule(std::move(tasks))).completed == values.size() && values == expected);
    }
    const auto noop = [](std::stop_token) {};
    const auto reject = [](auto&& action) {
        try { action(); }
        catch (const std::runtime_error&) { return; }
        throw std::logic_error("Invalid graph accepted");
    };
    reject([&] { Schedule cycle({{{1},{},noop},{{0},{},noop}}); });
    reject([&] { Schedule race({{{},{{5,true}},noop},{{},{{5,false}},noop}}); });
    std::latch concurrent(4);
    std::vector<Task> parallel(4);
    for (auto& task : parallel) task.body = [&](std::stop_token) { concurrent.arrive_and_wait(); };
    const auto report = pool.run(Schedule(std::move(parallel)));
    check(report.completed==4 && report.peak_workers==4);
    std::stop_source stop;
    const Schedule cancelled({{{},{},[&](std::stop_token) { stop.request_stop(); }},
                              {{0},{},[](std::stop_token) { throw std::runtime_error("Unexpected task"); }}});
    const auto cancelled_report = pool.run(cancelled,stop.get_token());
    check(cancelled_report.cancelled && cancelled_report.completed == 1);
    const Schedule error({{{},{},[](std::stop_token) { throw std::runtime_error("Task exception"); }}});
    reject([&] { (void)pool.run(error); });
    check(pool.run(Schedule({{{},{},noop}})).completed == 1);
    std::cout << "random_graphs=160 tasks_per_graph=80 peak_workers=" << report.peak_workers << '\n';
}
