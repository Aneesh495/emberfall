#include "emberfall/scene_stream.hpp"

#include <stdexcept>
#include <unistd.h>

int main(int argc,char** argv) {
    using namespace ember;
    using namespace std::chrono_literals;
    if (argc!=2) throw std::runtime_error("Pass source scene");
    const auto temporary = std::filesystem::temp_directory_path()/("ember-stream-"+std::to_string(getpid()));
    std::filesystem::create_directory(temporary);
    const auto path = temporary/"test.scene";
    const auto source = load_scene(argv[1]);
    write_atomic(path,write_scene(source));
    SceneStream stream(temporary,64*1024,1);
    const auto handle = stream.request("test.scene",12);
    auto before = stream.wait(handle,3s);
    if (!before || *before != source) throw std::runtime_error("Original stream parse failed");
    write_atomic(path,"corrupt\n");
    stream.reload(handle,75);
    if (stream.wait(handle,3s) != before || stream.inspect(handle).state != StreamState::failed) {
        throw std::runtime_error("Failed reload replaced live version");
    }
    auto changed = source;
    changed.title = "Updated room";
    write_atomic(path,write_scene(changed));
    stream.reload(handle);
    const auto after = stream.wait(handle,3s);
    if (!after || *after != changed || *before != source || stream.request("test.scene") != handle) {
        throw std::runtime_error("Stable handle or leased asset was invalidated");
    }
    const auto charged = stream.resident_bytes();
    before.reset();
    if (stream.resident_bytes() >= charged) throw std::runtime_error("Released old asset remained charged");
    stream.cancel(handle);
    if (stream.inspect(handle).state != StreamState::cancelled || stream.get(handle) != after) {
        throw std::runtime_error("Cancellation invalidated the published version");
    }
    const auto missing = stream.request("missing.scene");
    if (stream.wait(missing,3s) || stream.inspect(missing).error.empty()) throw std::runtime_error("Missing asset accepted");
    std::filesystem::remove_all(temporary);
}
