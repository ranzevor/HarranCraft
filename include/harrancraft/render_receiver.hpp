#pragma once

#include <harrancraft/render_wire.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace harrancraft::render {

enum class ReceiveState { idle, active, ended, truncated, malformed, rejected, capacity, stopped };

struct Position {
    std::string dimension;
    std::int32_t x = 0, y = 0, z = 0;
    auto operator<=>(const Position&) const = default;
};

// One owning thread stages immutable model records from arbitrary transport fragments.
// This code does not hold graphics-engine handles or perform GPU work.
class RenderReceiver {
public:
    static constexpr std::size_t max_positions = 16;

    RenderReceiver() : owner_(std::this_thread::get_id()) {}

    void begin(Identity identity, std::string dimension) {
        check_owner();
        retire(ReceiveState::stopped);
        if (identity.epoch == 0 || identity.epoch <= last_epoch_
            || identity.world == 0 || identity.camera == 0
            || identity.epoch > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())
            || identity.world > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())
            || identity.camera > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("Invalid render receiver identity");
        }
        text_check(dimension, 64, false);
        expected_ = identity;
        dimension_ = std::move(dimension);
        last_epoch_ = identity.epoch;
        last_frame_ = 0;
        last_capture_ = 0;
        count_ = 0;
        state_ = ReceiveState::active;
    }

    bool feed(std::span<const std::uint8_t> fragment) {
        check_owner();
        if (state_ != ReceiveState::active) return false;
        std::size_t offset = 0;
        while (offset < fragment.size()) {
            const auto needed = record_bytes_ == 0 ? std::size_t{12} : record_bytes_;
            const auto amount = std::min(needed - filled_, fragment.size() - offset);
            std::copy_n(fragment.begin() + static_cast<std::ptrdiff_t>(offset),
                        amount, buffer_.begin() + static_cast<std::ptrdiff_t>(filled_));
            filled_ += amount;
            offset += amount;
            if (record_bytes_ == 0 && filled_ == 12) {
                const auto total = static_cast<std::size_t>(
                    get(std::span<const std::uint8_t>(buffer_.data(), 12), 8, 4));
                if (total < header_bytes || total > max_bytes
                    || (total - header_bytes) % quad_bytes != 0) {
                    retire(ReceiveState::malformed);
                    return false;
                }
                record_bytes_ = total;
            }
            if (record_bytes_ != 0 && filled_ == record_bytes_) {
                Record record;
                try {
                    record = decode(std::span<const std::uint8_t>(
                        buffer_.data(), record_bytes_));
                } catch (const std::invalid_argument&) {
                    retire(ReceiveState::malformed);
                    return false;
                }
                if (!accept(std::move(record))) return false;
                filled_ = 0;
                record_bytes_ = 0;
            }
        }
        return true;
    }

    void end_of_stream() {
        check_owner();
        if (state_ == ReceiveState::active) {
            retire(filled_ == 0 ? ReceiveState::ended : ReceiveState::truncated);
        }
    }

    void stop() {
        check_owner();
        retire(ReceiveState::stopped);
    }

    ReceiveState state() const { check_owner(); return state_; }
    std::uint64_t epoch() const { check_owner(); return expected_.epoch; }
    std::uint64_t records() const { check_owner(); return count_; }
    std::uint64_t last_frame() const { check_owner(); return last_frame_; }
    std::uint64_t last_capture() const { check_owner(); return last_capture_; }
    std::size_t buffered_bytes() const { check_owner(); return filled_; }
    std::size_t staged_positions() const { check_owner(); return models_.size(); }

    std::optional<Record> find(const Position& position) const {
        check_owner();
        if (state_ != ReceiveState::active) return std::nullopt;
        const auto found = models_.find(position);
        return found == models_.end() ? std::nullopt : std::optional<Record>(found->second);
    }

private:
    bool accept(Record record) {
        if (record.identity.epoch != expected_.epoch
            || record.identity.world != expected_.world
            || record.identity.camera != expected_.camera
            || record.key.dimension != dimension_
            || record.identity.frame <= last_frame_
            || last_capture_ == std::numeric_limits<std::uint64_t>::max()
            || record.identity.capture != last_capture_ + 1) {
            retire(ReceiveState::rejected);
            return false;
        }
        const Position pos{
            record.key.dimension, record.key.x, record.key.y, record.key.z
        };
        if (record.kind == Kind::retire || record.quads.empty()) {
            models_.erase(pos);
        } else {
            if (!models_.contains(pos) && models_.size() == max_positions) {
                retire(ReceiveState::capacity);
                return false;
            }
            models_.insert_or_assign(pos, std::move(record));
        }
        last_frame_ = record.identity.frame;
        last_capture_ = record.identity.capture;
        ++count_;
        return true;
    }

    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Render receiver accessed from another thread");
        }
    }

    void retire(ReceiveState state) {
        models_.clear();
        buffer_.fill(0);
        filled_ = 0;
        record_bytes_ = 0;
        expected_ = {};
        dimension_.clear();
        state_ = state;
    }

    const std::thread::id owner_;
    std::array<std::uint8_t, max_bytes> buffer_{};
    std::size_t filled_ = 0;
    std::size_t record_bytes_ = 0;
    std::uint64_t last_epoch_ = 0;
    std::uint64_t last_frame_ = 0;
    std::uint64_t last_capture_ = 0;
    std::uint64_t count_ = 0;
    Identity expected_;
    std::string dimension_;
    std::map<Position, Record> models_;
    ReceiveState state_ = ReceiveState::idle;
};

} // namespace harrancraft::render
