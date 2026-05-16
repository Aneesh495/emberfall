#pragma once

#include "emberfall/game.hpp"

#include <deque>
#include <memory>

namespace ember::net {

using Bytes = std::vector<std::uint8_t>;
enum class Type : std::uint8_t { receipt, hello, welcome, input, frame, disconnect };

struct Packet {
    Type type{};
    std::uint64_t token{};
    std::uint32_t sequence{}, ack{}, ack_bits{};
    Bytes payload;
};

constexpr std::size_t datagram_limit = 1200;
constexpr std::size_t packet_overhead = 28;
Bytes encode_packet(const Packet& packet);
std::optional<Packet> decode_packet(std::span<const std::uint8_t> bytes);
bool newer(std::uint32_t candidate, std::uint32_t reference);

struct InputFrame {
    std::uint32_t epoch{}, order{}, jump{}, use{};
    Command command;
};

struct ServerFrame {
    std::uint32_t epoch{}, acknowledged{};
    unsigned level{};
    SimulationState simulation;
};

Bytes encode_frame(const ServerFrame& frame);
ServerFrame decode_frame(std::span<const std::uint8_t> bytes);

struct Statistics {
    std::uint64_t sent{}, received{}, duplicate{}, rejected{}, resent{};
};

struct Faults {
    unsigned delay_ms{}, jitter_ms{}, loss_percent{};
    unsigned seed{1};
};

class Session {
public:
    static std::unique_ptr<Session> host(std::uint16_t port, std::uint64_t key, std::uint64_t revision, Faults faults = {});
    static std::unique_ptr<Session> join(std::string_view ipv4, std::uint16_t port, std::uint64_t key,
                                         std::uint64_t revision, Faults faults = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    std::uint16_t port() const;
    void poll(std::uint64_t now_ms);
    void set_epoch(std::uint32_t epoch);
    void input(const InputFrame& input);
    Command remote_command();
    std::uint32_t last_input() const;
    void broadcast(const ServerFrame& frame);
    std::optional<ServerFrame> receive();
    bool connected() const;
    const Statistics& stats() const;

private:
    struct State;
    explicit Session(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

}
