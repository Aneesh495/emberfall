#include "emberfall/campaign.hpp"
#include "emberfall/net.hpp"

#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>

using namespace ember;
using namespace ember::net;

void require(bool condition,std::string reason) {
    if (!condition) throw std::runtime_error(std::move(reason));
}

class Exercise {
public:
    explicit Exercise(const Campaign& campaign)
        : campaign_(campaign), host_(Session::host(0,71,campaign.revision(),{50,20,2,174})) {
        connect();
    }

    void connect() {
        client_ = Session::join("127.0.0.1",host_->port(),71,campaign_.revision(),{50,20,2,472});
        for (unsigned i=0; i<200 && (!client_->connected() || !host_->connected()); ++i) clock();
        require(client_->connected() && host_->connected(),"Handshake did not complete");
        order_ = jump_ = use_ = 0;
    }

    void disconnect() {
        client_.reset();
        for (unsigned i=0; i<270; ++i) clock();
        require(!host_->connected() && host_->remote_command().move == Vec3{},"Stale input after peer timeout");
        connect();
    }

    void level(unsigned level) {
        level_ = level;
        ++epoch_;
        host_->set_epoch(epoch_);
        order_ = jump_ = use_ = 0;
        auto scene = campaign_.scene(level);
        if (level == 0) {
            for (SceneId i=100; i<183; ++i) {
                scene.nodes.emplace(i,Node{.id=i,.kind=Kind::crate,.name="Fragment exercise",
                                           .position={40+static_cast<float>(i),0.5f,20}});
            }
        }
        game_ = std::make_unique<Game>(scene);
        observed_ = std::make_unique<Game>(scene);
        for (unsigned i=0; i<25; ++i) tick();
        require(seen_epoch_ == epoch_,"Client has no snapshot for the new level");
    }

    void tick(Command first={}, Command second={}) {
        clock();
        if (second.jump) ++jump_;
        if (second.interact) ++use_;
        client_->input({epoch_,++order_,jump_,use_,second});
        game_->tick({first,host_->remote_command()});
        for (const auto event : game_->events()) {
            if (event.signal == Signal::mirror) ++mirrors_;
            if (event.signal == Signal::exit) ++exits_;
            events_ += std::to_string(time_) + "," + std::to_string(epoch_) + "," + std::to_string(level_) + "," +
                       std::to_string(static_cast<unsigned>(event.signal)) + "," + std::to_string(event.subject) + "\n";
        }
        if (order_%3 == 0) host_->broadcast({epoch_,host_->last_input(),level_,game_->snapshot()});
        if (auto frame = client_->receive()) {
            if (frame->epoch != epoch_) return;
            require(frame->level == level_,"Client observed the wrong level");
            observed_->apply_snapshot(frame->simulation);
            seen_epoch_ = frame->epoch;
        }
    }

    void hold(unsigned ticks) { for (unsigned i=0; i<ticks; ++i) tick(); }

    void move(unsigned player,Vec3 target) {
        unsigned still = 0;
        for (unsigned i=0; i<720; ++i) {
            auto offset = target - (player == 0 ? game_->player(player) : observed_->player(player)).body.feet;
            offset.y = 0;
            if (game_->completed()) return;
            if (length(offset) < 0.07f) {
                tick();
                if (++still == 15) return;
                continue;
            }
            still = 0;
            const auto movement = unit(offset)*std::min(1.0f,length(offset)*1.6f);
            tick(player == 0 ? Command{movement} : Command{},player == 1 ? Command{movement} : Command{});
        }
        const auto feet = game_->player(player).body.feet;
        throw std::runtime_error("Network movement stalled in level " + std::to_string(level_) +
                                 " slot=" + std::to_string(player) + " x=" + std::to_string(feet.x) +
                                 " z=" + std::to_string(feet.z) + " target x=" + std::to_string(target.x) +
                                 " z=" + std::to_string(target.z));
    }

    void check_ferry_support(SceneId support) const {
        require(game_->player(0).body.ground == support,"Ferry passenger support mismatch");
    }

    void check_level() {
        hold(35);
        require(game_->completed() && observed_->completed(),"Server or client did not reach completion");
    }

    void check_all(const std::filesystem::path& trace) const {
        require(mirrors_ == 1 && exits_ == 3,"Irreversible events did not occur exactly once");
        if (!trace.empty()) write_atomic(trace,"time_ms,epoch,level,signal,subject\n"+events_);
        std::cout << "completed=3 mirror_events=" << mirrors_ << " exit_events=" << exits_
                  << " sent=" << host_->stats().sent << " received=" << host_->stats().received
                  << " duplicates=" << host_->stats().duplicate << " rejected=" << host_->stats().rejected << '\n';
    }

private:
    void clock() {
        time_ += 16;
        host_->poll(time_);
        if (client_) client_->poll(time_);
        // Give real sockets time to deliver each scheduled virtual-clock burst.
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        host_->poll(time_);
    }

    const Campaign& campaign_;
    std::unique_ptr<Session> host_, client_;
    std::unique_ptr<Game> game_, observed_;
    std::string events_;
    std::uint64_t time_{};
    unsigned level_{}, epoch_{}, seen_epoch_{}, order_{}, jump_{}, use_{}, mirrors_{}, exits_{};
};

int main(int argc,char** argv) {
    if (argc<2 || argc>3) throw std::runtime_error("Pass level directory and optional event trace");
    require(newer(1,0xffffffffU) && !newer(0xffffffffU,1),"Sequence wrap mismatch");
    auto wire = encode_packet({Type::receipt,22,1,0,0,{}});
    require(wire.size()==28 && wire[0]=='E' && wire[1]=='M' && decode_packet(wire).has_value(),"Wire header mismatch");
    wire[6] = 3;
    require(!decode_packet(wire),"Bad packet length accepted");
    std::mt19937 random(8217);
    Bytes bytes;
    for (int i=0; i<12'000; ++i) {
        bytes.resize(random()%1300);
        for (auto& b : bytes) b = static_cast<std::uint8_t>(random());
        if (auto packet = decode_packet(bytes)) {
            require(encode_packet(*packet) == bytes,"Decoded noncanonical packet");
        }
    }

    const Campaign campaign(argv[1]);
    Exercise network(campaign);
    network.level(0);
    network.move(0,{-2,0,3});
    network.move(1,{2,0,3});
    network.hold(40);
    network.move(0,{0,0,1.4f});
    network.move(0,{0,0,-9});
    network.check_level();

    network.disconnect();
    network.level(1);
    network.move(0,{2,0,6.8f});
    network.hold(35);
    network.move(1,{-1.3f,0,3.2f});
    network.move(1,{-1.3f,0,2.2f});
    network.tick({},Command{.move={},.interact=true});
    network.hold(45);
    network.move(1,{0,0,-9.8f});
    network.check_level();

    network.level(2);
    network.move(0,{0,0,3.55f});
    network.check_ferry_support(3);
    network.move(1,{2.8f,0,7});
    network.hold(285);
    network.check_ferry_support(2);
    network.move(0,{0,0,-11});
    network.check_level();
    network.check_all(argc == 3 ? argv[2] : "");
}
