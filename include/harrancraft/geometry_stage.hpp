#pragma once

#include <harrancraft/geometry_wire.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace harrancraft::geometry {

// Restrict collision placement to exact signed axis permutations and scales.
// Arbitrary rotations would turn a box into an oriented shape, not an AABB.
struct CoordinateMap {
    std::string dimension;
    std::array<std::uint8_t, 3> source_axis{0, 1, 2};
    std::array<double, 3> scale{1, 1, 1};
    std::array<double, 3> origin{0, 0, 0};
};

struct HostBox {
    std::array<double, 3> minimum{};
    std::array<double, 3> maximum{};
    bool operator==(const HostBox&) const = default;
};

struct Candidate {
    std::uint64_t revision = 0;
    std::string canonical_state;
    std::vector<HostBox> boxes;
    bool operator==(const Candidate&) const = default;
};

struct Position {
    std::string dimension;
    std::int32_t x = 0, y = 0, z = 0;
    auto operator<=>(const Position&) const = default;
};

inline bool valid_map(const CoordinateMap& mapping) {
    try { check_text(mapping.dimension, 64, false); }
    catch (const std::invalid_argument&) { return false; }
    auto sorted = mapping.source_axis;
    std::sort(sorted.begin(), sorted.end());
    if (sorted != std::array<std::uint8_t, 3>{0, 1, 2}) return false;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(mapping.scale[axis]) || mapping.scale[axis] == 0
            || !std::isfinite(mapping.origin[axis])) {
            return false;
        }
    }
    return true;
}

inline std::optional<HostBox> map_box(
    const CoordinateMap& mapping, const Key& block, const Box& box) {
    if (!valid_map(mapping) || block.dimension != mapping.dimension || !valid_box(box)) {
        return std::nullopt;
    }
    const std::array<double, 3> local_min{box.min_x, box.min_y, box.min_z};
    const std::array<double, 3> local_max{box.max_x, box.max_y, box.max_z};
    const std::array<double, 3> base{
        static_cast<double>(block.x), static_cast<double>(block.y),
        static_cast<double>(block.z)
    };
    HostBox result;
    for (std::size_t host_axis = 0; host_axis < 3; ++host_axis) {
        const auto guest_axis = mapping.source_axis[host_axis];
        const auto a = (base[guest_axis] + local_min[guest_axis])
            * mapping.scale[host_axis] + mapping.origin[host_axis];
        const auto b = (base[guest_axis] + local_max[guest_axis])
            * mapping.scale[host_axis] + mapping.origin[host_axis];
        result.minimum[host_axis] = std::min(a, b);
        result.maximum[host_axis] = std::max(a, b);
        if (!std::isfinite(a) || !std::isfinite(b)
            || !(result.minimum[host_axis] < result.maximum[host_axis])) {
            return std::nullopt;
        }
    }
    return result;
}

enum class StageResult { applied, retired, stale, invalid, capacity };

// Owns only candidate AABBs, not Dying Light physics handles or registrations.
// Real host activation must use a separately verified safe engine mutation phase.
class CandidateLedger {
public:
    static constexpr std::size_t max_positions = 128;

    CandidateLedger() : owner_(std::this_thread::get_id()) {}

    void begin(std::uint64_t epoch, CoordinateMap mapping) {
        check_owner();
        stop();
        if (epoch == 0 || !valid_map(mapping)) {
            throw std::invalid_argument("Invalid geometry staging session");
        }
        mapping_ = std::move(mapping);
        epoch_ = epoch;
    }

    StageResult apply(const Frame& frame) {
        check_owner();
        if (epoch_ == 0 || frame.epoch != epoch_ || frame.revision <= high_revision_) {
            return StageResult::stale;
        }
        if (frame.revision != high_revision_ + 1) {
            stop();
            return StageResult::invalid;
        }
        try {
            check(frame);
        } catch (const std::invalid_argument&) {
            stop();
            return StageResult::invalid;
        }
        if (frame.key.dimension != mapping_->dimension) {
            stop();
            return StageResult::invalid;
        }
        const Position position{
            frame.key.dimension, frame.key.x, frame.key.y, frame.key.z
        };
        if (frame.kind == Kind::retire || frame.boxes.empty()) {
            candidates_.erase(position);
            high_revision_ = frame.revision;
            return StageResult::retired;
        }
        if (!candidates_.contains(position) && candidates_.size() == max_positions) {
            stop();
            return StageResult::capacity;
        }
        Candidate pending;
        pending.revision = frame.revision;
        pending.canonical_state = frame.state;
        pending.boxes.reserve(frame.boxes.size());
        for (const auto& box : frame.boxes) {
            auto host = map_box(*mapping_, frame.key, box);
            if (!host) {
                stop();
                return StageResult::invalid;
            }
            pending.boxes.push_back(*host);
        }
        candidates_.insert_or_assign(position, std::move(pending));
        high_revision_ = frame.revision;
        return StageResult::applied;
    }

    std::optional<Candidate> find(const Position& key) const {
        check_owner();
        const auto it = candidates_.find(key);
        if (it == candidates_.end()) return std::nullopt;
        return it->second;
    }

    std::size_t size() const {
        check_owner();
        return candidates_.size();
    }

    std::uint64_t epoch() const {
        check_owner();
        return epoch_;
    }

    void stop() {
        check_owner();
        candidates_.clear();
        mapping_.reset();
        epoch_ = 0;
        high_revision_ = 0;
    }

private:
    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Geometry staging accessed from another thread");
        }
    }
    const std::thread::id owner_;
    std::optional<CoordinateMap> mapping_;
    std::uint64_t epoch_ = 0;
    std::uint64_t high_revision_ = 0;
    std::map<Position, Candidate> candidates_;
};

} // namespace harrancraft::geometry
