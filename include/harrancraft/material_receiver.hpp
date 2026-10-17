#pragma once

#include <harrancraft/material_cache.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace harrancraft::material {

enum class ReceiveState {
    idle, active, ended, truncated, incomplete, malformed, rejected, stopped
};

// Incremental native source-texture receiver with no engine objects or I/O ownership.
class MaterialReceiver {
public:
    MaterialReceiver() : owner_(std::this_thread::get_id()) {}

    void begin(std::uint64_t epoch, std::uint64_t world,
               std::uint64_t camera, std::string dimension) {
        check_owner();
        reset(ReceiveState::stopped);
        cache_.begin(epoch, world, camera, std::move(dimension));
        received_ = 0;
        state_ = ReceiveState::active;
    }

    bool begin_model(const render::Record& model) {
        check_owner();
        if (state_ != ReceiveState::active) return false;
        if (filled_ != 0 || !cache_.begin_model(model)) {
            reset(ReceiveState::rejected);
            return false;
        }
        return true;
    }

    bool feed(std::span<const std::uint8_t> input) {
        check_owner();
        if (state_ != ReceiveState::active) return false;
        std::size_t offset = 0;
        while (offset < input.size()) {
            const auto target = declared_ == 0 ? std::size_t{12} : declared_;
            const auto amount = std::min(target - filled_, input.size() - offset);
            std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset),
                        amount, data_.begin() + static_cast<std::ptrdiff_t>(filled_));
            offset += amount;
            filled_ += amount;
            if (declared_ == 0 && filled_ == 12) {
                const auto size = static_cast<std::size_t>(texture::get(
                    std::span<const std::uint8_t>(data_.data(), 12), 8, 4));
                if (size < texture::header_bytes || size > texture::max_bytes
                    || (size - texture::header_bytes) % 4 != 0) {
                    reset(ReceiveState::malformed);
                    return false;
                }
                declared_ = size;
            }
            if (declared_ != 0 && filled_ == declared_) {
                texture::Record record;
                try {
                    record = texture::decode(std::span<const std::uint8_t>(
                        data_.data(), declared_));
                } catch (const std::invalid_argument&) {
                    reset(ReceiveState::malformed);
                    return false;
                }
                if (!cache_.accept(record)) {
                    reset(ReceiveState::rejected);
                    return false;
                }
                ++received_;
                filled_ = 0;
                declared_ = 0;
            }
        }
        return true;
    }

    void end_of_stream() {
        check_owner();
        if (state_ != ReceiveState::active) return;
        const auto partial = filled_ != 0;
        const auto incomplete = cache_.state() == State::pending && cache_.expected() != 0;
        reset(partial ? ReceiveState::truncated
                      : incomplete ? ReceiveState::incomplete : ReceiveState::ended);
    }

    void stop() {
        check_owner();
        reset(ReceiveState::stopped);
    }

    bool ready() const { check_owner(); return state_ == ReceiveState::active && cache_.ready(); }
    ReceiveState state() const { check_owner(); return state_; }
    std::uint64_t epoch() const { check_owner(); return cache_.epoch(); }
    std::uint64_t received() const { check_owner(); return received_; }
    std::size_t required() const { check_owner(); return cache_.expected(); }
    std::size_t complete() const { check_owner(); return cache_.received(); }
    std::size_t buffered_bytes() const { check_owner(); return filled_; }

    std::optional<texture::Record> find(const SpriteKey& key) const {
        check_owner();
        return ready() ? cache_.find(key) : std::nullopt;
    }

private:
    void reset(ReceiveState reason) {
        cache_.stop();
        data_.fill(0);
        filled_ = 0;
        declared_ = 0;
        state_ = reason;
    }

    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Material receiver accessed from a foreign thread");
        }
    }

    const std::thread::id owner_;
    MaterialCache cache_;
    std::array<std::uint8_t, texture::max_bytes> data_{};
    std::size_t filled_ = 0;
    std::size_t declared_ = 0;
    std::uint64_t received_ = 0;
    ReceiveState state_ = ReceiveState::idle;
};

} // namespace harrancraft::material
