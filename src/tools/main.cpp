#include "emberfall/editor.hpp"
#include "emberfall/replay.hpp"

#include <charconv>
#include <iostream>

namespace {
template<class T>
T number(std::string_view value) {
    T out{};
    const auto result = std::from_chars(value.data(),value.data()+value.size(),out);
    if (result.ec != std::errc{} || result.ptr != value.data()+value.size()) {
        throw std::runtime_error("Invalid argument: " + std::string(value));
    }
    return out;
}
}

int main(int argc,char** argv) {
    try {
        if (argc >= 4 && std::string_view(argv[1]) == "compose" && (argc-4)%5 == 0) {
            ember::SceneDocument document(ember::load_scene(argv[2]));
            for (int i=4; i<argc; i+=5) {
                const auto prefab = ember::load_scene(argv[i]);
                document.apply(ember::instantiate_prefab(prefab,number<ember::SceneId>(argv[i+1]),
                    {number<float>(argv[i+2]),number<float>(argv[i+3]),number<float>(argv[i+4])}));
            }
            const ember::Game validated(document.scene());
            ember::write_atomic(argv[3],ember::write_scene(document.scene()));
            return 0;
        }
        if (argc == 4 && std::string_view(argv[1]) == "replay-check") {
            const ember::Campaign campaign(argv[2]);
            const auto tape = ember::read_replay(argv[3]);
            const auto hash = ember::check_replay(campaign,tape);
            std::cout << "replay_ticks=" << tape.ticks.size() << " final_hash=" << hash << '\n';
            return 0;
        }
        std::cerr << "ember-tool compose INPUT OUTPUT [PREFAB ID X Y Z]...\n"
                     "ember-tool replay-check COOKED_LEVEL_DIRECTORY TAPE\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "ember-tool: " << error.what() << '\n';
        return 1;
    }
}
