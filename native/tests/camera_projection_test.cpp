#include <harrancraft/camera_projection.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace harrancraft::projection;
namespace geometry = harrancraft::geometry;
namespace host = harrancraft::host;

static std::size_t assertions = 0;

static void require(bool condition, std::string_view reason) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string(reason));
}

static void near(double actual, double expected, std::string_view reason,
                 double tolerance = 1e-12) {
    ++assertions;
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(reason) + ": actual=" + std::to_string(actual)
            + ", expected=" + std::to_string(expected));
    }
}

template<std::size_t Size>
static void near(const std::array<double, Size>& actual,
                 const std::array<double, Size>& expected, std::string_view reason) {
    for (std::size_t index = 0; index < Size; ++index) near(actual[index], expected[index], reason);
}

static Matrix4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

static Perspective perspective(Handedness handedness = Handedness::left,
                               DepthRange depth = DepthRange::zero_to_one,
                               bool reverse = false) {
    return {1, std::numbers::pi / 2, 2, 1, 9, handedness, depth, reverse};
}

static Viewport viewport(ScreenOrigin origin = ScreenOrigin::top_left) {
    return {10, 20, 800, 400, origin};
}

static Calibration calibration() {
    return {1, 7, "minecraft:overworld", identity(), 1, false, 1e-6};
}

static CameraContract camera() {
    return {{1, 2, 3, 4, 5}, 6, identity(), {0, 0, 0}, perspective(), viewport(), 1e-6};
}

static void projection_oracles() {
    struct DepthCase {
        DepthRange range;
        bool reverse;
        double coefficient, constant;
        std::array<double, 3> ndc;
        std::array<double, 3> depth;
    };
    // Analytic values for vertical FOV=90 degrees, aspect=2, near=1, far=9.
    // Microsoft D3DXMatrixPerspectiveFovLH and Khronos ARB_clip_control define
    // the matrix and depth ranges; these literals are independent of the builder.
    constexpr std::array<DepthCase, 4> cases{{
        {DepthRange::zero_to_one, false, 1.125, -1.125, {0, 0.75, 1}, {0, 0.75, 1}},
        {DepthRange::zero_to_one, true, -0.125, 1.125, {1, 0.25, 0}, {1, 0.25, 0}},
        {DepthRange::minus_one_to_one, false, 1.25, -2.25, {-1, 0.5, 1}, {0, 0.75, 1}},
        {DepthRange::minus_one_to_one, true, -1.25, 2.25, {1, -0.5, -1}, {1, 0.25, 0}}
    }};
    constexpr std::array<double, 3> distances{1, 3, 9};
    for (const auto handedness : {Handedness::left, Handedness::right}) {
        const double sign = handedness == Handedness::left ? 1 : -1;
        for (const auto& item : cases) {
            const auto parameters = perspective(handedness, item.range, item.reverse);
            const auto matrix = perspective_matrix(parameters);
            require(matrix.has_value(), "Valid perspective oracle was rejected");
            const Matrix4 expected{
                0.5, 0, 0, 0, 0, 1, 0, 0,
                0, 0, sign * item.coefficient, sign, 0, 0, item.constant, 0
            };
            near(*matrix, expected, "Perspective matrix changed column-vector convention");
            for (std::size_t index = 0; index < distances.size(); ++index) {
                const auto clip = host::transform(*matrix, {0, 0, sign * distances[index], 1});
                near(clip[3], distances[index], "Forward distance did not produce positive w");
                const auto screen = screen_point(clip, parameters, viewport());
                require(screen && screen->inside, "Near, middle or far plane was clipped");
                near(screen->ndc, Vector3{0, 0, item.ndc[index]}, "Depth oracle mismatch");
                near(screen->depth, item.depth[index], "Window depth oracle mismatch");
                near(screen->pixels, std::array<double, 2>{410, 220}, "Center projection moved");
            }
            const auto clip = host::transform(*matrix, {3, 1.5, sign * 3, 1});
            const auto screen = screen_point(clip, parameters, viewport());
            require(screen && screen->inside, "Off-center oracle was clipped");
            near(screen->ndc, Vector3{0.5, 0.5, item.ndc[1]}, "Off-center NDC oracle mismatch");
            near(screen->pixels, std::array<double, 2>{610, 120}, "Pixel oracle mismatch");

            const auto behind = host::transform(*matrix, {0, 0, -sign * 3, 1});
            near(behind[3], -3, "Behind-camera point acquired positive w");
            require(!screen_point(behind, parameters, viewport()), "Behind-camera point was divided");
            const auto eye = host::transform(*matrix, {0, 0, 0, 1});
            near(eye[3], 0, "Eye plane has nonzero homogeneous w");
            require(!screen_point(eye, parameters, viewport()), "Zero w was divided");
        }
    }
}

static void viewport_and_clipping() {
    for (const auto range : {DepthRange::zero_to_one, DepthRange::minus_one_to_one}) {
        for (const bool reverse : {false, true}) {
            const auto parameters = perspective(Handedness::left, range, reverse);
            const double z = range == DepthRange::zero_to_one ? 1 : 0;
            for (const auto origin : {ScreenOrigin::top_left, ScreenOrigin::bottom_left}) {
                auto target = viewport(origin);
                const auto point = screen_point({1, 1, z, 2}, parameters, target);
                require(point && point->inside, "Valid viewport point was rejected");
                near(point->pixels, std::array<double, 2>{610,
                    origin == ScreenOrigin::top_left ? 120.0 : 320.0}, "Viewport origin inverted twice");
                target.width = 1600;
                target.height = 800;
                const auto resized = screen_point({1, 1, z, 2}, parameters, target);
                require(resized && resized->inside, "Same-aspect resize was rejected");
                near(resized->ndc, point->ndc, "Resize changed NDC");
                near(resized->pixels, std::array<double, 2>{1210,
                    origin == ScreenOrigin::top_left ? 220.0 : 620.0}, "Resize did not use new extent");
            }
            for (std::size_t axis = 0; axis < 2; ++axis) {
                for (const double sign : {-1.0, 1.0}) {
                    Vector4 edge{0, 0, z, 2};
                    edge[axis] = sign * 2;
                    const auto boundary = screen_point(edge, parameters, viewport());
                    require(boundary && boundary->inside, "Inclusive side clip boundary was excluded");
                    near(boundary->ndc[axis], sign, "Side boundary NDC was changed");
                    edge[axis] = sign * 2.25;
                    const auto outside = screen_point(edge, parameters, viewport());
                    require(outside && !outside->inside, "Outside side plane was accepted");
                    near(outside->ndc[axis], sign * 1.125, "Outside NDC was clamped");
                    if (axis == 0) {
                        near(outside->pixels[0], sign < 0 ? -40 : 860, "Outside horizontal pixel clamped");
                    } else {
                        near(outside->pixels[1], sign < 0 ? 445 : -5, "Outside vertical pixel clamped");
                    }
                    edge[axis] = sign * 1.75;
                    const auto inside = screen_point(edge, parameters, viewport());
                    require(inside && inside->inside, "Inside side plane was excluded");
                }
            }
            const double minimum = range == DepthRange::zero_to_one ? 0 : -2;
            for (const double endpoint : {minimum, 2.0}) {
                const auto edge = screen_point({0, 0, endpoint, 2}, parameters, viewport());
                require(edge && edge->inside, "Inclusive depth clip boundary was excluded");
            }
            const auto before = screen_point({0, 0, minimum - 0.25, 2}, parameters, viewport());
            const auto after = screen_point({0, 0, 2.25, 2}, parameters, viewport());
            require(before && after && !before->inside && !after->inside,
                    "Depth outside clip volume was accepted");
            near(before->depth, range == DepthRange::zero_to_one ? -0.125 : -0.0625,
                 "Depth below range was clamped");
            near(after->depth, range == DepthRange::zero_to_one ? 1.125 : 1.0625,
                 "Depth above range was clamped");
        }
    }
    auto target = viewport();
    target.x = -10;
    target.y = -20;
    const auto offset = screen_point({0, 0, 1, 2}, perspective(), target);
    require(offset.has_value(), "Signed viewport origin was rejected");
    near(offset->pixels, std::array<double, 2>{390, 180}, "Signed viewport offset lost");
}

static void invalid_projection_inputs() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    auto parameters = perspective();
    parameters.version = 0;
    require(!perspective_matrix(parameters), "Unknown perspective version was accepted");
    for (const double value : {0.0, -1.0, std::numbers::pi, nan, infinity}) {
        parameters = perspective();
        parameters.vertical_fov_radians = value;
        require(!perspective_matrix(parameters), "Invalid vertical FOV was accepted");
    }
    for (const double value : {0.0, -1.0, nan, infinity}) {
        parameters = perspective();
        parameters.aspect = value;
        require(!perspective_matrix(parameters), "Invalid aspect was accepted");
        parameters = perspective();
        parameters.near_distance = value;
        require(!perspective_matrix(parameters), "Invalid near distance was accepted");
        parameters = perspective();
        parameters.far_distance = value;
        require(!perspective_matrix(parameters), "Invalid far distance was accepted");
    }
    for (const double value : {0.5, 1.0}) {
        parameters = perspective();
        parameters.far_distance = value;
        require(!perspective_matrix(parameters), "Unordered or coincident clip planes were accepted");
        parameters.reverse_z = true;
        require(!perspective_matrix(parameters), "Reverse Z bypassed physical plane ordering");
    }
    parameters = perspective();
    parameters.handedness = static_cast<Handedness>(99);
    require(!perspective_matrix(parameters), "Unknown perspective handedness was accepted");
    parameters = perspective();
    parameters.depth_range = static_cast<DepthRange>(99);
    require(!perspective_matrix(parameters), "Unknown depth range was accepted");
    parameters = perspective();
    parameters.vertical_fov_radians = std::numeric_limits<double>::denorm_min();
    require(!perspective_matrix(parameters), "Finite FOV reciprocal overflow was accepted");
    parameters = perspective();
    parameters.aspect = std::numeric_limits<double>::denorm_min();
    require(!perspective_matrix(parameters), "Finite aspect reciprocal overflow was accepted");
    parameters = perspective();
    parameters.near_distance = 1e308;
    parameters.far_distance = 1.7e308;
    require(!perspective_matrix(parameters), "Finite depth coefficient overflow was accepted");
    parameters = perspective();
    parameters.near_distance = 1e200;
    parameters.far_distance = 2e200;
    const auto large_planes = perspective_matrix(parameters);
    require(large_planes.has_value(), "Representable projection overflowed a near-far product");
    near((*large_planes)[10], 2, "Large-plane projection slope mismatch");
    near((*large_planes)[14] / 1e200, -2, "Large-plane projection offset mismatch");

    auto target = viewport();
    target.width = 0;
    require(!screen_point({0, 0, 0.5, 1}, perspective(), target), "Zero viewport width was accepted");
    target = viewport();
    target.height = 0;
    require(!valid(target), "Zero viewport height was accepted");
    target = viewport();
    target.origin = static_cast<ScreenOrigin>(99);
    require(!valid(target), "Unknown screen origin was accepted");
    target = viewport();
    target.x = std::numeric_limits<std::int32_t>::max() - 799;
    require(!valid(target), "Viewport right edge overflow was accepted");
    target = viewport();
    target.y = std::numeric_limits<std::int32_t>::max() - 399;
    require(!valid(target), "Viewport lower edge overflow was accepted");
    for (std::size_t axis = 0; axis < 4; ++axis) {
        for (const double value : {nan, infinity}) {
            Vector4 clip{0, 0, 0.5, 1};
            clip[axis] = value;
            require(!screen_point(clip, perspective(), viewport()), "Non-finite clip was accepted");
        }
    }
    require(!screen_point({0, 0, 0, -1}, perspective(), viewport()), "Negative w was accepted");
    require(!screen_point({0, 0, 0, -0.0}, perspective(), viewport()), "Negative zero w was accepted");
    require(!screen_point({1, 0, 0, std::numeric_limits<double>::denorm_min()},
                         perspective(), viewport()), "NDC division overflow was accepted");
    require(!screen_point({std::numeric_limits<double>::max(), 0, 0, 1},
                         perspective(), viewport()), "Finite pixel conversion overflow was accepted");
}

static void calibration_oracles() {
    auto value = calibration();
    require(valid(value), "Identity calibration was rejected");
    require(map_point(value, {2, -3, 4}) == Vector3{2, -3, 4}, "Identity mapping changed coordinates");
    value.host_from_guest[12] = 10;
    value.host_from_guest[13] = -20;
    value.host_from_guest[14] = 30;
    require(map_point(value, {-2, 3, 0.5}) == Vector3{8, -17, 30.5}, "Rigid translation oracle mismatch");
    require(unmap_point(value, {8, -17, 30.5}) == Vector3{-2, 3, 0.5}, "Rigid inverse oracle mismatch");
    value.host_from_guest[0] = 2;
    value.host_from_guest[5] = 2;
    value.host_from_guest[10] = 2;
    value.units_per_block = 2;
    require(map_point(value, {-2, 3, 0.5}) == Vector3{6, -14, 31}, "Uniform scale oracle mismatch");
    require(unmap_point(value, {6, -14, 31}) == Vector3{-2, 3, 0.5}, "Uniform inverse oracle mismatch");
    const auto inverse = inverse_matrix(value);
    require(inverse.has_value(), "Uniform inverse was rejected");
    near(*inverse, Matrix4{0.5, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0.5, 0, -5, 10, -15, 1},
         "Uniform inverse matrix lost scale or translation");

    value.host_from_guest[0] = -2;
    value.reverses_orientation = true;
    require(map_point(value, {2, 3, 4}) == Vector3{6, -14, 38}, "Reflected scale oracle mismatch");
    require(unmap_point(value, {6, -14, 38}) == Vector3{2, 3, 4}, "Reflected inverse oracle mismatch");
    value.reverses_orientation = false;
    require(!valid(value), "Reflection was accepted without orientation declaration");

    geometry::CoordinateMap axes{"minecraft:overworld", {2, 0, 1}, {2, -2, 2}, {10, -20, 30}};
    const auto from_axes = calibration_from_axes(axes, 42);
    require(from_axes && from_axes->revision == 42 && from_axes->units_per_block == 2
            && from_axes->reverses_orientation, "Axis calibration lost revision, scale or parity");
    require(map_point(*from_axes, {2, 3, 4}) == Vector3{18, -24, 36}, "Signed axis oracle mismatch");
    const auto recovered = axis_map(*from_axes);
    require(recovered && recovered->dimension == axes.dimension
            && recovered->source_axis == axes.source_axis && recovered->scale == axes.scale
            && recovered->origin == axes.origin, "Signed axis mapping did not preserve collision contract");
    const auto box = geometry::map_box(*recovered, {"minecraft:overworld", 1, 2, 3},
                                      {0.25, 0.5, 0.125, 0.75, 1, 0.875});
    require(box && box->minimum == Vector3{16.25, -23.5, 35}
            && box->maximum == Vector3{17.75, -22.5, 36}, "Collision bounds diverged from calibrated axes");

    value = calibration();
    value.host_from_guest = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
    require(map_point(value, {2, 3, 4}) == Vector3{7, 22, 34}, "Quarter-turn oracle mismatch");
    require(unmap_point(value, {7, 22, 34}) == Vector3{2, 3, 4}, "Quarter-turn inverse mismatch");
    const auto quarter = axis_map(value);
    require(quarter && quarter->source_axis == std::array<std::uint8_t, 3>{1, 0, 2}
            && quarter->scale == Vector3{-1, 1, 1}, "Exact quarter turn lost AABB representation");

    value = calibration();
    value.host_from_guest = {0.6, 0.8, 0, 0, -0.8, 0.6, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    require(valid(value), "General rigid rotation was rejected");
    const auto rotated = map_point(value, {5, 0, 2});
    const auto restored = unmap_point(value, {3, 4, 2});
    require(rotated && restored, "General rotation mapping was rejected");
    near(*rotated, Vector3{3, 4, 2}, "General rotation oracle mismatch");
    near(*restored, Vector3{5, 0, 2}, "General rotation inverse oracle mismatch");
    require(!axis_map(value), "General rotation was misrepresented as an exact collision AABB");
    value.host_from_guest = {1, 1e-12, 0, 0, -1e-12, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    require(valid(value) && !axis_map(value), "Nearly axial rotation was silently snapped for collision");
}

static void invalid_calibrations() {
    std::vector<Calibration> invalid;
    auto value = calibration();
    value.version = 0;
    invalid.push_back(value);
    value = calibration();
    value.revision = 0;
    invalid.push_back(value);
    value = calibration();
    value.host_from_guest[4] = 0.25;
    invalid.push_back(value);
    value = calibration();
    value.host_from_guest[5] = 2;
    invalid.push_back(value);
    value = calibration();
    value.host_from_guest[10] = 0;
    invalid.push_back(value);
    value = calibration();
    value.host_from_guest[3] = 0.01;
    invalid.push_back(value);
    value = calibration();
    value.host_from_guest[15] = 0;
    invalid.push_back(value);
    value = calibration();
    value.reverses_orientation = true;
    invalid.push_back(value);
    value = calibration();
    value.units_per_block = 2;
    invalid.push_back(value);
    for (const auto& dimension : {std::string{}, std::string("bad dimension"), std::string(65, 'x')}) {
        value = calibration();
        value.dimension = dimension;
        invalid.push_back(value);
    }
    for (const double number : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
        value = calibration();
        value.units_per_block = number;
        invalid.push_back(value);
        value = calibration();
        value.max_roundtrip_error = number;
        invalid.push_back(value);
    }
    for (std::size_t index = 0; index < 16; ++index) {
        for (const double number : {std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity()}) {
            value = calibration();
            value.host_from_guest[index] = number;
            invalid.push_back(value);
        }
    }
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        const auto& rejected = invalid[index];
        require(!valid(rejected), "Invalid calibration passed validation at case " + std::to_string(index));
        require(!inverse_matrix(rejected) && !map_point(rejected, {1, 2, 3})
                && !unmap_point(rejected, {1, 2, 3}) && !axis_map(rejected),
                "Invalid calibration was usable through another entry point");
    }
    geometry::CoordinateMap axes{"minecraft:overworld", {0, 1, 2}, {1, 1, 1}, {0, 0, 0}};
    require(!calibration_from_axes(axes, 0), "Zero mapping revision was accepted");
    axes.scale[1] = 2;
    require(geometry::valid_map(axes) && !calibration_from_axes(axes, 1),
            "Nonuniform collision scale was silently made a similarity calibration");
    axes.scale = {1, 1, 1};
    axes.source_axis = {0, 0, 2};
    require(!calibration_from_axes(axes, 1), "Repeated source axis was accepted");
}

static void roundtrip_and_correspondence() {
    auto value = calibration();
    // At 2^54 a one-unit positive increment is not representable.
    value.host_from_guest[12] = 18014398509481984.0;
    require(valid(value), "Finite large origin was rejected before measuring point error");
    require(!map_point(value, {1, 0, 0}), "One-block loss at a large origin passed absolute error gate");
    require(!map_point(value, {1125899906842625.0, 0, 0}),
            "World-coordinate-relative tolerance hid a one-block round-trip loss");
    require(map_point(value, {4, 0, 0}) == Vector3{18014398509481988.0, 0, 0},
            "Representable point at a large origin was rejected");
    require(!unmap_point(value, {3, 0, 0}), "Host-to-guest round-trip error was not checked");
    require(unmap_point(value, {4, 0, 0}) == Vector3{-18014398509481980.0, 0, 0},
            "Representable inverse point at a large origin was rejected");
    value.units_per_block = 2;
    value.host_from_guest[0] = value.host_from_guest[5] = value.host_from_guest[10] = 2;
    value.max_roundtrip_error = 0.25;
    require(!unmap_point(value, {3, 0, 0}), "Host error was compared in the wrong units");
    value.max_roundtrip_error = 0.5;
    require(unmap_point(value, {3, 0, 0}) == Vector3{-9007199254740990.0, 0, 0},
            "Absolute guest-unit error boundary was not inclusive");

    value = calibration();
    value.units_per_block = 2;
    value.host_from_guest[0] = value.host_from_guest[5] = value.host_from_guest[10] = 2;
    require(!map_point(value, {std::numeric_limits<double>::max(), 0, 0}),
            "Finite forward coordinate overflow was accepted");
    value.units_per_block = std::numeric_limits<double>::denorm_min();
    value.host_from_guest[0] = value.host_from_guest[5] = value.host_from_guest[10]
        = value.units_per_block;
    require(valid(value) && !inverse_matrix(value) && !map_point(value, {0, 0, 0}),
            "Finite inverse coefficient overflow was accepted");
    value = calibration();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const double number : {std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity()}) {
            Vector3 point{1, 2, 3};
            point[axis] = number;
            require(!map_point(value, point) && !unmap_point(value, point),
                    "Non-finite world point was accepted");
        }
    }
    value.units_per_block = 2;
    value.host_from_guest = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 10, -20, 30, 1};
    std::array<Correspondence, 2> points{{{{0, 0, 0}, {10, -20, 30}}, {{1, 2, 3}, {12, -16, 36}}}};
    const auto exact = correspondence_error(value, points);
    require(exact.has_value(), "Exact correspondences were rejected");
    near(*exact, 0, "Exact correspondence acquired error");
    points[1].host = {15, -12, 36};
    const auto displaced = correspondence_error(value, points);
    require(displaced.has_value(), "Displaced correspondence was rejected");
    near(*displaced, 5, "Correspondence residual was not Euclidean host distance");
    std::vector<Correspondence> bounded(64, points[0]);
    bounded.back() = points[1];
    const auto maximum = correspondence_error(value, bounded);
    require(maximum.has_value(), "Correspondence capacity boundary was rejected");
    near(*maximum, 5, "Maximum correspondence error was not retained");
    bounded.push_back(points[0]);
    require(!correspondence_error(value, bounded), "Excess correspondence count was accepted");
    require(!correspondence_error(value, {}), "Empty correspondence set was accepted");
    points[1].host[0] = std::numeric_limits<double>::quiet_NaN();
    require(!correspondence_error(value, points), "Non-finite measured host point was accepted");
    value = calibration();
    std::array<Correspondence, 1> large{{{{0, 0, 0}, {1e308, 1e308, 0}}}};
    const auto norm = correspondence_error(value, large);
    require(norm.has_value(), "Representable Euclidean norm overflowed intermediate squares");
    near(*norm / 1e308, std::numbers::sqrt2, "Large Euclidean residual mismatch");
    large[0] = {{std::numeric_limits<double>::max(), 0, 0},
                {-std::numeric_limits<double>::max(), 0, 0}};
    require(!correspondence_error(value, large), "Finite residual subtraction overflow was accepted");
}

static void view_and_camera_contracts() {
    for (const auto handedness : {Handedness::left, Handedness::right}) {
        const double sign = handedness == Handedness::left ? 1 : -1;
        const auto matrix = view_matrix({2, 3, 4}, {1, 0, 0}, {0, 1, 0}, {0, 0, sign}, handedness);
        require(matrix.has_value(), "Canonical camera basis was rejected");
        near(*matrix, Matrix4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, -3, -4, 1},
             "View matrix translation oracle mismatch");
        near(host::transform(*matrix, {2, 3, 4 + sign * 3, 1}), Vector4{0, 0, sign * 3, 1},
             "Camera forward direction was inverted");
        const auto turned = view_matrix({2, 3, 4}, {0, 0, -sign}, {0, 1, 0}, {1, 0, 0}, handedness);
        require(turned.has_value(), "Turned camera basis was rejected");
        near(*turned, Matrix4{0, 0, sign, 0, 0, 1, 0, 0, -sign, 0, 0, 0,
                             sign * 4, -3, -sign * 2, 1}, "Turned view matrix oracle mismatch");
        near(host::transform(*turned, {5, 3, 4, 1}), Vector4{0, 0, sign * 3, 1},
             "Turned camera did not map its look direction");
    }
    require(!view_matrix({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 1}, Handedness::left),
            "Nonorthogonal view basis was accepted");
    require(!view_matrix({0, 0, 0}, {2, 0, 0}, {0, 1, 0}, {0, 0, 1}, Handedness::left),
            "Scaled view basis was accepted");
    require(!view_matrix({0, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, Handedness::left),
            "Reflected view basis was accepted as a proper camera rotation");
    require(!view_matrix({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 0}, Handedness::left),
            "Singular view basis was accepted");
    require(!view_matrix({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                         static_cast<Handedness>(99)), "Unknown view handedness was accepted");
    require(!view_matrix({std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), 0},
                         {0.6, 0.8, 0}, {-0.8, 0.6, 0}, {0, 0, 1}, Handedness::left),
            "Finite view translation overflow was accepted");

    auto value = camera();
    require(valid(value), "Valid camera contract was rejected");
    constexpr std::array<std::uint64_t CaptureKey::*, 5> identity_fields{
        &CaptureKey::epoch, &CaptureKey::world, &CaptureKey::camera, &CaptureKey::frame, &CaptureKey::capture
    };
    for (const auto member : identity_fields) {
        value = camera();
        value.key.*member = 0;
        require(!valid(value), "Incomplete camera identity was accepted");
    }
    value = camera();
    value.configuration_revision = 0;
    require(!valid(value), "Zero camera configuration revision was accepted");
    value = camera();
    value.viewport.width = 1600;
    require(!valid(value), "Camera aspect disagreed with viewport extent");
    value.viewport.height = 800;
    require(valid(value), "Same-aspect camera resize was rejected");
    value = camera();
    value.position = {5, 0, 0};
    require(!valid(value), "Camera position disagreed with view translation");
    value.view[12] = -5;
    require(valid(value), "Matching camera position and view were rejected");
    value.position[0] = 5.001;
    require(!valid(value), "Camera position error budget was ignored");
    for (const double number : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::infinity()}) {
        value = camera();
        value.max_position_error = number;
        require(!valid(value), "Invalid camera position error budget was accepted");
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        value = camera();
        value.position[axis] = std::numeric_limits<double>::quiet_NaN();
        require(!valid(value), "Non-finite camera position was accepted");
    }
    for (const std::size_t index : {std::size_t{0}, std::size_t{3}, std::size_t{12}}) {
        value = camera();
        value.view[index] = std::numeric_limits<double>::infinity();
        require(!valid(value), "Non-finite camera matrix was accepted");
    }
    value = camera();
    value.view[4] = 0.25;
    require(!valid(value), "Camera shear was accepted");
    value = camera();
    value.view[0] = 2;
    require(!valid(value), "Camera scale was accepted");
    value = camera();
    value.view[10] = 0;
    require(!valid(value), "Singular camera was accepted");
}

static void paired_camera_oracles() {
    auto mapping = calibration();
    mapping.units_per_block = 2;
    mapping.host_from_guest = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 10, 20, 30, 1};
    const auto guest = camera();
    auto host_camera = camera();
    host_camera.position = {10, 20, 30};
    host_camera.view[12] = -10;
    host_camera.view[13] = -20;
    host_camera.view[14] = -30;
    host_camera.perspective.near_distance = 2;
    host_camera.perspective.far_distance = 18;
    const auto scaled = project_pair(mapping, guest, host_camera, {3, 1.5, 3});
    require(scaled && scaled->guest_screen && scaled->host_screen,
            "Calibrated camera pair was rejected");
    near(scaled->guest_world, Vector3{3, 1.5, 3}, "Pair changed guest world point");
    near(scaled->host_world, Vector3{16, 23, 36}, "Pair host world point oracle mismatch");
    near(scaled->guest_clip, Vector4{1.5, 1.5, 2.25, 3}, "Guest clip oracle mismatch");
    near(scaled->host_clip, Vector4{3, 3, 4.5, 6}, "Scaled host clip oracle mismatch");
    near(scaled->guest_screen->pixels, std::array<double, 2>{610, 120}, "Guest pair pixels changed");
    near(scaled->host_screen->pixels, std::array<double, 2>{610, 120}, "Host pair pixels changed");
    near(scaled->guest_screen->depth, 0.75, "Guest pair depth changed");
    near(scaled->host_screen->depth, 0.75, "Consistently scaled near/far changed depth");

    host_camera.perspective.near_distance = 1;
    host_camera.perspective.far_distance = 9;
    const auto unscaled_planes = project_pair(mapping, guest, host_camera, {3, 1.5, 3});
    require(unscaled_planes && unscaled_planes->guest_screen && unscaled_planes->host_screen,
            "Explicit pair with different depth calibration was rejected");
    near(unscaled_planes->host_screen->pixels, std::array<double, 2>{610, 120},
         "Changing clip distances changed XY projection");
    near(unscaled_planes->guest_screen->depth, 0.75, "Guest depth inherited host plane settings");
    near(unscaled_planes->host_screen->depth, 0.9375, "Unscaled host near/far was mistaken for matched depth");

    const auto behind = project_pair(calibration(), guest, camera(), {0, 0, -3});
    require(behind && !behind->guest_screen && !behind->host_screen,
            "Behind-camera pair fabricated screen coordinates");
    const auto eye = project_pair(calibration(), guest, camera(), {0, 0, 0});
    require(eye && !eye->guest_screen && !eye->host_screen, "Eye-plane pair divided zero w");
    host_camera = camera();
    host_camera.view[0] = -1;
    host_camera.view[10] = -1;
    const auto opposite = project_pair(calibration(), guest, host_camera, {0, 0, 3});
    require(opposite && opposite->guest_screen && opposite->guest_screen->inside
            && !opposite->host_screen, "Opposite camera visibility was copied from the guest");
    auto invalid = camera();
    invalid.position[0] = 1;
    require(!project_pair(calibration(), invalid, guest, {0, 0, 3})
            && !project_pair(calibration(), guest, invalid, {0, 0, 3}), "Invalid camera pair was projected");
    mapping.revision = 0;
    require(!project_pair(mapping, guest, guest, {0, 0, 3}), "Invalid calibration pair was projected");
    require(!project_pair(calibration(), guest, guest,
                          {std::numeric_limits<double>::quiet_NaN(), 0, 3}), "Non-finite paired point was accepted");
    require(!project_pair(calibration(), guest, guest,
                          {1, 0, std::numeric_limits<double>::denorm_min()}),
            "Positive-w NDC overflow was misclassified as an invisible camera pair");
    require(!project_pair(calibration(), guest, guest,
                          {std::numeric_limits<double>::max(), 0, 1}),
            "Positive-w pixel overflow was misclassified as an invisible camera pair");
    auto distant = camera();
    distant.position[0] = std::numeric_limits<double>::max();
    distant.view[12] = -std::numeric_limits<double>::max();
    require(valid(distant), "Finite distant camera was rejected before point projection");
    require(!project_pair(calibration(), distant, guest,
                          {-std::numeric_limits<double>::max(), 0, 3}), "Intermediate view overflow was accepted");
}

int main() {
    try {
        projection_oracles();
        viewport_and_clipping();
        invalid_projection_inputs();
        calibration_oracles();
        invalid_calibrations();
        roundtrip_and_correspondence();
        view_and_camera_contracts();
        paired_camera_oracles();
        std::cout << "camera-projection: passed " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "camera-projection: " << error.what() << '\n';
        return 1;
    }
}
