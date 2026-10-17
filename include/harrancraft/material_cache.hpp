#pragma once

#include <harrancraft/render_wire.hpp>
#include <harrancraft/texture_wire.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace harrancraft::material {

struct SpriteKey {
    std::string atlas, sprite;
    auto operator<=>(const SpriteKey&) const = default;
};

inline constexpr std::size_t max_material_sprites = 16;

inline std::set<SpriteKey> sprite_manifest(const render::Record& model) {
    std::set<SpriteKey> requested;
    for (const auto& quad : model.quads) {
        texture::check_text(quad.atlas, 96);
        texture::check_text(quad.sprite, 128);
        requested.insert({quad.atlas, quad.sprite});
        if (requested.size() > max_material_sprites) {
            throw std::invalid_argument("Model exceeds material sprite capacity");
        }
    }
    return requested;
}

enum class State { idle, pending, ready, retired, invalid, ended, stopped };

// One current model capture owns all its CPU-side source sprites. No GPU handles.
class MaterialCache {
public:
    static constexpr std::size_t max_sprites = max_material_sprites;

    MaterialCache() : owner_(std::this_thread::get_id()) {}

    void begin(std::uint64_t epoch, std::uint64_t world, std::uint64_t camera,
               std::string dimension) {
        check_owner();
        retire(State::stopped);
        constexpr auto bound = static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());
        if (epoch <= last_epoch_ || epoch == 0 || world == 0 || camera == 0
            || epoch > bound || world > bound || camera > bound) {
            throw std::invalid_argument("Invalid material session");
        }
        render::text_check(dimension, 64, false);
        epoch_ = epoch;
        world_ = world;
        camera_ = camera;
        dimension_ = std::move(dimension);
        last_epoch_ = epoch;
        last_frame_ = 0;
        last_capture_ = 0;
        state_ = State::pending;
    }

    bool begin_model(const render::Record& model) {
        check_owner();
        if (state_ != State::pending && state_ != State::ready
            && state_ != State::retired) return false;
        try { render::check(model); }
        catch (const std::invalid_argument&) {
            retire(State::invalid);
            return false;
        }
        const auto& id = model.identity;
        if (id.epoch != epoch_ || id.world != world_ || id.camera != camera_
            || model.key.dimension != dimension_
            || id.frame <= last_frame_
            || last_capture_ == std::numeric_limits<std::uint64_t>::max()
            || id.capture != last_capture_ + 1) {
            retire(State::invalid);
            return false;
        }
        std::set<SpriteKey> requested;
        try {
            requested = sprite_manifest(model);
        } catch (const std::invalid_argument&) {
            retire(State::invalid);
            return false;
        }
        required_ = std::move(requested);
        sprites_.clear();
        capture_ = id;
        last_frame_ = id.frame;
        last_capture_ = id.capture;
        if (model.kind == render::Kind::retire || model.quads.empty()) {
            state_ = State::retired;
        } else {
            state_ = State::pending;
        }
        return true;
    }

    bool accept(const texture::Record& sprite) {
        check_owner();
        if (state_ != State::pending || required_.empty()) {
            retire(State::invalid);
            return false;
        }
        try { texture::check(sprite); }
        catch (const std::invalid_argument&) {
            retire(State::invalid);
            return false;
        }
        const auto& id = sprite.identity;
        if (id.epoch != capture_.epoch || id.world != capture_.world
            || id.camera != capture_.camera || id.frame != capture_.frame
            || id.capture != capture_.capture) {
            retire(State::invalid);
            return false;
        }
        const SpriteKey key{sprite.atlas, sprite.sprite};
        if (!required_.contains(key) || sprites_.contains(key)) {
            retire(State::invalid);
            return false;
        }
        if (sprite.kind == texture::Kind::retire) {
            retire(State::retired);
            return true;
        }
        sprites_.emplace(key, sprite);
        if (sprites_.size() == required_.size()) state_ = State::ready;
        return true;
    }

    void end_of_stream() {
        check_owner();
        if (state_ == State::pending || state_ == State::ready
            || state_ == State::retired) retire(State::ended);
    }

    void stop() {
        check_owner();
        retire(State::stopped);
    }

    State state() const { check_owner(); return state_; }
    bool ready() const { check_owner(); return state_ == State::ready; }
    std::size_t expected() const { check_owner(); return required_.size(); }
    std::size_t received() const { check_owner(); return sprites_.size(); }
    std::uint64_t epoch() const { check_owner(); return epoch_; }

    std::optional<texture::Record> find(const SpriteKey& key) const {
        check_owner();
        if (state_ != State::ready) return std::nullopt;
        const auto it = sprites_.find(key);
        return it == sprites_.end() ? std::nullopt : std::optional<texture::Record>(it->second);
    }

private:
    void retire(State reason) {
        sprites_.clear();
        required_.clear();
        dimension_.clear();
        epoch_ = 0;
        world_ = 0;
        camera_ = 0;
        capture_ = {};
        state_ = reason;
    }

    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Material cache accessed from a foreign thread");
        }
    }

    const std::thread::id owner_;
    std::uint64_t epoch_ = 0, world_ = 0, camera_ = 0, last_epoch_ = 0;
    std::uint64_t last_frame_ = 0, last_capture_ = 0;
    render::Identity capture_;
    std::string dimension_;
    std::set<SpriteKey> required_;
    std::map<SpriteKey, texture::Record> sprites_;
    State state_ = State::idle;
};

} // namespace harrancraft::material
