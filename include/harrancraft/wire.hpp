#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace harrancraft::wire {
inline constexpr std::uint32_t magic = 0x31435248;
inline constexpr std::uint16_t version = 1;
inline constexpr std::size_t frame_bytes = 512;
inline constexpr std::size_t dimension_bytes = 64;
inline constexpr std::size_t state_bytes = 192;
inline constexpr std::uint32_t native_origin = 1;
inline constexpr std::uint32_t guest_origin = 2;

enum class Kind : std::uint16_t {
    hello = 1, welcome = 2, get = 3, set = 4, result = 5,
    event = 6, ping = 7, pong = 8, close = 9
};
enum class Status : std::uint32_t {
    ok = 0, stale_epoch = 1, invalid = 2, unavailable = 3,
    conflict = 4, forbidden = 5, retired_id = 6, capacity = 7,
    unsupported_state = 8
};
struct Frame {
    Kind kind = Kind::hello;
    std::uint64_t epoch = 0;
    std::uint64_t id = 0;
    std::uint32_t origin = native_origin;
    Status status = Status::ok;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint64_t cause = 0;
    std::string dimension, expected, state;
    bool operator==(const Frame&) const = default;
};
using Bytes = std::array<std::uint8_t, frame_bytes>;

inline void put(Bytes& b, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        b.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
    }
}
inline std::uint64_t get(const Bytes& b, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(b.at(offset + i)) << (i * 8);
    }
    return value;
}
inline void check(const Frame& f) {
    const auto kind = static_cast<std::uint16_t>(f.kind);
    if (kind < 1 || kind > 9 || static_cast<std::uint32_t>(f.status) > 8 ||
        (f.origin != native_origin && f.origin != guest_origin) ||
        f.epoch > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        f.id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        f.cause > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("Invalid wire fields");
    }
}
inline void put_text(Bytes& b, std::size_t at, std::size_t len_at,
                     std::size_t capacity, std::string_view text) {
    if (text.size() > capacity) throw std::invalid_argument("Wire text exceeds capacity");
    put(b, len_at, text.size(), 2);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 32 || c > 126) throw std::invalid_argument("Invalid wire text");
        b.at(at + i) = c;
    }
}
inline std::string read_text(const Bytes& b, std::size_t at,
                             std::size_t len_at, std::size_t capacity) {
    const auto len = static_cast<std::size_t>(get(b, len_at, 2));
    if (len > capacity) throw std::invalid_argument("Wire text exceeds capacity");
    std::string result;
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto c = b.at(at + i);
        if (i < len) {
            if (c < 32 || c > 126) throw std::invalid_argument("Invalid wire text");
            result.push_back(static_cast<char>(c));
        } else if (c != 0) {
            throw std::invalid_argument("Nonzero wire padding");
        }
    }
    return result;
}
inline Bytes encode(const Frame& f) {
    check(f);
    Bytes b{};
    put(b, 0, magic, 4); put(b, 4, version, 2);
    put(b, 6, static_cast<std::uint16_t>(f.kind), 2);
    put(b, 8, f.epoch, 8); put(b, 16, f.id, 8);
    put(b, 24, f.origin, 4); put(b, 28, static_cast<std::uint32_t>(f.status), 4);
    put(b, 32, static_cast<std::uint32_t>(f.x), 4);
    put(b, 36, static_cast<std::uint32_t>(f.y), 4);
    put(b, 40, static_cast<std::uint32_t>(f.z), 4);
    put(b, 52, f.cause, 8);
    put_text(b, 64, 44, dimension_bytes, f.dimension);
    put_text(b, 128, 46, state_bytes, f.expected);
    put_text(b, 320, 48, state_bytes, f.state);
    return b;
}
inline Frame decode(const Bytes& b) {
    if (get(b, 0, 4) != magic || get(b, 4, 2) != version ||
        get(b, 50, 2) != 0 || get(b, 60, 4) != 0) {
        throw std::invalid_argument("Invalid wire header");
    }
    Frame f;
    f.kind = static_cast<Kind>(get(b, 6, 2));
    f.epoch = get(b, 8, 8); f.id = get(b, 16, 8);
    f.origin = static_cast<std::uint32_t>(get(b, 24, 4));
    f.status = static_cast<Status>(get(b, 28, 4));
    // Unsigned-to-signed conversion is defined modulo 2^N in C++20.
    f.x = static_cast<std::int32_t>(get(b, 32, 4));
    f.y = static_cast<std::int32_t>(get(b, 36, 4));
    f.z = static_cast<std::int32_t>(get(b, 40, 4));
    f.cause = get(b, 52, 8);
    f.dimension = read_text(b, 64, 44, dimension_bytes);
    f.expected = read_text(b, 128, 46, state_bytes);
    f.state = read_text(b, 320, 48, state_bytes);
    check(f);
    return f;
}
} // namespace harrancraft::wire
