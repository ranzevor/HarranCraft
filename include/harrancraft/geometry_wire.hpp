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

namespace harrancraft::geometry {

static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);
inline constexpr std::uint32_t magic = 0x31474348;
inline constexpr std::uint16_t version = 1;
inline constexpr std::size_t header_bytes = 320;
inline constexpr std::size_t max_boxes = 128;
inline constexpr std::size_t box_bytes = 48;
inline constexpr std::size_t max_bytes = header_bytes + max_boxes * box_bytes;

enum class Kind : std::uint16_t { snapshot = 1, retire = 2 };
struct Key {
    std::string dimension;
    std::int32_t x = 0, y = 0, z = 0;
    bool operator==(const Key&) const = default;
};
struct Box {
    double min_x = 0, min_y = 0, min_z = 0;
    double max_x = 0, max_y = 0, max_z = 0;
    bool operator==(const Box&) const = default;
};
struct Frame {
    Kind kind = Kind::snapshot;
    std::uint64_t epoch = 0, revision = 0;
    Key key;
    std::string state;
    std::vector<Box> boxes;
    bool operator==(const Frame&) const = default;
};
using Bytes = std::vector<std::uint8_t>;

inline void put(Bytes& bytes, std::size_t at, std::uint64_t value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}
inline std::uint64_t get(std::span<const std::uint8_t> bytes, std::size_t at,
                         std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        value |= static_cast<std::uint64_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}
inline bool valid_box(const Box& b) {
    return std::isfinite(b.min_x) && std::isfinite(b.min_y) && std::isfinite(b.min_z)
        && std::isfinite(b.max_x) && std::isfinite(b.max_y) && std::isfinite(b.max_z)
        && b.min_x < b.max_x && b.min_y < b.max_y && b.min_z < b.max_z;
}
inline void check_text(std::string_view value, std::size_t capacity, bool empty_ok) {
    if (value.size() > capacity || (!empty_ok && value.empty())) {
        throw std::invalid_argument("Geometry text length invalid");
    }
    for (char item : value) {
        const auto ch = static_cast<unsigned char>(item);
        if (ch < 32 || ch > 126) throw std::invalid_argument("Geometry text is not ASCII");
    }
}
inline void check(const Frame& f) {
    if ((f.kind != Kind::snapshot && f.kind != Kind::retire)
        || f.epoch == 0 || f.revision == 0
        || f.epoch > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        || f.revision > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        || f.boxes.size() > max_boxes) {
        throw std::invalid_argument("Invalid geometry frame identity");
    }
    check_text(f.key.dimension, 64, false);
    check_text(f.state, 192, f.kind == Kind::retire);
    if (f.kind == Kind::retire && (!f.state.empty() || !f.boxes.empty())) {
        throw std::invalid_argument("Retirement must have no geometry");
    }
    for (const auto& box : f.boxes) {
        if (!valid_box(box)) throw std::invalid_argument("Invalid geometry bounds");
    }
}
inline void write_text(Bytes& bytes, std::size_t at, std::size_t len_at,
                       std::string_view value) {
    put(bytes, len_at, value.size(), 2);
    for (std::size_t i = 0; i < value.size(); ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(value[i]);
    }
}
inline std::string read_text(std::span<const std::uint8_t> bytes, std::size_t at,
                             std::size_t len_at, std::size_t capacity) {
    const auto len = static_cast<std::size_t>(get(bytes, len_at, 2));
    if (len > capacity) throw std::invalid_argument("Geometry text exceeds capacity");
    std::string result;
    result.reserve(len);
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto ch = bytes[at + i];
        if (i < len) {
            if (ch < 32 || ch > 126) throw std::invalid_argument("Invalid geometry text");
            result.push_back(static_cast<char>(ch));
        } else if (ch != 0) {
            throw std::invalid_argument("Nonzero geometry padding");
        }
    }
    return result;
}
inline Bytes encode(const Frame& f) {
    check(f);
    Bytes bytes(header_bytes + f.boxes.size() * box_bytes, 0);
    put(bytes, 0, magic, 4);
    put(bytes, 4, version, 2);
    put(bytes, 6, static_cast<std::uint16_t>(f.kind), 2);
    put(bytes, 8, bytes.size(), 4);
    put(bytes, 16, f.epoch, 8);
    put(bytes, 24, f.revision, 8);
    put(bytes, 32, static_cast<std::uint32_t>(f.key.x), 4);
    put(bytes, 36, static_cast<std::uint32_t>(f.key.y), 4);
    put(bytes, 40, static_cast<std::uint32_t>(f.key.z), 4);
    put(bytes, 44, f.boxes.size(), 2);
    write_text(bytes, 64, 46, f.key.dimension);
    write_text(bytes, 128, 48, f.state);
    for (std::size_t i = 0; i < f.boxes.size(); ++i) {
        const auto& box = f.boxes[i];
        const std::array<double, 6> dimensions{
            box.min_x, box.min_y, box.min_z, box.max_x, box.max_y, box.max_z
        };
        for (std::size_t j = 0; j < dimensions.size(); ++j) {
            put(bytes, header_bytes + i * box_bytes + j * 8,
                std::bit_cast<std::uint64_t>(dimensions[j]), 8);
        }
    }
    return bytes;
}
inline Frame decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_bytes || bytes.size() > max_bytes) {
        throw std::invalid_argument("Invalid geometry frame length");
    }
    if (get(bytes, 0, 4) != magic || get(bytes, 4, 2) != version
        || get(bytes, 8, 4) != bytes.size() || get(bytes, 12, 4) != 0
        || get(bytes, 50, 2) != 0 || get(bytes, 52, 4) != 0
        || get(bytes, 56, 8) != 0) {
        throw std::invalid_argument("Invalid geometry header");
    }
    const auto count = static_cast<std::size_t>(get(bytes, 44, 2));
    if (count > max_boxes || bytes.size() != header_bytes + count * box_bytes) {
        throw std::invalid_argument("Invalid geometry box count");
    }
    Frame f;
    f.kind = static_cast<Kind>(get(bytes, 6, 2));
    f.epoch = get(bytes, 16, 8);
    f.revision = get(bytes, 24, 8);
    f.key.x = static_cast<std::int32_t>(get(bytes, 32, 4));
    f.key.y = static_cast<std::int32_t>(get(bytes, 36, 4));
    f.key.z = static_cast<std::int32_t>(get(bytes, 40, 4));
    f.key.dimension = read_text(bytes, 64, 46, 64);
    f.state = read_text(bytes, 128, 48, 192);
    f.boxes.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::array<double, 6> doubles{};
        for (std::size_t j = 0; j < doubles.size(); ++j) {
            doubles[j] = std::bit_cast<double>(
                get(bytes, header_bytes + i * box_bytes + j * 8, 8));
        }
        f.boxes.push_back({
            doubles[0], doubles[1], doubles[2], doubles[3], doubles[4], doubles[5]
        });
    }
    check(f);
    return f;
}
} // namespace harrancraft::geometry
