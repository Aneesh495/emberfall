#pragma once

#include "emberfall/scene.hpp"

#include <chrono>
#include <memory>

namespace ember {

struct SceneHandle {
    std::uint16_t slot{}, owner{};
    friend bool operator==(SceneHandle,SceneHandle) = default;
};

enum class StreamState { queued, loading, ready, failed, cancelled, evicted };
struct StreamInfo {
    StreamState state{};
    std::size_t resident_bytes{};
    std::string error;
};

class SceneStream {
public:
    SceneStream(std::filesystem::path directory, std::size_t budget, unsigned workers = 2);
    ~SceneStream();
    SceneStream(const SceneStream&) = delete;
    SceneStream& operator=(const SceneStream&) = delete;

    SceneHandle request(std::string_view name, unsigned priority = 0);
    void reload(SceneHandle handle,unsigned priority = 0);
    void cancel(SceneHandle handle);
    std::shared_ptr<const Scene> get(SceneHandle handle);
    std::shared_ptr<const Scene> wait(SceneHandle handle, std::chrono::milliseconds timeout);
    StreamInfo inspect(SceneHandle handle) const;
    std::size_t resident_bytes() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

}
