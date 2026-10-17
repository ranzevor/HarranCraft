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

namespace harrancraft::render {

static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);

inline constexpr std::uint32_t magic = 0x31524348;
inline constexpr std::uint16_t version = 1;
inline constexpr std::size_t header_bytes = 384;
inline constexpr std::size_t quad_bytes = 400;
inline constexpr std::size_t max_quads = 512;
inline constexpr std::size_t max_bytes = header_bytes + quad_bytes * max_quads;

enum class Kind : std::uint16_t { snapshot = 1, retire = 2 };

struct Identity {
    std::uint64_t epoch = 0, world = 0, camera = 0, frame = 0, capture = 0;
    bool operator==(const Identity&) const = default;
};
struct Key {
    std::string dimension;
    std::int32_t x = 0, y = 0, z = 0;
    bool operator==(const Key&) const = default;
};
struct Pose {
    double x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    bool operator==(const Pose&) const = default;
};
struct Vertex {
    float x = 0, y = 0, z = 0, u = 0, v = 0;
    std::uint32_t color = 0, light = 0, normal = 0;
    bool operator==(const Vertex&) const = default;
};
struct Quad {
    std::int16_t cull_face = -1;
    std::uint16_t face = 0;
    std::int16_t tint_index = -1;
    bool shade = false;
    std::uint8_t emission = 0;
    std::string atlas, sprite;
    std::array<Vertex, 4> vertices{};
    bool operator==(const Quad&) const = default;
};
struct Record {
    Kind kind = Kind::snapshot;
    Identity identity;
    Key key;
    std::string state;
    Pose pose;
    std::vector<Quad> quads;
    bool operator==(const Record&) const = default;
};
using Bytes = std::vector<std::uint8_t>;

inline void put(Bytes& bytes, std::size_t at, std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes.at(at + index) = static_cast<std::uint8_t>(value >> (index * 8));
    }
}
inline std::uint64_t get(std::span<const std::uint8_t> bytes, std::size_t at,
                         std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= static_cast<std::uint64_t>(bytes[at + index]) << (index * 8);
    }
    return value;
}
inline void text_check(std::string_view value, std::size_t limit, bool allow_empty) {
    if (value.size() > limit || (!allow_empty && value.empty())) {
        throw std::invalid_argument("Invalid render string length");
    }
    for (char item : value) {
        const auto c = static_cast<unsigned char>(item);
        if (c < 33 || c > 126) throw std::invalid_argument("Invalid render ASCII string");
    }
}
inline bool finite(const Pose& pose) {
    return std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z)
        && std::isfinite(pose.yaw) && std::isfinite(pose.pitch);
}
inline bool finite(const Vertex& vertex) {
    return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z)
        && std::isfinite(vertex.u) && std::isfinite(vertex.v);
}
inline bool zero_camera(const Pose& p) {
    return std::bit_cast<std::uint64_t>(p.x) == 0
        && std::bit_cast<std::uint64_t>(p.y) == 0
        && std::bit_cast<std::uint64_t>(p.z) == 0
        && std::bit_cast<std::uint32_t>(p.yaw) == 0
        && std::bit_cast<std::uint32_t>(p.pitch) == 0;
}
inline void check(const Record& record) {
    const auto& id = record.identity;
    constexpr auto signed_max = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if ((record.kind != Kind::snapshot && record.kind != Kind::retire)
        || id.epoch == 0 || id.world == 0 || id.camera == 0
        || id.frame == 0 || id.capture == 0
        || id.epoch > signed_max || id.world > signed_max || id.camera > signed_max
        || id.frame > signed_max || id.capture > signed_max
        || record.quads.size() > max_quads || !finite(record.pose)) {
        throw std::invalid_argument("Invalid render identity");
    }
    text_check(record.key.dimension, 64, false);
    text_check(record.state, 192, record.kind == Kind::retire);
    if (record.kind == Kind::retire
        && (!record.state.empty() || !record.quads.empty() || !zero_camera(record.pose))) {
        throw std::invalid_argument("Retired model contains data");
    }
    for (const auto& quad : record.quads) {
        if (quad.cull_face < -1 || quad.cull_face > 5 || quad.face > 5
            || quad.tint_index < -1 || quad.tint_index > 255 || quad.emission > 15) {
            throw std::invalid_argument("Invalid render quad metadata");
        }
        text_check(quad.atlas, 128, false);
        text_check(quad.sprite, 128, false);
        for (const auto& vertex : quad.vertices) {
            if (!finite(vertex)) throw std::invalid_argument("Invalid render vertex");
        }
    }
}
inline void put_text(Bytes& bytes, std::size_t at, std::string_view value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        bytes[at + index] = static_cast<std::uint8_t>(value[index]);
    }
}
inline std::string get_text(std::span<const std::uint8_t> bytes, std::size_t at,
                            std::size_t size, std::size_t capacity) {
    if (size > capacity) throw std::invalid_argument("Oversize render string");
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0; index < capacity; ++index) {
        const auto c = bytes[at + index];
        if (index < size) {
            if (c < 33 || c > 126) throw std::invalid_argument("Invalid render ASCII");
            result.push_back(static_cast<char>(c));
        } else if (c != 0) {
            throw std::invalid_argument("Nonzero render padding");
        }
    }
    return result;
}

inline Bytes encode(const Record& record) {
    check(record);
    Bytes bytes(header_bytes + record.quads.size() * quad_bytes, 0);
    const auto& id = record.identity;
    put(bytes, 0, magic, 4);
    put(bytes, 4, version, 2);
    put(bytes, 6, static_cast<std::uint16_t>(record.kind), 2);
    put(bytes, 8, bytes.size(), 4);
    put(bytes, 16, id.epoch, 8);
    put(bytes, 24, id.world, 8);
    put(bytes, 32, id.camera, 8);
    put(bytes, 40, id.frame, 8);
    put(bytes, 48, id.capture, 8);
    put(bytes, 56, static_cast<std::uint32_t>(record.key.x), 4);
    put(bytes, 60, static_cast<std::uint32_t>(record.key.y), 4);
    put(bytes, 64, static_cast<std::uint32_t>(record.key.z), 4);
    put(bytes, 68, record.quads.size(), 2);
    put(bytes, 70, record.key.dimension.size(), 2);
    put(bytes, 72, record.state.size(), 2);
    put(bytes, 76, std::bit_cast<std::uint64_t>(record.pose.x), 8);
    put(bytes, 84, std::bit_cast<std::uint64_t>(record.pose.y), 8);
    put(bytes, 92, std::bit_cast<std::uint64_t>(record.pose.z), 8);
    put(bytes, 100, std::bit_cast<std::uint32_t>(record.pose.yaw), 4);
    put(bytes, 104, std::bit_cast<std::uint32_t>(record.pose.pitch), 4);
    put_text(bytes, 112, record.key.dimension);
    put_text(bytes, 176, record.state);
    for (std::size_t i = 0; i < record.quads.size(); ++i) {
        const auto& quad = record.quads[i];
        const auto at = header_bytes + quad_bytes * i;
        put(bytes, at, static_cast<std::uint16_t>(quad.cull_face), 2);
        put(bytes, at + 2, quad.face, 2);
        put(bytes, at + 4, static_cast<std::uint16_t>(quad.tint_index), 2);
        put(bytes, at + 6, quad.shade ? 1 : 0, 1);
        put(bytes, at + 7, quad.emission, 1);
        put(bytes, at + 8, quad.atlas.size(), 2);
        put(bytes, at + 10, quad.sprite.size(), 2);
        put_text(bytes, at + 16, quad.atlas);
        put_text(bytes, at + 144, quad.sprite);
        for (std::size_t v = 0; v < quad.vertices.size(); ++v) {
            const auto& vertex = quad.vertices[v];
            const auto start = at + 272 + 32 * v;
            put(bytes, start, std::bit_cast<std::uint32_t>(vertex.x), 4);
            put(bytes, start + 4, std::bit_cast<std::uint32_t>(vertex.y), 4);
            put(bytes, start + 8, std::bit_cast<std::uint32_t>(vertex.z), 4);
            put(bytes, start + 12, std::bit_cast<std::uint32_t>(vertex.u), 4);
            put(bytes, start + 16, std::bit_cast<std::uint32_t>(vertex.v), 4);
            put(bytes, start + 20, vertex.color, 4);
            put(bytes, start + 24, vertex.light, 4);
            put(bytes, start + 28, vertex.normal, 4);
        }
    }
    return bytes;
}

inline Record decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < header_bytes || bytes.size() > max_bytes) {
        throw std::invalid_argument("Invalid render record length");
    }
    if (get(bytes, 0, 4) != magic || get(bytes, 4, 2) != version
        || get(bytes, 8, 4) != bytes.size() || get(bytes, 12, 4) != 0
        || get(bytes, 74, 2) != 0 || get(bytes, 108, 4) != 0) {
        throw std::invalid_argument("Invalid render header");
    }
    for (std::size_t at = 368; at < header_bytes; ++at) {
        if (bytes[at] != 0) throw std::invalid_argument("Nonzero render reserved bytes");
    }
    const auto count = static_cast<std::size_t>(get(bytes, 68, 2));
    if (count > max_quads || bytes.size() != header_bytes + count * quad_bytes) {
        throw std::invalid_argument("Invalid render quad count");
    }
    Record result;
    result.kind = static_cast<Kind>(get(bytes, 6, 2));
    result.identity = {get(bytes, 16, 8), get(bytes, 24, 8), get(bytes, 32, 8),
                       get(bytes, 40, 8), get(bytes, 48, 8)};
    result.key.x = static_cast<std::int32_t>(get(bytes, 56, 4));
    result.key.y = static_cast<std::int32_t>(get(bytes, 60, 4));
    result.key.z = static_cast<std::int32_t>(get(bytes, 64, 4));
    result.key.dimension = get_text(bytes, 112, static_cast<std::size_t>(get(bytes, 70, 2)), 64);
    result.state = get_text(bytes, 176, static_cast<std::size_t>(get(bytes, 72, 2)), 192);
    result.pose = {
        std::bit_cast<double>(get(bytes, 76, 8)),
        std::bit_cast<double>(get(bytes, 84, 8)),
        std::bit_cast<double>(get(bytes, 92, 8)),
        std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 100, 4))),
        std::bit_cast<float>(static_cast<std::uint32_t>(get(bytes, 104, 4)))
    };
    result.quads.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = header_bytes + i * quad_bytes;
        if (get(bytes, at + 12, 4) != 0 || get(bytes, at + 6, 1) > 1) {
            throw std::invalid_argument("Invalid render quad flags");
        }
        Quad quad;
        quad.cull_face = static_cast<std::int16_t>(get(bytes, at, 2));
        quad.face = static_cast<std::uint16_t>(get(bytes, at + 2, 2));
        quad.tint_index = static_cast<std::int16_t>(get(bytes, at + 4, 2));
        quad.shade = get(bytes, at + 6, 1) != 0;
        quad.emission = static_cast<std::uint8_t>(get(bytes, at + 7, 1));
        quad.atlas = get_text(bytes, at + 16, static_cast<std::size_t>(
                                  get(bytes, at + 8, 2)), 128);
        quad.sprite = get_text(bytes, at + 144, static_cast<std::size_t>(
                                   get(bytes, at + 10, 2)), 128);
        for (std::size_t v = 0; v < quad.vertices.size(); ++v) {
            auto& vertex = quad.vertices[v];
            const auto offset = at + 272 + 32 * v;
            vertex.x = std::bit_cast<float>(
                static_cast<std::uint32_t>(get(bytes, offset, 4)));
            vertex.y = std::bit_cast<float>(
                static_cast<std::uint32_t>(get(bytes, offset + 4, 4)));
            vertex.z = std::bit_cast<float>(
                static_cast<std::uint32_t>(get(bytes, offset + 8, 4)));
            vertex.u = std::bit_cast<float>(
                static_cast<std::uint32_t>(get(bytes, offset + 12, 4)));
            vertex.v = std::bit_cast<float>(
                static_cast<std::uint32_t>(get(bytes, offset + 16, 4)));
            vertex.color = static_cast<std::uint32_t>(get(bytes, offset + 20, 4));
            vertex.light = static_cast<std::uint32_t>(get(bytes, offset + 24, 4));
            vertex.normal = static_cast<std::uint32_t>(get(bytes, offset + 28, 4));
        }
        result.quads.push_back(std::move(quad));
    }
    check(result);
    return result;
}
} // namespace harrancraft::render
