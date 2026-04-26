#include "emberfall/campaign.hpp"

#include <stdexcept>
#include <unistd.h>

int main(int argc, char** argv) {
    using namespace ember;
    if (argc != 2) throw std::runtime_error("Missing level directory");
    const auto path = std::filesystem::temp_directory_path()/("ember-test-" + std::to_string(getpid()) + ".save");
    const Campaign campaign(argv[1]);
    const auto progress = Progress{1,16,false};
    campaign.save_progress(path,progress);
    if (campaign.read_progress(path) != progress) throw std::runtime_error("Save mismatch");
    const auto loaded = campaign.load(progress);
    if (loaded->checkpoint() != 16 || loaded->player(0).body.feet.z != -5) {
        throw std::runtime_error("Checkpoint mismatches save");
    }
    const auto v1 = "ember-progress 1 " + std::to_string(campaign.revision()) + " 1 16\n";
    write_atomic(path,v1 + std::to_string(fingerprint(v1)) + "\n");
    if (campaign.read_progress(path) != progress) throw std::runtime_error("Save migration mismatch");
    write_atomic(path,v1);
    bool rejected = false;
    try { (void)campaign.read_progress(path); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Truncated save accepted");
    const Settings settings{0.4f,1.3f,true,false,true};
    save_settings(path,settings);
    if (read_settings(path) != settings) throw std::runtime_error("Settings mismatch");
    std::filesystem::remove(path);
}
