#pragma once

#include <harrancraft/geometry_stage.hpp>
#include <harrancraft/host_observation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace harrancraft::projection {

using host::CaptureKey;
using host::Matrix4;
using host::Vector3;
using host::Vector4;

inline constexpr std::uint32_t contract_version = 1;
inline constexpr double basis_tolerance = 1e-9;

enum class Handedness { left, right };
enum class DepthRange { zero_to_one, minus_one_to_one };
enum class ScreenOrigin { top_left, bottom_left };

struct Perspective {
    std::uint32_t version = contract_version;
    double vertical_fov_radians = 0;
    double aspect = 0;
    double near_distance = 0;
    double far_distance = 0;
    Handedness handedness = Handedness::right;
    DepthRange depth_range = DepthRange::minus_one_to_one;
    bool reverse_z = false;
    bool operator==(const Perspective&) const = default;
};

struct Viewport {
    std::int32_t x = 0, y = 0;
    std::uint32_t width = 0, height = 0;
    ScreenOrigin origin = ScreenOrigin::top_left;
    bool operator==(const Viewport&) const = default;
};

struct CameraContract {
    CaptureKey key;
    std::uint64_t configuration_revision = 0;
    Matrix4 view{};
    Vector3 position{};
    Perspective perspective;
    Viewport viewport;
    double max_position_error = 1e-6;
    bool operator==(const CameraContract&) const = default;
};

struct Calibration {
    std::uint32_t version = contract_version;
    std::uint64_t revision = 0;
    std::string dimension;
    Matrix4 host_from_guest{};
    double units_per_block = 1;
    bool reverses_orientation = false;
    // Absolute round-trip error in guest block units, independent of world origin.
    double max_roundtrip_error = 1e-6;
    bool operator==(const Calibration&) const = default;
};

struct ScreenPoint {
    Vector3 ndc{};
    std::array<double, 2> pixels{};
    double depth = 0;
    bool inside = false;
    bool operator==(const ScreenPoint&) const = default;
};

struct ProjectedPoint {
    Vector3 guest_world{}, host_world{};
    Vector4 guest_clip{}, host_clip{};
    std::optional<ScreenPoint> guest_screen, host_screen;
    bool operator==(const ProjectedPoint&) const = default;
};

struct Correspondence { Vector3 guest{}, host{}; };

inline Matrix4 identity_matrix() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

inline bool valid_capture(const CaptureKey& key) {
    return key.epoch != 0 && key.world != 0 && key.camera != 0
        && key.frame != 0 && key.capture != 0;
}

inline double dot(const Vector3& a, const Vector3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline double determinant(const std::array<Vector3, 3>& columns) {
    const auto& a = columns[0];
    const auto& b = columns[1];
    const auto& c = columns[2];
    return a[0] * (b[1] * c[2] - b[2] * c[1])
        - b[0] * (a[1] * c[2] - a[2] * c[1])
        + c[0] * (a[1] * b[2] - a[2] * b[1]);
}

inline bool orthogonal(const std::array<Vector3, 3>& columns, bool reversed) {
    for (std::size_t a = 0; a < 3; ++a) {
        if (!host::finite(columns[a])
            || std::abs(dot(columns[a], columns[a]) - 1) > basis_tolerance) return false;
        for (std::size_t b = a + 1; b < 3; ++b) {
            if (std::abs(dot(columns[a], columns[b])) > basis_tolerance) return false;
        }
    }
    return std::abs(determinant(columns) - (reversed ? -1 : 1)) <= 4 * basis_tolerance;
}

inline bool affine(const Matrix4& matrix) {
    return host::finite(matrix) && matrix[3] == 0 && matrix[7] == 0
        && matrix[11] == 0 && matrix[15] == 1;
}

inline bool valid(const Perspective& value) {
    return value.version == contract_version
        && std::isfinite(value.vertical_fov_radians)
        && value.vertical_fov_radians > 0 && value.vertical_fov_radians < std::numbers::pi
        && std::isfinite(value.aspect) && value.aspect > 0
        && std::isfinite(value.near_distance) && value.near_distance > 0
        && std::isfinite(value.far_distance) && value.far_distance > value.near_distance
        && (value.handedness == Handedness::left || value.handedness == Handedness::right)
        && (value.depth_range == DepthRange::zero_to_one
            || value.depth_range == DepthRange::minus_one_to_one);
}

inline bool valid(const Viewport& value) {
    if (value.width == 0 || value.height == 0
        || (value.origin != ScreenOrigin::top_left && value.origin != ScreenOrigin::bottom_left)) {
        return false;
    }
    const auto right = static_cast<std::int64_t>(value.x) + value.width;
    const auto end = static_cast<std::int64_t>(value.y) + value.height;
    return right <= std::numeric_limits<std::int32_t>::max()
        && end <= std::numeric_limits<std::int32_t>::max();
}

// Column vectors: w is positive forward distance for either view-space handedness.
inline std::optional<Matrix4> perspective_matrix(const Perspective& value) {
    if (!valid(value)) return std::nullopt;
    const double q = 1 / std::tan(value.vertical_fov_radians / 2);
    double near_depth = value.depth_range == DepthRange::zero_to_one ? 0 : -1;
    double far_depth = 1;
    if (value.reverse_z) std::swap(near_depth, far_depth);
    const double range = value.far_distance - value.near_distance;
    const double a = far_depth + (far_depth - near_depth) * (value.near_distance / range);
    const double b = (near_depth - far_depth) * value.near_distance
        * (value.far_distance / range);
    const double sign = value.handedness == Handedness::left ? 1 : -1;
    Matrix4 result{};
    result[0] = q / value.aspect;
    result[5] = q;
    result[10] = sign * a;
    result[11] = sign;
    result[14] = b;
    if (!host::finite(result) || result[0] <= 0 || result[5] <= 0 || b == 0) {
        return std::nullopt;
    }
    return result;
}

inline std::optional<Matrix4> view_matrix(
    const Vector3& position, const Vector3& right, const Vector3& up,
    const Vector3& forward, Handedness handedness) {
    if (!host::finite(position)
        || (handedness != Handedness::left && handedness != Handedness::right)) {
        return std::nullopt;
    }
    const double sign = handedness == Handedness::left ? 1 : -1;
    const Vector3 z{sign * forward[0], sign * forward[1], sign * forward[2]};
    if (!orthogonal({right, up, z}, false)) return std::nullopt;
    Matrix4 result = identity_matrix();
    for (std::size_t col = 0; col < 3; ++col) {
        result[col * 4] = right[col];
        result[col * 4 + 1] = up[col];
        result[col * 4 + 2] = z[col];
    }
    result[12] = -dot(right, position);
    result[13] = -dot(up, position);
    result[14] = -dot(z, position);
    return host::finite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

inline bool valid(const CameraContract& value) {
    if (!valid_capture(value.key) || value.configuration_revision == 0
        || !affine(value.view) || !host::finite(value.position)
        || !perspective_matrix(value.perspective) || !valid(value.viewport)
        || !std::isfinite(value.max_position_error) || value.max_position_error <= 0) {
        return false;
    }
    std::array<Vector3, 3> rows{};
    for (std::size_t row = 0; row < 3; ++row) {
        rows[row] = {value.view[row], value.view[4 + row], value.view[8 + row]};
    }
    if (!orthogonal(rows, false)) return false;
    const double aspect = static_cast<double>(value.viewport.width) / value.viewport.height;
    if (std::abs(value.perspective.aspect - aspect) > basis_tolerance * aspect) return false;
    const auto origin = host::transform(value.view,
        {value.position[0], value.position[1], value.position[2], 1});
    if (!host::finite(origin)) return false;
    return std::abs(origin[0]) <= value.max_position_error
        && std::abs(origin[1]) <= value.max_position_error
        && std::abs(origin[2]) <= value.max_position_error;
}

inline bool valid(const Calibration& value) {
    if (value.version != contract_version || value.revision == 0
        || !affine(value.host_from_guest) || !std::isfinite(value.units_per_block)
        || value.units_per_block <= 0 || !std::isfinite(value.max_roundtrip_error)
        || value.max_roundtrip_error <= 0) return false;
    try { geometry::check_text(value.dimension, 64, false); }
    catch (const std::invalid_argument&) { return false; }
    if (value.dimension.find(' ') != std::string::npos) return false;
    std::array<Vector3, 3> columns{};
    for (std::size_t col = 0; col < 3; ++col) {
        for (std::size_t row = 0; row < 3; ++row) {
            columns[col][row] = value.host_from_guest[col * 4 + row] / value.units_per_block;
        }
    }
    return orthogonal(columns, value.reverses_orientation);
}

inline std::optional<Matrix4> inverse_matrix(const Calibration& value) {
    if (!valid(value)) return std::nullopt;
    Matrix4 result = identity_matrix();
    for (std::size_t col = 0; col < 3; ++col) {
        for (std::size_t row = 0; row < 3; ++row) {
            result[col * 4 + row] = (value.host_from_guest[row * 4 + col]
                / value.units_per_block) / value.units_per_block;
        }
    }
    for (std::size_t row = 0; row < 3; ++row) {
        result[12 + row] = -(result[row] * value.host_from_guest[12]
            + result[4 + row] * value.host_from_guest[13]
            + result[8 + row] * value.host_from_guest[14]);
    }
    return host::finite(result) ? std::optional<Matrix4>(result) : std::nullopt;
}

inline std::optional<Vector3> map_point(const Calibration& value, const Vector3& point) {
    const auto inverse = inverse_matrix(value);
    if (!inverse || !host::finite(point)) return std::nullopt;
    const auto mapped = host::transform(value.host_from_guest, {point[0], point[1], point[2], 1});
    if (!host::finite(mapped)) return std::nullopt;
    const auto restored = host::transform(*inverse, mapped);
    if (!host::finite(restored)) return std::nullopt;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(restored[axis] - point[axis]) > value.max_roundtrip_error) return std::nullopt;
    }
    return Vector3{mapped[0], mapped[1], mapped[2]};
}

inline std::optional<Vector3> unmap_point(const Calibration& value, const Vector3& point) {
    const auto inverse = inverse_matrix(value);
    if (!inverse || !host::finite(point)) return std::nullopt;
    const auto mapped = host::transform(*inverse, {point[0], point[1], point[2], 1});
    if (!host::finite(mapped)) return std::nullopt;
    const Vector3 guest{mapped[0], mapped[1], mapped[2]};
    const auto restored = map_point(value, guest);
    if (!restored) return std::nullopt;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs((*restored)[axis] - point[axis]) / value.units_per_block
            > value.max_roundtrip_error) return std::nullopt;
    }
    return guest;
}

inline std::optional<Calibration> calibration_from_axes(
    const geometry::CoordinateMap& mapping, std::uint64_t revision) {
    if (!geometry::valid_map(mapping)) return std::nullopt;
    Calibration value;
    value.revision = revision;
    value.dimension = mapping.dimension;
    value.units_per_block = std::abs(mapping.scale[0]);
    value.host_from_guest = identity_matrix();
    std::fill_n(value.host_from_guest.begin(), 12, 0.0);
    std::array<Vector3, 3> columns{};
    for (std::size_t row = 0; row < 3; ++row) {
        if (std::abs(mapping.scale[row]) != value.units_per_block) return std::nullopt;
        const auto col = mapping.source_axis[row];
        value.host_from_guest[col * 4 + row] = mapping.scale[row];
        value.host_from_guest[12 + row] = mapping.origin[row];
        columns[col][row] = mapping.scale[row] / value.units_per_block;
    }
    value.reverses_orientation = determinant(columns) < 0;
    return valid(value) ? std::optional<Calibration>(std::move(value)) : std::nullopt;
}

// Only exact signed axis permutations preserve the existing collision AABBs.
inline std::optional<geometry::CoordinateMap> axis_map(const Calibration& value) {
    if (!valid(value)) return std::nullopt;
    geometry::CoordinateMap result;
    result.dimension = value.dimension;
    for (std::size_t row = 0; row < 3; ++row) {
        bool found = false;
        for (std::size_t col = 0; col < 3; ++col) {
            const double entry = value.host_from_guest[col * 4 + row];
            if (entry == 0) continue;
            if (found || std::abs(entry) != value.units_per_block) return std::nullopt;
            found = true;
            result.source_axis[row] = static_cast<std::uint8_t>(col);
            result.scale[row] = entry;
        }
        if (!found) return std::nullopt;
        result.origin[row] = value.host_from_guest[12 + row];
    }
    return geometry::valid_map(result) ? std::optional<geometry::CoordinateMap>(result)
                                       : std::nullopt;
}

inline std::optional<double> correspondence_error(
    const Calibration& value, std::span<const Correspondence> points) {
    if (points.empty() || points.size() > 64) return std::nullopt;
    double maximum = 0;
    for (const auto& point : points) {
        const auto mapped = map_point(value, point.guest);
        if (!mapped || !host::finite(point.host)) return std::nullopt;
        const double error = std::hypot((*mapped)[0] - point.host[0],
            (*mapped)[1] - point.host[1], (*mapped)[2] - point.host[2]);
        if (!std::isfinite(error)) return std::nullopt;
        maximum = std::max(maximum, error);
    }
    return maximum;
}

// Outside points retain their coordinates; clipping never clamps them into the viewport.
inline std::optional<ScreenPoint> screen_point(
    const Vector4& clip, const Perspective& perspective, const Viewport& viewport) {
    if (!valid(perspective) || !valid(viewport) || !host::finite(clip) || clip[3] <= 0) {
        return std::nullopt;
    }
    ScreenPoint result;
    const double w = clip[3];
    for (std::size_t axis = 0; axis < 3; ++axis) result.ndc[axis] = clip[axis] / w;
    const double minimum = perspective.depth_range == DepthRange::zero_to_one ? 0 : -w;
    result.inside = clip[0] >= -w && clip[0] <= w && clip[1] >= -w && clip[1] <= w
        && clip[2] >= minimum && clip[2] <= w;
    result.pixels[0] = viewport.x + (result.ndc[0] + 1) * 0.5 * viewport.width;
    const double y = viewport.origin == ScreenOrigin::top_left ? 1 - result.ndc[1]
                                                              : result.ndc[1] + 1;
    result.pixels[1] = viewport.y + y * 0.5 * viewport.height;
    result.depth = perspective.depth_range == DepthRange::zero_to_one ? result.ndc[2]
                                                                     : result.ndc[2] * 0.5 + 0.5;
    if (!host::finite(result.ndc) || !host::finite(result.pixels) || !std::isfinite(result.depth)) {
        return std::nullopt;
    }
    return result;
}

inline std::optional<ProjectedPoint> project_pair(
    const Calibration& calibration, const CameraContract& guest, const CameraContract& host_camera,
    const Vector3& point) {
    if (!valid(guest) || !valid(host_camera)) return std::nullopt;
    const auto mapped = map_point(calibration, point);
    if (!mapped) return std::nullopt;
    const auto guest_projection = perspective_matrix(guest.perspective);
    const auto host_projection = perspective_matrix(host_camera.perspective);
    const Vector4 guest_point{point[0], point[1], point[2], 1};
    const Vector4 host_point{(*mapped)[0], (*mapped)[1], (*mapped)[2], 1};
    ProjectedPoint result;
    result.guest_world = point;
    result.host_world = *mapped;
    result.guest_clip = host::transform(*guest_projection, host::transform(guest.view, guest_point));
    result.host_clip = host::transform(*host_projection, host::transform(host_camera.view, host_point));
    if (!host::finite(result.guest_clip) || !host::finite(result.host_clip)) return std::nullopt;
    result.guest_screen = screen_point(result.guest_clip, guest.perspective, guest.viewport);
    result.host_screen = screen_point(result.host_clip, host_camera.perspective, host_camera.viewport);
    if ((result.guest_clip[3] > 0 && !result.guest_screen)
        || (result.host_clip[3] > 0 && !result.host_screen)) return std::nullopt;
    return result;
}

} // namespace harrancraft::projection
