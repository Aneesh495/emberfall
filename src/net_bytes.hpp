#pragma once
#include "emberfall/net.hpp"

#include <bit>
#include <limits>

namespace ember::net {

struct Writer {
    Bytes bytes;
    void u8(std::uint8_t v) { bytes.push_back(v); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v>>8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v>>16));
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v>>32));
    }
    void flag(bool v) { u8(v ? 1 : 0); }
    void real(float v, float scale = 1000) {
        const auto quantized = std::round(static_cast<double>(v)*scale);
        if (!std::isfinite(quantized) || std::abs(quantized) > 2147483647) {
            throw std::runtime_error("Wire real outside bounds");
        }
        u32(std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(quantized)));
    }
    void vector(Vec3 v) { real(v.x); real(v.y); real(v.z); }
};

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::uint8_t u8() {
        if (bytes.empty()) throw std::runtime_error("Truncated wire payload");
        const auto value = bytes.front();
        bytes = bytes.subspan(1);
        return value;
    }
    std::uint16_t u16() {
        const auto low = u8();
        const auto high = u8();
        return static_cast<std::uint16_t>(low | (high<<8));
    }
    std::uint32_t u32() {
        const auto low = u16();
        const auto high = u16();
        return low | (static_cast<std::uint32_t>(high)<<16);
    }
    std::uint64_t u64() {
        const auto low = u32();
        const auto high = u32();
        return low | (static_cast<std::uint64_t>(high)<<32);
    }
    bool flag() {
        const auto v = u8();
        if (v>1) throw std::runtime_error("Noncanonical wire flag");
        return v != 0;
    }
    float real(float scale = 1000) { return static_cast<float>(std::bit_cast<std::int32_t>(u32()))/scale; }
    Vec3 vector() { return {real(),real(),real()}; }
    void end() const {
        if (!bytes.empty()) throw std::runtime_error("Trailing wire data");
    }
};

constexpr std::size_t frame_device_limit = 224;
constexpr std::size_t fragment_header_size = 6;
constexpr std::size_t fragment_size = datagram_limit-packet_overhead-fragment_header_size;
constexpr std::size_t fragment_limit = 8;

}
