#pragma once

#include <harrancraft/material_cache.hpp>
#include <harrancraft/render_receiver.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace harrancraft::material {

enum class Channel { models, textures };
enum class CoordinationState {
    idle, active, ended, truncated, incomplete, malformed, rejected, capacity,
    timed_out, revoked, io_failed, stopped
};

struct Material {
    render::Record model;
    std::map<SpriteKey, texture::Record> sprites;
};

// A single I/O owner joins two FIFO streams; no engine callbacks or endpoint ownership.
class MaterialCoordinator {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    static constexpr std::size_t max_pending_captures = 16;
    static constexpr std::size_t max_positions = render::RenderReceiver::max_positions;
    static constexpr std::size_t max_records = 256;
    // Wire/manifest accounting excludes fixed parsers and bounded container overhead.
    static constexpr std::size_t max_bytes = 4 * 1024 * 1024;
    static constexpr std::size_t parser_capacity = render::max_bytes + texture::max_bytes;

    explicit MaterialCoordinator(std::chrono::milliseconds timeout = std::chrono::seconds(1))
        : owner_(std::this_thread::get_id()), timeout_(timeout) {
        if (timeout <= std::chrono::milliseconds::zero() || timeout > std::chrono::seconds(5)) {
            throw std::invalid_argument("Invalid material coordination timeout");
        }
    }

    MaterialCoordinator(const MaterialCoordinator&) = delete;
    MaterialCoordinator& operator=(const MaterialCoordinator&) = delete;

    void begin(std::uint64_t epoch, std::uint64_t world, std::uint64_t camera,
               std::string dimension, TimePoint now = Clock::now()) {
        check_owner();
        reset(CoordinationState::stopped);
        constexpr auto limit = static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
        if (epoch == 0 || epoch <= last_epoch_ || world == 0 || camera == 0
            || epoch > limit || world > limit || camera > limit) {
            throw std::invalid_argument("Invalid material coordinator session");
        }
        render::text_check(dimension, 64, false);
        expected_ = {epoch, world, camera, 0, 0};
        dimension_ = std::move(dimension);
        last_epoch_ = epoch;
        model_capture_ = 0;
        model_frame_ = 0;
        texture_capture_ = 0;
        texture_frame_ = 0;
        models_received_ = 0;
        textures_received_ = 0;
        last_time_ = now;
        state_ = CoordinationState::active;
    }

    bool feed_models(std::span<const std::uint8_t> bytes, TimePoint now = Clock::now()) {
        return feed(Channel::models, models_, bytes, now);
    }

    bool feed_textures(std::span<const std::uint8_t> bytes, TimePoint now = Clock::now()) {
        return feed(Channel::textures, textures_, bytes, now);
    }

    // The owner also polls while idle; completed materials have no assembly deadline.
    bool poll(TimePoint now = Clock::now()) {
        check_owner();
        if (state_ != CoordinationState::active) return false;
        if (now < last_time_) return reject(CoordinationState::rejected);
        last_time_ = now;
        if ((models_.filled != 0 && expired(models_.started, now))
            || (textures_.filled != 0 && expired(textures_.started, now))) {
            return reject(CoordinationState::timed_out);
        }
        for (const auto& [capture, pending] : pending_) {
            (void)capture;
            if (expired(pending.started, now)) return reject(CoordinationState::timed_out);
        }
        return true;
    }

    void end_of_stream(Channel channel) {
        check_owner();
        if (state_ != CoordinationState::active) return;
        const bool partial = channel == Channel::models ? models_.filled != 0
                                                       : textures_.filled != 0;
        reset(partial ? CoordinationState::truncated
                      : !pending_.empty() || buffered_bytes() != 0
                          ? CoordinationState::incomplete : CoordinationState::ended);
    }

    void fail(Channel channel) {
        check_owner();
        (void)channel;
        if (state_ == CoordinationState::active) reset(CoordinationState::io_failed);
    }

    void stop() { check_owner(); reset(CoordinationState::stopped); }
    CoordinationState state() const { check_owner(); return state_; }
    std::uint64_t epoch() const { check_owner(); return expected_.epoch; }
    std::uint64_t models_received() const { check_owner(); return models_received_; }
    std::uint64_t textures_received() const { check_owner(); return textures_received_; }
    std::size_t pending_captures() const { check_owner(); return pending_.size(); }
    std::size_t buffered_bytes() const { check_owner(); return models_.filled + textures_.filled; }
    std::size_t retained_bytes() const { check_owner(); return usage().bytes; }
    std::size_t retained_records() const { check_owner(); return usage().records; }

    std::size_t ready_positions() const {
        check_owner();
        return static_cast<std::size_t>(std::count_if(positions_.begin(), positions_.end(),
            [](const auto& item) { return item.second.material.has_value(); }));
    }

    std::optional<Material> find(const render::Position& position, TimePoint now = Clock::now()) {
        if (!poll(now)) return std::nullopt;
        const auto found = positions_.find(position);
        return found == positions_.end() ? std::nullopt : found->second.material;
    }

private:
    template<std::size_t Capacity>
    struct Parser {
        std::array<std::uint8_t, Capacity> bytes{};
        std::size_t filled = 0, declared = 0;
        TimePoint started{};

        void clear() {
            std::fill_n(bytes.begin(), filled, std::uint8_t{0});
            filled = 0;
            declared = 0;
            started = {};
        }
    };

    struct Pending {
        render::Identity identity;
        TimePoint started;
        bool model_seen = false, superseded = false;
        render::Position position;
        std::optional<render::Record> model;
        std::set<SpriteKey> required, seen;
        std::map<SpriteKey, texture::Record> sprites;
    };

    struct PositionState {
        std::uint64_t capture = 0;
        std::optional<Material> material;
    };

    struct Usage { std::size_t bytes = 0, records = 0; };

    static std::size_t texture_size(const texture::Record& record) {
        return texture::header_bytes + record.argb.size() * 4;
    }

    Usage usage() const {
        Usage total;
        for (const auto& [capture, pending] : pending_) {
            (void)capture;
            // Charge bounded identity/manifest tombstones as well as live wire payloads.
            total.bytes += render::header_bytes
                + (pending.required.size() + pending.seen.size()) * (96 + 128);
            total.records += (pending.model_seen ? 1U : 0U) + pending.seen.size();
            if (pending.model) total.bytes += pending.model->quads.size() * render::quad_bytes;
            for (const auto& [key, sprite] : pending.sprites) {
                (void)key;
                total.bytes += texture_size(sprite);
            }
        }
        for (const auto& [position, slot] : positions_) {
            (void)position;
            total.bytes += render::header_bytes;
            if (!slot.material) continue;
            const auto& value = *slot.material;
            total.bytes += value.model.quads.size() * render::quad_bytes;
            total.records += 1 + value.sprites.size();
            for (const auto& [key, sprite] : value.sprites) {
                (void)key;
                total.bytes += texture_size(sprite);
            }
        }
        return total;
    }

    bool within_capacity() {
        const auto current = usage();
        if (pending_.size() > max_pending_captures || positions_.size() > max_positions
            || current.records > max_records || current.bytes > max_bytes) {
            return reject(CoordinationState::capacity);
        }
        return true;
    }

    bool same_session(const render::Identity& identity) const {
        return identity.epoch == expected_.epoch && identity.world == expected_.world
            && identity.camera == expected_.camera;
    }

    bool expired(TimePoint start, TimePoint now) const { return now - start >= timeout_; }

    Pending* obtain(const render::Identity& identity, TimePoint started) {
        auto found = pending_.find(identity.capture);
        if (found != pending_.end()) {
            if (found->second.identity != identity) {
                reject(CoordinationState::rejected);
                return nullptr;
            }
            found->second.started = std::min(found->second.started, started);
            return &found->second;
        }
        if (pending_.size() == max_pending_captures) {
            reject(CoordinationState::capacity);
            return nullptr;
        }
        Pending candidate;
        candidate.identity = identity;
        candidate.started = started;
        return &pending_.emplace(identity.capture, std::move(candidate)).first->second;
    }

    void supersede(const render::Position& position) {
        positions_.erase(position);
        for (auto& [capture, pending] : pending_) {
            (void)capture;
            if (pending.model_seen && pending.position == position) {
                pending.superseded = true;
                pending.model.reset();
                pending.sprites.clear();
            }
        }
    }

    bool finish(std::uint64_t capture) {
        auto found = pending_.find(capture);
        if (found == pending_.end() || !found->second.model_seen) return true;
        auto& pending = found->second;
        for (const auto& key : pending.seen) {
            if (!pending.required.contains(key)) return reject(CoordinationState::rejected);
        }
        for (const auto& [key, sprite] : pending.sprites) {
            (void)key;
            if (sprite.kind == texture::Kind::retire) {
                reset(CoordinationState::revoked);
                return true;
            }
        }
        if (pending.required.size() != pending.seen.size()) {
            return texture_capture_ > capture ? reject(CoordinationState::incomplete) : true;
        }
        if (!pending.superseded && pending.model && !pending.required.empty()) {
            auto slot = positions_.find(pending.position);
            if (slot == positions_.end() || slot->second.capture != capture) {
                return reject(CoordinationState::rejected);
            }
            slot->second.material = Material{std::move(*pending.model), std::move(pending.sprites)};
        }
        pending_.erase(found);
        return true;
    }

    bool accept(render::Record record, TimePoint started) {
        const auto id = record.identity;
        if (!same_session(id) || record.key.dimension != dimension_
            || id.capture != model_capture_ + 1 || id.frame <= model_frame_) {
            return reject(CoordinationState::rejected);
        }
        std::set<SpriteKey> required;
        try { required = sprite_manifest(record); }
        catch (const std::invalid_argument&) { return reject(CoordinationState::rejected); }
        auto* pending = obtain(id, started);
        if (!pending) return false;
        const render::Position position{
            record.key.dimension, record.key.x, record.key.y, record.key.z
        };
        supersede(position);
        pending->model_seen = true;
        pending->position = position;
        pending->required = std::move(required);
        if (!pending->required.empty()) {
            positions_.emplace(position, PositionState{id.capture, std::nullopt});
            pending->model = std::move(record);
        }
        model_capture_ = id.capture;
        model_frame_ = id.frame;
        if (!within_capacity() || !finish(id.capture)) return false;
        ++models_received_;
        return true;
    }

    bool accept(texture::Record record, TimePoint started) {
        const render::Identity id{record.identity.epoch, record.identity.world,
            record.identity.camera, record.identity.frame, record.identity.capture};
        if (!same_session(id) || id.capture < texture_capture_
            || (id.capture == texture_capture_ && id.frame != texture_frame_)
            || (id.capture > texture_capture_ && id.frame <= texture_frame_)) {
            return reject(CoordinationState::rejected);
        }
        const SpriteKey key{record.atlas, record.sprite};
        if (!pending_.contains(id.capture) && id.capture <= model_capture_) {
            if (record.kind == texture::Kind::retire) {
                for (const auto& [position, slot] : positions_) {
                    (void)position;
                    if (slot.material && slot.material->model.identity == id
                        && slot.material->sprites.contains(key)) {
                        ++textures_received_;
                        reset(CoordinationState::revoked);
                        return true;
                    }
                }
            }
            return reject(CoordinationState::rejected);
        }
        if (id.capture > model_capture_ && id.capture - model_capture_ > max_pending_captures) {
            return reject(CoordinationState::capacity);
        }
        if (id.capture > texture_capture_) {
            for (const auto& [capture, pending] : pending_) {
                if (capture < id.capture && pending.model_seen
                    && pending.required.size() != pending.seen.size()) {
                    return reject(CoordinationState::incomplete);
                }
            }
        }
        auto* pending = obtain(id, started);
        if (!pending) return false;
        if (pending->seen.contains(key)
            || (pending->model_seen && !pending->required.contains(key))) {
            return reject(CoordinationState::rejected);
        }
        if (pending->seen.size() == max_material_sprites) return reject(CoordinationState::capacity);
        pending->seen.insert(key);
        if (!pending->superseded || record.kind == texture::Kind::retire) {
            pending->sprites.emplace(key, std::move(record));
        }
        texture_capture_ = id.capture;
        texture_frame_ = id.frame;
        if (!within_capacity() || !finish(id.capture)) return false;
        ++textures_received_;
        return true;
    }

    template<std::size_t Capacity>
    bool feed(Channel channel, Parser<Capacity>& parser, std::span<const std::uint8_t> input,
              TimePoint now) {
        if (!poll(now)) return false;
        try {
            std::size_t offset = 0;
            while (offset < input.size()) {
                if (parser.filled == 0) parser.started = now;
                const auto target = parser.declared == 0 ? std::size_t{12} : parser.declared;
                const auto count = std::min(target - parser.filled, input.size() - offset);
                std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), count,
                            parser.bytes.begin() + static_cast<std::ptrdiff_t>(parser.filled));
                offset += count;
                parser.filled += count;
                if (parser.declared == 0 && parser.filled == 12) {
                    const auto length = static_cast<std::size_t>(render::get(parser.bytes, 8, 4));
                    const auto header = channel == Channel::models ? render::header_bytes
                                                                  : texture::header_bytes;
                    const auto unit = channel == Channel::models ? render::quad_bytes : 4;
                    if (length < header || length > Capacity || (length - header) % unit != 0) {
                        return reject(CoordinationState::malformed);
                    }
                    parser.declared = length;
                }
                if (parser.declared != 0 && parser.filled == parser.declared) {
                    const auto bytes = std::span<const std::uint8_t>(parser.bytes.data(), parser.filled);
                    const auto started = parser.started;
                    bool accepted;
                    try {
                        if (channel == Channel::models) {
                            auto record = render::decode(bytes);
                            parser.clear();
                            accepted = accept(std::move(record), started);
                        } else {
                            auto record = texture::decode(bytes);
                            parser.clear();
                            accepted = accept(std::move(record), started);
                        }
                    } catch (const std::invalid_argument&) {
                        return reject(CoordinationState::malformed);
                    }
                    if (!accepted) return false;
                    if (state_ != CoordinationState::active) return offset == input.size();
                }
            }
            return true;
        } catch (...) {
            reset(CoordinationState::io_failed);
            throw;
        }
    }

    bool reject(CoordinationState state) { reset(state); return false; }

    void reset(CoordinationState state) {
        models_.clear();
        textures_.clear();
        pending_.clear();
        positions_.clear();
        expected_ = {};
        dimension_.clear();
        state_ = state;
    }

    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Material coordinator accessed from a foreign thread");
        }
    }

    const std::thread::id owner_;
    const std::chrono::milliseconds timeout_;
    Parser<render::max_bytes> models_;
    Parser<texture::max_bytes> textures_;
    std::map<std::uint64_t, Pending> pending_;
    std::map<render::Position, PositionState> positions_;
    render::Identity expected_;
    std::string dimension_;
    TimePoint last_time_{};
    std::uint64_t last_epoch_ = 0, model_capture_ = 0, model_frame_ = 0;
    std::uint64_t texture_capture_ = 0, texture_frame_ = 0;
    std::uint64_t models_received_ = 0, textures_received_ = 0;
    CoordinationState state_ = CoordinationState::idle;
};

} // namespace harrancraft::material
