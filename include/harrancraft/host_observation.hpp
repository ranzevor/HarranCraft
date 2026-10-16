#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace harrancraft::host {

// Expected fingerprints must come from independently checked game files, not a host callback.
struct ImageIdentity {
    std::array<std::uint8_t, 32> sha256{};
    std::uint64_t file_bytes = 0;
    std::uint16_t machine = 0;
    bool operator==(const ImageIdentity&) const = default;
};

inline bool valid_image(const ImageIdentity& image) {
    return image.machine == 0x8664 && image.file_bytes != 0
        && std::any_of(image.sha256.begin(), image.sha256.end(), [](auto b) { return b != 0; });
}

enum class Mode { unknown, single_player, cooperative, invasion };
enum class Lifecycle { unknown, loading, playing, shutting_down };
enum class ReadPhase { unverified, window_or_present, verified_engine_read };

// The future game adapter is responsible for establishing this evidence independently.
// These values alone do not authorize calling an engine function or changing host state.
struct Evidence {
    Mode mode = Mode::unknown;
    Lifecycle lifecycle = Lifecycle::unknown;
    ReadPhase phase = ReadPhase::unverified;
    std::uint64_t world = 0;
    bool operator==(const Evidence&) const = default;
};

inline bool readable(const Evidence& evidence) {
    return evidence.mode == Mode::single_player
        && evidence.lifecycle == Lifecycle::playing
        && evidence.phase == ReadPhase::verified_engine_read && evidence.world != 0;
}

struct CaptureKey {
    std::uint64_t epoch = 0;
    std::uint64_t world = 0;
    std::uint64_t camera = 0;
    std::uint64_t frame = 0;
    std::uint64_t capture = 0;
    bool operator==(const CaptureKey&) const = default;
};

using Matrix4 = std::array<double, 16>; // Column-major, with column vectors.
using Vector3 = std::array<double, 3>;
using Vector4 = std::array<double, 4>;

struct CameraSample {
    CaptureKey key;
    Matrix4 view{};
    Matrix4 projection{};
    Vector3 position{};
    bool operator==(const CameraSample&) const = default;
};

inline bool finite(const auto& values) {
    return std::all_of(values.begin(), values.end(), [](double value) {
        return std::isfinite(value);
    });
}

inline bool valid_sample(const CameraSample& sample) {
    const auto& key = sample.key;
    return key.epoch != 0 && key.world != 0 && key.camera != 0
        && key.frame != 0 && key.capture != 0
        && finite(sample.view) && finite(sample.projection) && finite(sample.position);
}

// An isolated read-only lifecycle gate. All methods, including snapshots, are owner-thread only.
class Observer {
public:
    explicit Observer(std::vector<ImageIdentity> allowlist)
        : allowlist_(std::move(allowlist)), owner_(std::this_thread::get_id()) {
        if (!std::all_of(allowlist_.begin(), allowlist_.end(), valid_image)) {
            throw std::invalid_argument("Invalid host image allowlist");
        }
    }

    std::optional<std::uint64_t> begin(const ImageIdentity& image, const Evidence& evidence) {
        check_owner();
        revoke();
        if (!valid_image(image) || !readable(evidence)
            || std::find(allowlist_.begin(), allowlist_.end(), image) == allowlist_.end()) {
            return std::nullopt;
        }
        if (last_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Host observation epoch exhausted");
        }
        ++last_epoch_;
        active_ = evidence;
        return last_epoch_;
    }

    bool capture(const Evidence& evidence, const CameraSample& sample) {
        check_owner();
        if (!check_context(evidence)) return false;
        const auto& key = sample.key;
        if (!valid_sample(sample) || key.epoch != last_epoch_ || key.world != evidence.world) {
            return false;
        }
        if (last_ && (key.frame <= last_->key.frame || key.capture <= last_->key.capture)) {
            return false;
        }
        last_ = sample;
        return true;
    }

    std::optional<CameraSample> snapshot(const Evidence& evidence, const CaptureKey& key) {
        check_owner();
        if (!check_context(evidence) || !last_ || last_->key != key) return std::nullopt;
        return last_;
    }

    void stop() {
        check_owner();
        revoke();
    }

private:
    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Host observation accessed from another thread");
        }
    }

    bool check_context(const Evidence& evidence) {
        if (!active_ || !readable(evidence) || evidence != *active_) {
            revoke();
            return false;
        }
        return true;
    }

    void revoke() {
        last_.reset();
        active_.reset();
    }

    const std::vector<ImageIdentity> allowlist_;
    const std::thread::id owner_;
    std::uint64_t last_epoch_ = 0;
    std::optional<Evidence> active_;
    std::optional<CameraSample> last_;
};

inline Vector4 transform(const Matrix4& matrix, const Vector4& point) {
    Vector4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            result[row] += matrix[col * 4 + row] * point[col];
        }
    }
    return result;
}

// Produces homogeneous clip coordinates, not screen coordinates or host-depth occlusion.
// Axis units, handedness, view/projection conventions and scene depth need host calibration.
inline std::optional<Vector4> project_clip(
    const CameraSample& sample, const Matrix4& host_from_guest, const Vector3& guest_point) {
    if (!valid_sample(sample) || !finite(host_from_guest) || !finite(guest_point)
        || host_from_guest[3] != 0 || host_from_guest[7] != 0
        || host_from_guest[11] != 0 || host_from_guest[15] != 1) {
        return std::nullopt;
    }
    const Vector4 point{guest_point[0], guest_point[1], guest_point[2], 1};
    const auto host_point = transform(host_from_guest, point);
    const auto view_point = transform(sample.view, host_point);
    const auto clip_point = transform(sample.projection, view_point);
    if (!finite(host_point) || !finite(view_point) || !finite(clip_point)) {
        return std::nullopt;
    }
    return clip_point;
}

} // namespace harrancraft::host
