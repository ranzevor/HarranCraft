#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace harrancraft::texture {

static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);
inline constexpr std::uint32_t magic = 0x31544348;
inline constexpr std::uint16_t version = 1;
inline constexpr std::size_t header_bytes = 320;
inline constexpr std::uint16_t max_edge = 64;
inline constexpr std::size_t max_pixels = max_edge * max_edge;
inline constexpr std::size_t max_bytes = header_bytes + max_pixels * 4;

enum class Kind : std::uint16_t { snapshot = 1, retire = 2 };
struct Identity {
    std::uint64_t epoch = 0, world = 0, camera = 0, frame = 0, capture = 0;
    bool operator==(const Identity&) const = default;
};
struct Record {
    Kind kind = Kind::snapshot;
    Identity identity;
    std::string atlas, sprite;
    std::int32_t atlas_x = 0, atlas_y = 0;
    std::uint16_t width = 0, height = 0;
    float min_u = 0, max_u = 0, min_v = 0, max_v = 0;
    std::vector<std::uint32_t> argb;
    bool operator==(const Record&) const = default;
};
using Bytes = std::vector<std::uint8_t>;

inline void put(Bytes& b, std::size_t at, std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
        b.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
    }
}
inline std::uint64_t get(std::span<const std::uint8_t> b, std::size_t at, std::size_t bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        value |= static_cast<std::uint64_t>(b[at + i]) << (i * 8);
    }
    return value;
}
inline void check_text(std::string_view value, std::size_t capacity) {
    if (value.empty() || value.size() > capacity) {
        throw std::invalid_argument("Invalid texture identifier length");
    }
    for (const char c : value) {
        const auto octet = static_cast<unsigned char>(c);
        if (octet < 33 || octet > 126) throw std::invalid_argument("Invalid texture identifier");
    }
}
inline void check(const Record& r) {
    const auto& id = r.identity;
    constexpr auto max_signed = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if ((r.kind != Kind::snapshot && r.kind != Kind::retire)
        || id.epoch == 0 || id.world == 0 || id.camera == 0
        || id.frame == 0 || id.capture == 0
        || id.epoch > max_signed || id.world > max_signed || id.camera > max_signed
        || id.frame > max_signed || id.capture > max_signed) {
        throw std::invalid_argument("Invalid texture identity");
    }
    check_text(r.atlas, 96);
    check_text(r.sprite, 128);
    if (r.kind == Kind::snapshot) {
        if (r.atlas_x < 0 || r.atlas_y < 0
            || r.width == 0 || r.height == 0 || r.width > max_edge || r.height > max_edge
            || r.argb.size() != static_cast<std::size_t>(r.width) * r.height
            || !std::isfinite(r.min_u) || !std::isfinite(r.max_u)
            || !std::isfinite(r.min_v) || !std::isfinite(r.max_v)
            || r.min_u < 0 || r.max_u > 1 || r.min_v < 0 || r.max_v > 1
            || !(r.min_u < r.max_u && r.min_v < r.max_v)) {
            throw std::invalid_argument("Invalid texture dimensions or UV");
        }
    } else if (r.atlas_x != 0 || r.atlas_y != 0 || r.width != 0 || r.height != 0
        || std::bit_cast<std::uint32_t>(r.min_u) != 0
        || std::bit_cast<std::uint32_t>(r.max_u) != 0
        || std::bit_cast<std::uint32_t>(r.min_v) != 0
        || std::bit_cast<std::uint32_t>(r.max_v) != 0 || !r.argb.empty()) {
        throw std::invalid_argument("Retired sprite carries pixel data");
    }
}
inline void put_text(Bytes& bytes, std::size_t at, std::string_view str) {
    for (std::size_t i = 0; i < str.size(); ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(str[i]);
    }
}
inline std::string read_text(std::span<const std::uint8_t> b, std::size_t at,
                             std::size_t length, std::size_t capacity) {
    if (length > capacity) throw std::invalid_argument("Oversized texture identifier");
    std::string value;
    value.reserve(length);
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto ch = b[at + i];
        if (i < length) {
            if (ch < 33 || ch > 126) throw std::invalid_argument("Invalid texture ASCII");
            value.push_back(static_cast<char>(ch));
        } else if (ch != 0) {
            throw std::invalid_argument("Nonzero texture padding");
        }
    }
    return value;
}
inline Bytes encode(const Record& r) {
    check(r);
    Bytes bytes(header_bytes + r.argb.size() * 4, 0);
    put(bytes, 0, magic, 4); put(bytes, 4, version, 2);
    put(bytes, 6, static_cast<std::uint16_t>(r.kind), 2);
    put(bytes, 8, bytes.size(), 4);
    put(bytes, 16, r.identity.epoch, 8);
    put(bytes, 24, r.identity.world, 8);
    put(bytes, 32, r.identity.camera, 8);
    put(bytes, 40, r.identity.frame, 8);
    put(bytes, 48, r.identity.capture, 8);
    put(bytes, 56, static_cast<std::uint32_t>(r.atlas_x), 4);
    put(bytes, 60, static_cast<std::uint32_t>(r.atlas_y), 4);
    put(bytes, 64, r.width, 2); put(bytes, 66, r.height, 2);
    put(bytes, 72, std::bit_cast<std::uint32_t>(r.min_u), 4);
    put(bytes, 76, std::bit_cast<std::uint32_t>(r.max_u), 4);
    put(bytes, 80, std::bit_cast<std::uint32_t>(r.min_v), 4);
    put(bytes, 84, std::bit_cast<std::uint32_t>(r.max_v), 4);
    put(bytes, 88, r.atlas.size(), 2); put(bytes, 90, r.sprite.size(), 2);
    put_text(bytes, 96, r.atlas); put_text(bytes, 192, r.sprite);
    for (std::size_t i = 0; i < r.argb.size(); ++i) {
        put(bytes, header_bytes + i * 4, r.argb[i], 4);
    }
    return bytes;
}
inline Record decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_bytes || bytes.size() > max_bytes) {
        throw std::invalid_argument("Invalid texture record size");
    }
    if (get(bytes, 0, 4) != magic || get(bytes, 4, 2) != version
        || get(bytes, 8, 4) != bytes.size() || get(bytes, 12, 4) != 0
        || get(bytes, 68, 4) != 0 || get(bytes, 92, 4) != 0) {
        throw std::invalid_argument("Invalid texture record header");
    }
    Record r;
    r.kind = static_cast<Kind>(get(bytes, 6, 2));
    r.identity = {get(bytes, 16, 8), get(bytes, 24, 8), get(bytes, 32, 8),
                  get(bytes, 40, 8), get(bytes, 48, 8)};
    r.atlas_x = static_cast<std::int32_t>(get(bytes, 56, 4));
    r.atlas_y = static_cast<std::int32_t>(get(bytes, 60, 4));
    r.width = static_cast<std::uint16_t>(get(bytes, 64, 2));
    r.height = static_cast<std::uint16_t>(get(bytes, 66, 2));
    r.min_u = std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 72, 4)));
    r.max_u = std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 76, 4)));
    r.min_v = std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 80, 4)));
    r.max_v = std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 84, 4)));
    r.atlas = read_text(bytes, 96, static_cast<std::size_t>(get(bytes, 88, 2)), 96);
    r.sprite = read_text(bytes, 192, static_cast<std::size_t>(get(bytes, 90, 2)), 128);
    if (r.width > max_edge || r.height > max_edge
        || bytes.size() != header_bytes
            + (r.kind == Kind::retire ? 0 : static_cast<std::size_t>(r.width) * r.height * 4)) {
        throw std::invalid_argument("Invalid texture dimensions");
    }
    const auto count = (bytes.size() - header_bytes) / 4;
    r.argb.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        r.argb.push_back(static_cast<std::uint32_t>(get(bytes, header_bytes + i * 4, 4)));
    }
    check(r);
    return r;
}
} // namespace harrancraft::texture
