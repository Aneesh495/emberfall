#include "net_bytes.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <random>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

namespace ember::net {
namespace {

constexpr std::uint64_t timeout = 3500;
constexpr std::uint64_t retry = 135;
constexpr std::size_t pending_limit = 24;
constexpr std::size_t delay_limit = 256;

bool same(sockaddr_in a,sockaddr_in b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

class Socket {
public:
    explicit Socket(std::uint16_t port) {
        fd_ = ::socket(AF_INET,SOCK_DGRAM,0);
        if (fd_ < 0) fail("Create UDP socket");
        try {
            if (fcntl(fd_,F_SETFL,O_NONBLOCK) < 0 || fcntl(fd_,F_SETFD,FD_CLOEXEC) < 0) fail("Set UDP flags");
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            local.sin_port = htons(port);
            if (::bind(fd_,reinterpret_cast<const sockaddr*>(&local),sizeof(local)) < 0) fail("Bind UDP socket");
            socklen_t length = sizeof(local);
            if (::getsockname(fd_,reinterpret_cast<sockaddr*>(&local),&length) < 0) fail("Read UDP port");
            port_ = ntohs(local.sin_port);
        } catch (...) {
            ::close(fd_);
            throw;
        }
    }
    ~Socket() { ::close(fd_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    std::uint16_t port() const { return port_; }
    void send(std::span<const std::uint8_t> bytes,sockaddr_in target) const {
        const auto sent = ::sendto(fd_,bytes.data(),bytes.size(),0,reinterpret_cast<const sockaddr*>(&target),sizeof(target));
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS && errno != EINTR) fail("Send UDP packet");
    }
    std::size_t receive(std::span<std::uint8_t> bytes,sockaddr_in& source) const {
        socklen_t length = sizeof(source);
        const auto received = ::recvfrom(fd_,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&source),&length);
        if (received >= 0) return static_cast<std::size_t>(received);
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) fail("Receive UDP packet");
        return 0;
    }
private:
    [[noreturn]] static void fail(const char* what) { throw std::system_error(errno,std::generic_category(),what); }
    int fd_{};
    std::uint16_t port_{};
};

struct Recent {
    std::uint32_t high{}, mask{};

    bool accept(std::uint32_t sequence) {
        if (!high) {
            high = sequence;
            return true;
        }
        if (sequence == high) return false;
        if (newer(sequence,high)) {
            const auto step = sequence-high;
            mask = step>32 ? 0 : (step==32 ? 0 : mask<<step) | (1U<<(step-1));
            high = sequence;
            return true;
        }
        const auto distance = high-sequence;
        if (distance>32 || (mask & (1U<<(distance-1)))) return false;
        mask |= 1U<<(distance-1);
        return true;
    }

    static bool acknowledged(std::uint32_t sequence, std::uint32_t high, std::uint32_t mask) {
        if (!high) return false;
        const auto distance = high-sequence;
        return distance == 0 || (distance <= 32 && (mask & (1U<<(distance-1))));
    }
};

std::uint32_t increment(std::uint32_t& v) {
    if (++v == 0) ++v;
    return v;
}

Bytes revision_bytes(std::uint64_t revision) {
    Writer writer;
    writer.u64(revision);
    return std::move(writer.bytes);
}

}

struct Session::State {
    struct Pending {
        Packet packet;
        std::uint64_t last{};
        unsigned attempts{};
    };
    struct Delayed {
        Bytes data;
        sockaddr_in target{};
        std::uint64_t due{};
    };
    struct Fragment {
        std::uint32_t id{};
        std::uint8_t total{};
        std::uint64_t received{};
        std::array<Bytes,fragment_limit> parts;
    };

    State(std::uint16_t port, std::uint64_t key, std::uint64_t revision, bool host, Faults faults)
        : socket(port), token(key), revision(revision), hosting(host), faults(faults), random(faults.seed) {
        if (faults.delay_ms>500 || faults.jitter_ms>250 || faults.loss_percent>30) {
            throw std::runtime_error("Fault simulation outside bounded range");
        }
    }

    void dispatch(const Packet& packet) {
        if (!peer) return;
        auto payload = encode_packet(packet);
        ++counters.sent;
        if (faults.loss_percent && random()%100 < faults.loss_percent) return;
        if (faults.delay_ms || faults.jitter_ms) {
            if (delayed.size() == delay_limit) { ++counters.rejected; return; }
            const auto jitter = static_cast<int>(random()%(2*faults.jitter_ms+1))-static_cast<int>(faults.jitter_ms);
            const auto delay = std::max(0,static_cast<int>(faults.delay_ms)+jitter);
            delayed.push_back({std::move(payload),*peer,now+static_cast<std::uint64_t>(delay)});
        } else socket.send(payload,*peer);
    }

    void send(Type type, Bytes payload = {}, bool reliable = false) {
        if (!peer) return;
        Packet packet{type,token,increment(next),incoming.high,incoming.mask,std::move(payload)};
        if (reliable) {
            if (pending.size() == pending_limit) { ++counters.rejected; return; }
            pending.push_back({packet,now,1});
        }
        dispatch(packet);
        last_out = now;
    }

    void maintenance() {
        for (auto it=delayed.begin(); it!=delayed.end();) {
            if (it->due>now) { ++it; continue; }
            socket.send(it->data,it->target);
            it = delayed.erase(it);
        }
        for (auto it=pending.begin(); it!=pending.end();) {
            if (it->attempts>=12 && now-it->last>=retry) { it = pending.erase(it); continue; }
            if (now-it->last>=retry) {
                it->packet.ack = incoming.high;
                it->packet.ack_bits = incoming.mask;
                dispatch(it->packet);
                ++it->attempts;
                ++counters.resent;
                it->last = now;
            }
            ++it;
        }
        if (ready && now-last_in > timeout) reset();
        if (!hosting && !ready && now-last_hello >= 500) {
            send(Type::hello,revision_bytes(revision),true);
            last_hello = now;
        }
        if (ready && now-last_out >= 100) send(Type::receipt);
    }

    void reset() {
        ready = false;
        input_seen = false;
        last_order = last_jump = last_use = 0;
        command = {};
        pending.clear();
        delayed.clear();
        incoming = {};
        fragments = {};
        latest.reset();
        accepted_frame = 0;
        if (hosting) peer.reset();
    }

    void read_input(Reader reader) {
        const auto target_epoch = reader.u32();
        const auto order = reader.u32();
        const auto jump = reader.u32();
        const auto use = reader.u32();
        auto x = reader.real();
        auto z = reader.real();
        const bool crouch = reader.flag();
        reader.end();
        if (std::abs(x)>1 || std::abs(z)>1 || !order) throw std::runtime_error("Invalid input direction");
        if (target_epoch != epoch || (input_seen && !newer(order,last_order))) return;
        if (input_seen && (order-last_order > 1024 || jump-last_jump > 128 || use-last_use > 128)) {
            throw std::runtime_error("Input skipped outside history");
        }
        command.jump = command.jump || ((input_seen || order <= 8) && jump != last_jump);
        command.interact = command.interact || ((input_seen || order <= 8) && use != last_use);
        command.move = {x,0,z};
        command.crouch = crouch;
        last_order = order;
        last_jump = jump;
        last_use = use;
        input_seen = true;
        last_input_time = now;
    }

    void read_fragment(Reader reader) {
        const auto id = reader.u32();
        const auto part = reader.u8();
        const auto count = reader.u8();
        if (!id || !count || count>fragment_limit || part>=count || reader.bytes.empty() ||
            reader.bytes.size()>fragment_size || (part+1<count && reader.bytes.size()!=fragment_size)) {
            throw std::runtime_error("Invalid snapshot fragment");
        }
        if (accepted_frame && !newer(id,accepted_frame)) return;
        auto& bucket = fragments[id%fragments.size()];
        if (bucket.id != id) {
            if (bucket.id && !newer(id,bucket.id)) return;
            bucket = {id,count,0,{}};
        }
        if (bucket.total != count) throw std::runtime_error("Inconsistent fragment group");
        const auto flag = std::uint64_t{1}<<part;
        if (bucket.received & flag) return;
        bucket.parts[part].assign(reader.bytes.begin(),reader.bytes.end());
        bucket.received |= flag;
        if (bucket.received != (std::uint64_t{1}<<count)-1) return;
        Bytes whole;
        whole.reserve(count*fragment_size);
        for (unsigned i=0; i<count; ++i) whole.insert(whole.end(),bucket.parts[i].begin(),bucket.parts[i].end());
        latest = decode_frame(whole);
        accepted_frame = id;
        bucket = {};
    }

    void read(Packet& packet,sockaddr_in source) {
        if (packet.token != token || (peer && !same(*peer,source))) { ++counters.rejected; return; }
        if (!peer) {
            if (!hosting || packet.type != Type::hello || packet.payload != revision_bytes(revision)) {
                ++counters.rejected;
                return;
            }
            peer = source;
        }
        std::erase_if(pending,[&](const auto& entry) {
            return Recent::acknowledged(entry.packet.sequence,packet.ack,packet.ack_bits);
        });
        if (!incoming.accept(packet.sequence)) { ++counters.duplicate; return; }
        ++counters.received;
        last_in = now;
        Reader data{packet.payload};
        switch (packet.type) {
        case Type::hello:
            if (!hosting || packet.payload != revision_bytes(revision)) throw std::runtime_error("Wrong hello");
            ready = true;
            send(Type::welcome,revision_bytes(revision),true);
            break;
        case Type::welcome:
            if (hosting || packet.payload != revision_bytes(revision)) throw std::runtime_error("Wrong welcome");
            ready = true;
            break;
        case Type::input:
            if (!hosting || !ready) return;
            read_input(data);
            break;
        case Type::frame:
            if (hosting || !ready) return;
            read_fragment(data);
            break;
        case Type::receipt:
            data.end();
            break;
        case Type::disconnect:
            data.end();
            reset();
            break;
        }
    }

    Socket socket;
    std::uint64_t token{}, revision{};
    bool hosting{}, ready{}, input_seen{};
    Faults faults;
    std::mt19937 random;
    std::optional<sockaddr_in> peer;
    std::uint64_t now{}, last_in{}, last_out{}, last_hello{}, last_input_time{};
    std::uint32_t next{}, message_id{}, epoch{}, accepted_frame{};
    std::uint32_t last_order{}, last_jump{}, last_use{};
    Recent incoming;
    Statistics counters;
    Command command;
    std::vector<Pending> pending;
    std::vector<Delayed> delayed;
    std::array<Fragment,4> fragments;
    std::optional<ServerFrame> latest;
};

Session::Session(std::unique_ptr<State> state) : state_(std::move(state)) {}
Session::~Session() = default;

std::unique_ptr<Session> Session::host(std::uint16_t port,std::uint64_t key,std::uint64_t revision,Faults faults) {
    return std::unique_ptr<Session>(new Session(std::make_unique<State>(port,key,revision,true,faults)));
}

std::unique_ptr<Session> Session::join(std::string_view ipv4,std::uint16_t port,std::uint64_t key,
                                       std::uint64_t revision,Faults faults) {
    auto state = std::make_unique<State>(0,key,revision,false,faults);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET,std::string(ipv4).c_str(),&address.sin_addr) != 1 || !port) {
        throw std::runtime_error("Join requires an IPv4 address and a nonzero port");
    }
    state->peer = address;
    state->send(Type::hello,revision_bytes(revision),true);
    return std::unique_ptr<Session>(new Session(std::move(state)));
}

std::uint16_t Session::port() const { return state_->socket.port(); }
bool Session::connected() const { return state_->ready; }
const Statistics& Session::stats() const { return state_->counters; }
std::uint32_t Session::last_input() const { return state_->last_order; }

void Session::poll(std::uint64_t now_ms) {
    auto& state = *state_;
    state.now = now_ms;
    state.maintenance();
    std::array<std::uint8_t,datagram_limit+1> data{};
    for (unsigned i=0; i<96; ++i) {
        sockaddr_in source{};
        const auto received = state.socket.receive(data,source);
        if (!received) break;
        auto packet = decode_packet(std::span(data).first(received));
        if (!packet) { ++state.counters.rejected; continue; }
        try { state.read(*packet,source); }
        catch (const std::runtime_error&) { ++state.counters.rejected; }
    }
}

void Session::set_epoch(std::uint32_t epoch) {
    auto& state = *state_;
    state.epoch = epoch;
    state.last_order = state.last_jump = state.last_use = 0;
    state.input_seen = false;
    state.command = {};
}

void Session::input(const InputFrame& input) {
    if (!connected()) return;
    Writer out;
    out.u32(input.epoch);
    out.u32(input.order);
    out.u32(input.jump);
    out.u32(input.use);
    out.real(std::clamp(input.command.move.x,-1.0f,1.0f));
    out.real(std::clamp(input.command.move.z,-1.0f,1.0f));
    out.flag(input.command.crouch);
    state_->send(Type::input,std::move(out.bytes));
}

Command Session::remote_command() {
    auto& state = *state_;
    if (!state.ready || !state.input_seen || state.now-state.last_input_time > 250) return {};
    const auto result = state.command;
    state.command.jump = state.command.interact = false;
    return result;
}

void Session::broadcast(const ServerFrame& frame) {
    auto& state = *state_;
    if (!state.ready || !state.hosting) return;
    const auto bytes = encode_frame(frame);
    const auto parts = (bytes.size()+fragment_size-1)/fragment_size;
    if (!parts || parts>fragment_limit) throw std::runtime_error("Server frame exceeds fragmentation budget");
    const auto message = increment(state.message_id);
    for (std::size_t part=0; part<parts; ++part) {
        Writer chunk;
        chunk.u32(message);
        chunk.u8(static_cast<std::uint8_t>(part));
        chunk.u8(static_cast<std::uint8_t>(parts));
        const auto payload = std::span(bytes).subspan(part*fragment_size,std::min(fragment_size,bytes.size()-part*fragment_size));
        chunk.bytes.insert(chunk.bytes.end(),payload.begin(),payload.end());
        state.send(Type::frame,std::move(chunk.bytes));
    }
}

std::optional<ServerFrame> Session::receive() {
    return std::exchange(state_->latest,{});
}

}
