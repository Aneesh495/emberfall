#include "emberfall/scene_stream.hpp"

#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace ember {
namespace {

constexpr std::size_t slot_limit = 128;
constexpr std::size_t queue_limit = 32;
constexpr std::size_t raw_limit = 2'000'000;
std::atomic<std::uint32_t> next_owner{1};

bool valid_name(std::string_view name) {
    return name.size() <= 80 && name.ends_with(".scene") &&
           !name.starts_with('.') && std::ranges::all_of(name,[](char c) {
               return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='.' || c=='-' || c=='_';
           });
}

bool pending(StreamState state) {
    return state == StreamState::queued || state == StreamState::loading;
}

std::size_t estimate(const Scene& scene) {
    std::size_t bytes = sizeof(Scene) + scene.title.capacity() + scene.objective.capacity();
    for (const auto& [id,node] : scene.nodes) {
        (void)id;
        bytes += sizeof(node)+node.name.capacity()+5*sizeof(void*);
    }
    return bytes;
}

}

struct SceneStream::State {
    struct Entry {
        std::string name;
        StreamState state{StreamState::evicted};
        std::shared_ptr<const Scene> version;
        std::size_t bytes{};
        std::uint64_t revision{}, touched{};
        std::string error;
    };
    struct Job {
        std::uint16_t slot{};
        unsigned priority{};
        std::uint64_t revision{};
        std::string name;
    };
    struct Retired {
        std::weak_ptr<const Scene> scene;
        std::size_t bytes{};
    };

    State(std::filesystem::path folder,std::size_t ceiling,unsigned workers)
        : root(std::move(folder)), budget(ceiling) {
        if (!workers || workers>4 || budget<1024 || budget>128*1024*1024) throw std::runtime_error("Scene stream budget");
        const auto id = next_owner.fetch_add(1);
        if (id > std::numeric_limits<std::uint16_t>::max()) throw std::runtime_error("Scene stream owner capacity");
        owner = static_cast<std::uint16_t>(id);
        entries.reserve(slot_limit);
        jobs.reserve(queue_limit);
        retired.reserve(slot_limit);
        threads.reserve(workers);
        try {
            for (unsigned i=0; i<workers; ++i) threads.emplace_back([this] { worker(); });
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~State() { shutdown(); }

    void shutdown() {
        {
            const std::scoped_lock lock(mutex);
            done = true;
            jobs.clear();
        }
        wake.notify_all();
        threads.clear();
    }

    Entry& checked(SceneHandle h) {
        if (h.owner != owner || h.slot>=entries.size()) throw std::runtime_error("Stale scene handle");
        return entries[h.slot];
    }
    void enqueue(std::uint16_t slot,unsigned priority) {
        if (priority>100) throw std::runtime_error("Scene stream priority limit");
        auto& entry = entries[slot];
        const auto old = std::ranges::find(jobs,slot,&Job::slot);
        if (old == jobs.end() && jobs.size()==queue_limit) throw std::runtime_error("Scene stream queue full");
        Job job{slot,priority,entry.revision+1,entry.name};
        if (old == jobs.end()) jobs.push_back(std::move(job));
        else *old = std::move(job);
        ++entry.revision;
        entry.state = StreamState::queued;
        entry.error.clear();
        wake.notify_all();
    }

    void prune() {
        std::erase_if(retired,[&](const auto& record) {
            if (!record.scene.expired()) return false;
            charged -= record.bytes;
            return true;
        });
    }

    bool admit(std::uint16_t slot,std::shared_ptr<const Scene> scene,std::size_t bytes) {
        prune();
        auto& entry = entries[slot];
        const bool pinned = entry.version && entry.version.use_count()>1;
        if (bytes>budget || (pinned && retired.size()==slot_limit)) return false;
        const auto reclaim = pinned ? 0 : entry.bytes;
        while (charged-reclaim+bytes>budget) {
            Entry* victim = nullptr;
            for (auto& candidate : entries) {
                if (&candidate==&entry || !candidate.version || candidate.version.use_count()>1 || pending(candidate.state)) continue;
                if (!victim || victim->touched>candidate.touched) victim = &candidate;
            }
            if (!victim) return false;
            charged -= victim->bytes;
            victim->bytes = 0;
            victim->version.reset();
            victim->state = StreamState::evicted;
        }
        if (pinned) retired.push_back({entry.version,entry.bytes});
        charged = charged-reclaim+bytes;
        entry.bytes = bytes;
        entry.version = std::move(scene);
        entry.touched = ++clock;
        return true;
    }

    void worker() {
        std::unique_lock lock(mutex);
        while (true) {
            wake.wait(lock,[&] { return done || !jobs.empty(); });
            if (done) return;
            const auto found = std::ranges::max_element(jobs,{},&Job::priority);
            Job job = std::move(*found);
            jobs.erase(found);
            entries[job.slot].state = StreamState::loading;
            lock.unlock();
            std::shared_ptr<const Scene> value;
            std::string error;
            try {
                const auto path = root/job.name;
                if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path)) {
                    throw std::runtime_error("Missing scene asset " + job.name);
                }
                value = std::make_shared<const Scene>(parse_scene(read_bounded(path,raw_limit)));
            } catch (const std::exception& issue) {
                error = issue.what();
            }
            const auto bytes = value ? estimate(*value) : 0;
            lock.lock();
            auto& entry = entries[job.slot];
            if (done || entry.revision != job.revision) continue;
            if (value && admit(job.slot,std::move(value),bytes)) {
                entry.state = StreamState::ready;
            } else {
                entry.state = StreamState::failed;
                entry.error = error.empty() ? "Scene residency budget exceeded" : std::move(error);
            }
            wake.notify_all();
        }
    }

    std::filesystem::path root;
    std::size_t budget{}, charged{};
    std::uint64_t clock{};
    std::uint16_t owner{};
    std::vector<Entry> entries;
    std::vector<Job> jobs;
    std::vector<Retired> retired;
    std::vector<std::jthread> threads;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool done{};
};

SceneStream::SceneStream(std::filesystem::path root,std::size_t budget,unsigned workers)
    : state_(std::make_unique<State>(std::move(root),budget,workers)) {}
SceneStream::~SceneStream() = default;

SceneHandle SceneStream::request(std::string_view name,unsigned priority) {
    if (!valid_name(name)) throw std::runtime_error("Invalid scene asset name");
    auto& state = *state_;
    const std::scoped_lock lock(state.mutex);
    const auto found = std::ranges::find(state.entries,name,&State::Entry::name);
    if (found != state.entries.end()) {
        const auto slot = static_cast<std::uint16_t>(found-state.entries.begin());
        if (!pending(found->state) && !found->version) state.enqueue(slot,priority);
        return {slot,state.owner};
    }
    if (state.entries.size()==slot_limit) throw std::runtime_error("Scene stream slot limit");
    const auto slot = static_cast<std::uint16_t>(state.entries.size());
    state.entries.push_back({.name=std::string(name),.version={},.error={}});
    try { state.enqueue(slot,priority); }
    catch (...) { state.entries.pop_back(); throw; }
    return {slot,state.owner};
}

void SceneStream::reload(SceneHandle handle,unsigned priority) {
    auto& state = *state_;
    const std::scoped_lock lock(state.mutex);
    (void)state.checked(handle);
    state.enqueue(handle.slot,priority);
}

void SceneStream::cancel(SceneHandle handle) {
    auto& state = *state_;
    const std::scoped_lock lock(state.mutex);
    auto& entry = state.checked(handle);
    ++entry.revision;
    entry.state = StreamState::cancelled;
    std::erase_if(state.jobs,[handle](const auto& job) { return job.slot==handle.slot; });
    state.wake.notify_all();
}

std::shared_ptr<const Scene> SceneStream::get(SceneHandle handle) {
    auto& state = *state_;
    const std::scoped_lock lock(state.mutex);
    auto& entry = state.checked(handle);
    entry.touched = ++state.clock;
    return entry.version;
}

std::shared_ptr<const Scene> SceneStream::wait(SceneHandle handle,std::chrono::milliseconds timeout) {
    auto& state = *state_;
    std::unique_lock lock(state.mutex);
    auto& entry = state.checked(handle);
    if (!state.wake.wait_for(lock,timeout,[&] { return !pending(entry.state); })) {
        throw std::runtime_error("Scene streaming timed out");
    }
    entry.touched = ++state.clock;
    return entry.version;
}

StreamInfo SceneStream::inspect(SceneHandle handle) const {
    const std::scoped_lock lock(state_->mutex);
    const auto& entry = state_->checked(handle);
    return {entry.state,entry.bytes,entry.error};
}

std::size_t SceneStream::resident_bytes() const {
    const std::scoped_lock lock(state_->mutex);
    state_->prune();
    return state_->charged;
}

}
