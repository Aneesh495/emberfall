#include "net_bytes.hpp"

namespace ember::net {
namespace {

constexpr std::uint32_t magic = 0x52424d45; // EMBR in wire order.

}

bool newer(std::uint32_t candidate,std::uint32_t reference) {
    const auto distance = candidate-reference;
    return distance != 0 && distance < 0x80000000U;
}

Bytes encode_packet(const Packet& packet) {
    if (packet.payload.size() > datagram_limit-packet_overhead || !packet.sequence ||
        static_cast<unsigned>(packet.type)>static_cast<unsigned>(Type::disconnect)) {
        throw std::runtime_error("Packet outside wire bounds");
    }
    Writer out;
    out.bytes.reserve(packet_overhead+packet.payload.size());
    out.u32(magic);
    out.u8(1);
    out.u8(static_cast<std::uint8_t>(packet.type));
    out.u16(static_cast<std::uint16_t>(packet.payload.size()));
    out.u64(packet.token);
    out.u32(packet.sequence);
    out.u32(packet.ack);
    out.u32(packet.ack_bits);
    out.bytes.insert(out.bytes.end(),packet.payload.begin(),packet.payload.end());
    return std::move(out.bytes);
}

std::optional<Packet> decode_packet(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < packet_overhead || bytes.size() > datagram_limit) return std::nullopt;
    Reader in{bytes};
    if (in.u32() != magic || in.u8() != 1) return std::nullopt;
    const auto type = in.u8();
    const auto length = in.u16();
    if (type>static_cast<unsigned>(Type::disconnect) || length+packet_overhead != bytes.size()) return std::nullopt;
    Packet packet{static_cast<Type>(type),in.u64(),in.u32(),in.u32(),in.u32(),{}};
    if (!packet.sequence) return std::nullopt;
    packet.payload.assign(in.bytes.begin(),in.bytes.end());
    return packet;
}

Bytes encode_frame(const ServerFrame& frame) {
    if (frame.level > 2 || !frame.epoch || frame.simulation.devices.size() > frame_device_limit) {
        throw std::runtime_error("Server frame exceeds wire contract");
    }
    Writer out;
    out.u32(frame.epoch);
    out.u32(frame.acknowledged);
    out.u8(static_cast<std::uint8_t>(frame.level));
    const auto& s = frame.simulation;
    out.u64(s.ticks);
    out.u32(s.checkpoint);
    out.flag(s.completed);
    for (const auto& p : s.players) {
        out.vector(p.body.feet);
        out.vector(p.body.velocity);
        out.u32(p.body.ground);
        out.real(p.body.height);
        out.real(p.facing,10000);
        out.real(p.stride,10000);
    }
    out.u16(static_cast<std::uint16_t>(s.devices.size()));
    for (const auto& device : s.devices) {
        out.u32(device.id);
        out.vector(device.position);
        out.real(device.yaw,10000);
        out.real(device.state.travel,1000000);
        out.u32(device.state.ticks);
        out.flag(device.state.active);
    }
    return std::move(out.bytes);
}

ServerFrame decode_frame(std::span<const std::uint8_t> bytes) {
    Reader in{bytes};
    ServerFrame result;
    result.epoch = in.u32();
    result.acknowledged = in.u32();
    result.level = in.u8();
    if (!result.epoch || result.level > 2) throw std::runtime_error("Invalid server frame identity");
    auto& s = result.simulation;
    s.ticks = in.u64();
    s.checkpoint = in.u32();
    s.completed = in.flag();
    for (auto& p : s.players) {
        p.body.feet = in.vector();
        p.body.velocity = in.vector();
        p.body.ground = in.u32();
        p.body.height = in.real();
        p.facing = in.real(10000);
        p.stride = in.real(10000);
    }
    const auto count = in.u16();
    if (count > frame_device_limit || in.bytes.size() != static_cast<std::size_t>(count)*29) {
        throw std::runtime_error("Invalid server device count");
    }
    s.devices.reserve(count);
    SceneId previous{};
    for (unsigned i=0; i<count; ++i) {
        DeviceState d;
        d.id = in.u32();
        if (d.id <= previous) throw std::runtime_error("Device IDs out of order");
        previous = d.id;
        d.position = in.vector();
        d.yaw = in.real(10000);
        d.state.travel = in.real(1000000);
        d.state.ticks = in.u32();
        d.state.active = in.flag();
        s.devices.push_back(d);
    }
    in.end();
    return result;
}

}
