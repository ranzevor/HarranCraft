#pragma once

#include <harrancraft/camera_projection.hpp>
#include <harrancraft/geometry_receiver.hpp>
#include <harrancraft/material_coordinator.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace harrancraft::projection {

// GeometryWire v1 has no camera/frame fields. The controlled caller supplies this
// association; matching position/state alone does not establish temporal identity.
struct GeometryAssociation {
    CaptureKey guest_capture;
    std::uint64_t epoch = 0, revision = 0;
    render::Position position;
    std::string canonical_state;
    bool operator==(const GeometryAssociation&) const = default;
};

inline bool valid(const GeometryAssociation& association) {
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto& key = association.guest_capture;
    if (!valid_capture(key) || key.epoch > limit || key.world > limit || key.camera > limit
        || key.frame > limit || key.capture > limit || association.epoch != key.epoch
        || association.revision == 0 || association.revision > limit) return false;
    try {
        render::text_check(association.position.dimension, 64, false);
        render::text_check(association.canonical_state, 192, false);
    } catch (const std::invalid_argument&) { return false; }
    return true;
}

struct ProjectionKey {
    std::uint64_t serial = 0, calibration_revision = 0;
    CaptureKey guest_capture, host_capture;
    std::uint64_t guest_configuration_revision = 0, host_configuration_revision = 0;
    GeometryAssociation geometry;
    bool operator==(const ProjectionKey&) const = default;
};

struct ProjectedCapture {
    ProjectionKey key;
    bool reverses_orientation = false;
    std::vector<std::array<ProjectedPoint, 4>> quads;
    std::vector<std::array<ProjectedPoint, 8>> boxes;
};

// Owns one bounded CPU projection. Sources outlive this object and share its owner
// thread. The owner polls while idle and revalidates the key before consuming a copy.
class CaptureProjector {
public:
    using Clock = material::MaterialCoordinator::Clock;
    using TimePoint = material::MaterialCoordinator::TimePoint;
    static constexpr std::size_t max_points = render::max_quads * 4 + geometry::max_boxes * 8;

    CaptureProjector(host::Observer& observer, material::MaterialCoordinator& materials,
                     geometry::GeometryReceiver& geometry)
        : owner_(std::this_thread::get_id()), observer_(observer), materials_(materials),
          geometry_(geometry) {}

    CaptureProjector(const CaptureProjector&) = delete;
    CaptureProjector& operator=(const CaptureProjector&) = delete;

    std::optional<ProjectionKey> capture(
        const Calibration& calibration, const CameraContract& guest,
        const CameraContract& host_camera, const GeometryAssociation& association,
        const host::Evidence& evidence, TimePoint now = Clock::now()) {
        check_owner();
        active_.reset();
        if (!valid(calibration) || !valid(guest) || !valid(host_camera) || !valid(association)
            || association.guest_capture != guest.key || association.epoch != guest.key.epoch
            || calibration.dimension != association.position.dimension
            || serial_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        const auto mapping = axis_map(calibration);
        if (!mapping) return std::nullopt;
        Context context{calibration, guest, host_camera, association};
        if (!follows(context)) return std::nullopt;
        const auto sources = read_sources(context, evidence, now);
        if (!sources) return std::nullopt;

        ProjectedCapture pending;
        pending.key = {serial_ + 1, calibration.revision, guest.key, host_camera.key,
            guest.configuration_revision, host_camera.configuration_revision, association};
        pending.reverses_orientation = calibration.reverses_orientation;
        pending.quads.reserve(sources->model.quads.size());
        pending.boxes.reserve(sources->shape.boxes.size());
        const auto& block = association.position;
        for (const auto& quad : sources->model.quads) {
            std::array<ProjectedPoint, 4> projected;
            for (std::size_t index = 0; index < quad.vertices.size(); ++index) {
                const auto& vertex = quad.vertices[index];
                const auto point = project_pair(calibration, guest, host_camera,
                    {static_cast<double>(block.x) + vertex.x,
                     static_cast<double>(block.y) + vertex.y,
                     static_cast<double>(block.z) + vertex.z});
                if (!point) return std::nullopt;
                projected[index] = *point;
            }
            pending.quads.push_back(std::move(projected));
        }
        for (const auto& box : sources->shape.boxes) {
            std::array<ProjectedPoint, 8> projected;
            for (std::size_t corner = 0; corner < projected.size(); ++corner) {
                const auto point = project_pair(calibration, guest, host_camera,
                    {static_cast<double>(block.x) + ((corner & 1U) == 0 ? box.min_x : box.max_x),
                     static_cast<double>(block.y) + ((corner & 2U) == 0 ? box.min_y : box.max_y),
                     static_cast<double>(block.z) + ((corner & 4U) == 0 ? box.min_z : box.max_z)});
                if (!point) return std::nullopt;
                projected[corner] = *point;
            }
            pending.boxes.push_back(std::move(projected));
        }
        std::optional<ProjectionKey> result{pending.key};
        // Commit only after every point, source and fallible result copy has been checked.
        last_ = std::move(context);
        active_ = std::move(pending);
        ++serial_;
        return result;
    }

    bool poll(const host::Evidence& evidence, TimePoint now = Clock::now()) {
        check_owner();
        if (!active_) return false;
        try {
            if (read_sources(*last_, evidence, now)) return true;
        } catch (...) {
            active_.reset();
            throw;
        }
        active_.reset();
        return false;
    }

    std::optional<ProjectionKey> current_key(
        const host::Evidence& evidence, TimePoint now = Clock::now()) {
        if (!poll(evidence, now)) return std::nullopt;
        try { return active_->key; }
        catch (...) { active_.reset(); throw; }
    }

    std::optional<ProjectedCapture> snapshot(
        const ProjectionKey& key, const host::Evidence& evidence, TimePoint now = Clock::now()) {
        if (!poll(evidence, now) || active_->key != key) return std::nullopt;
        try { return active_; }
        catch (...) { active_.reset(); throw; }
    }

    void stop() { check_owner(); active_.reset(); }

    std::size_t retained_points() const {
        check_owner();
        return active_ ? active_->quads.size() * 4 + active_->boxes.size() * 8 : 0;
    }

private:
    struct Context {
        Calibration calibration;
        CameraContract guest, host_camera;
        GeometryAssociation association;
    };
    struct Sources { render::Record model; geometry::Frame shape; };

    static CaptureKey capture_key(const render::Identity& identity) {
        return {identity.epoch, identity.world, identity.camera, identity.frame, identity.capture};
    }

    static bool camera_follows(const CameraContract& next, const CameraContract& previous) {
        const auto& a = next.key;
        const auto& b = previous.key;
        if (a.epoch < b.epoch) return false;
        if (a.epoch > b.epoch) return true;
        if (a.world != b.world || a.frame <= b.frame || a.capture <= b.capture) return false;
        if (a.camera != b.camera) return true;
        if (next.configuration_revision < previous.configuration_revision) return false;
        return next.configuration_revision > previous.configuration_revision
            || (next.perspective == previous.perspective && next.viewport == previous.viewport
                && next.max_position_error == previous.max_position_error);
    }

    bool follows(const Context& next) const {
        if (!last_) return true;
        if (!camera_follows(next.guest, last_->guest)
            || !camera_follows(next.host_camera, last_->host_camera)) return false;
        if (next.association.epoch == last_->association.epoch
            && next.association.revision <= last_->association.revision) return false;
        if (next.calibration.revision < last_->calibration.revision) return false;
        return next.calibration.revision > last_->calibration.revision
            || next.calibration == last_->calibration;
    }

    static bool matches_projection(const Matrix4& observed, const Matrix4& expected) {
        if (!host::finite(observed)) return false;
        for (std::size_t index = 0; index < observed.size(); ++index) {
            const double scale = std::max(std::abs(observed[index]), std::abs(expected[index]));
            if (std::abs(observed[index] - expected[index]) > 1e-6 * scale) return false;
        }
        return true;
    }

    std::optional<Sources> read_sources(
        const Context& context, const host::Evidence& evidence, TimePoint now) {
        const auto& guest = context.guest;
        const auto& camera = context.host_camera;
        const auto& association = context.association;
        const auto observed = observer_.snapshot(evidence, camera.key);
        const auto expected_projection = perspective_matrix(camera.perspective);
        if (!observed || observed->view != camera.view || observed->position != camera.position
            || !expected_projection || !matches_projection(observed->projection, *expected_projection)) {
            return std::nullopt;
        }
        auto material = materials_.find(association.position, now);
        if (!material || capture_key(material->model.identity) != guest.key
            || material->model.kind != render::Kind::snapshot
            || material->model.state != association.canonical_state
            || Vector3{material->model.pose.x, material->model.pose.y, material->model.pose.z}
                != guest.position) return std::nullopt;
        auto receipt = geometry_.last_record();
        if (geometry_.state() != geometry::ReceiverState::active || !receipt
            || receipt->kind != geometry::Kind::snapshot || receipt->epoch != association.epoch
            || receipt->revision != association.revision
            || receipt->key.dimension != association.position.dimension
            || receipt->key.x != association.position.x || receipt->key.y != association.position.y
            || receipt->key.z != association.position.z || receipt->state != association.canonical_state) {
            return std::nullopt;
        }
        const auto candidate = geometry_.find({receipt->key.dimension,
            receipt->key.x, receipt->key.y, receipt->key.z});
        if (receipt->boxes.empty()) {
            if (candidate) return std::nullopt;
        } else {
            if (!candidate || candidate->revision != receipt->revision
                || candidate->canonical_state != receipt->state
                || candidate->boxes.size() != receipt->boxes.size()) return std::nullopt;
            const auto mapping = axis_map(context.calibration);
            if (!mapping) return std::nullopt;
            for (std::size_t index = 0; index < receipt->boxes.size(); ++index) {
                const auto mapped = geometry::map_box(*mapping, receipt->key, receipt->boxes[index]);
                if (!mapped || *mapped != candidate->boxes[index]) return std::nullopt;
            }
        }
        return Sources{std::move(material->model), std::move(*receipt)};
    }

    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Capture projection accessed from another thread");
        }
    }

    const std::thread::id owner_;
    host::Observer& observer_;
    material::MaterialCoordinator& materials_;
    geometry::GeometryReceiver& geometry_;
    std::optional<Context> last_;
    std::optional<ProjectedCapture> active_;
    std::uint64_t serial_ = 0;
};

} // namespace harrancraft::projection
