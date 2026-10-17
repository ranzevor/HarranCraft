#pragma once

#include <harrancraft/geometry_stage.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>

namespace harrancraft::geometry {

enum class ReceiverState {
    idle, active, ended, truncated, malformed, rejected, capacity, stopped
};

// The caller owns transport I/O. This bounded receiver consumes arbitrary
// byte fragments on one owner thread and never calls the game engine.
class GeometryReceiver {
public:
    GeometryReceiver() : owner_(std::this_thread::get_id()) {}

    void begin(std::uint64_t epoch, CoordinateMap mapping) {
        check_owner();
        retire(ReceiverState::stopped);
        if (epoch == 0 || epoch <= last_epoch_
            || epoch > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
            || !valid_map(mapping)) {
            throw std::invalid_argument("Invalid receiver session");
        }
        ledger_.begin(epoch, std::move(mapping));
        last_epoch_ = epoch;
        accepted_ = 0;
        last_revision_ = 0;
        state_ = ReceiverState::active;
    }

    bool feed(std::span<const std::uint8_t> incoming) {
        check_owner();
        if (state_ != ReceiverState::active) return false;
        try {
            std::size_t offset = 0;
            while (offset < incoming.size()) {
                const std::size_t target = record_size_ == 0 ? 12 : record_size_;
                const std::size_t amount = std::min(target - filled_, incoming.size() - offset);
                std::copy_n(incoming.begin() + static_cast<std::ptrdiff_t>(offset),
                            amount, buffer_.begin() + static_cast<std::ptrdiff_t>(filled_));
                offset += amount;
                filled_ += amount;

                if (record_size_ == 0 && filled_ == 12) {
                    const auto length = static_cast<std::size_t>(
                        get(std::span<const std::uint8_t>(buffer_.data(), 12), 8, 4));
                    if (length < header_bytes || length > max_bytes
                        || (length - header_bytes) % box_bytes != 0) {
                        retire(ReceiverState::malformed);
                        return false;
                    }
                    record_size_ = length;
                }
                if (record_size_ != 0 && filled_ == record_size_) {
                    Frame frame;
                    try {
                        frame = decode(std::span<const std::uint8_t>(
                            buffer_.data(), record_size_));
                    } catch (const std::invalid_argument&) {
                        retire(ReceiverState::malformed);
                        return false;
                    }
                    const auto outcome = ledger_.apply(frame);
                    if (outcome != StageResult::applied && outcome != StageResult::retired) {
                        retire(outcome == StageResult::capacity
                                   ? ReceiverState::capacity : ReceiverState::rejected);
                        return false;
                    }
                    if (accepted_ == std::numeric_limits<std::uint64_t>::max()) {
                        retire(ReceiverState::capacity);
                        return false;
                    }
                    ++accepted_;
                    last_revision_ = frame.revision;
                    last_record_ = std::move(frame);
                    filled_ = 0;
                    record_size_ = 0;
                }
            }
            return true;
        } catch (...) {
            retire(ReceiverState::rejected);
            throw;
        }
    }

    void end_of_stream() {
        check_owner();
        if (state_ == ReceiverState::active) {
            retire(filled_ == 0 ? ReceiverState::ended : ReceiverState::truncated);
        }
    }

    void stop() {
        check_owner();
        retire(ReceiverState::stopped);
    }

    ReceiverState state() const { check_owner(); return state_; }
    std::uint64_t epoch() const { check_owner(); return ledger_.epoch(); }
    std::uint64_t records() const { check_owner(); return accepted_; }
    std::uint64_t last_revision() const { check_owner(); return last_revision_; }
    std::size_t buffered_bytes() const { check_owner(); return filled_; }
    std::size_t staged_positions() const { check_owner(); return ledger_.size(); }

    // One decoded receipt distinguishes an empty snapshot from missing geometry.
    std::optional<Frame> last_record() const {
        check_owner();
        return last_record_;
    }

    std::optional<Candidate> find(const Position& position) const {
        check_owner();
        if (state_ != ReceiverState::active) return std::nullopt;
        return ledger_.find(position);
    }

private:
    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Geometry receiver accessed from another thread");
        }
    }

    void retire(ReceiverState reason) {
        ledger_.stop();
        last_record_.reset();
        buffer_.fill(0);
        filled_ = 0;
        record_size_ = 0;
        state_ = reason;
    }

    const std::thread::id owner_;
    CandidateLedger ledger_;
    std::optional<Frame> last_record_;
    std::array<std::uint8_t, max_bytes> buffer_{};
    std::size_t filled_ = 0;
    std::size_t record_size_ = 0;
    std::uint64_t last_epoch_ = 0;
    std::uint64_t last_revision_ = 0;
    std::uint64_t accepted_ = 0;
    ReceiverState state_ = ReceiverState::idle;
};

} // namespace harrancraft::geometry
