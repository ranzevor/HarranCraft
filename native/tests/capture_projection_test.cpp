#include <harrancraft/capture_projection.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace harrancraft;
using namespace std::chrono_literals;
using projection::CaptureProjector;
using projection::ProjectionKey;

static int assertions = 0;
static constexpr auto start = CaptureProjector::TimePoint{};
static constexpr auto dimension = "minecraft:overworld";
static constexpr auto atlas = "minecraft:textures/atlas/blocks.png";

static void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}

static void close_to(double actual, double expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= 1e-9, message);
}

static host::ImageIdentity image() {
    host::ImageIdentity result;
    result.sha256[0] = 11;
    result.file_bytes = 4096;
    result.machine = 0x8664;
    return result;
}

static host::Evidence evidence() {
    return {host::Mode::single_player, host::Lifecycle::playing,
            host::ReadPhase::verified_engine_read, 71};
}

static geometry::CoordinateMap mapping() {
    geometry::CoordinateMap result;
    result.dimension = dimension;
    return result;
}

static projection::Calibration calibration() {
    projection::Calibration result;
    result.revision = 1;
    result.dimension = dimension;
    result.host_from_guest = projection::identity_matrix();
    return result;
}

static projection::CameraContract camera(host::CaptureKey key) {
    projection::CameraContract result;
    result.key = key;
    result.configuration_revision = 1;
    result.view = projection::identity_matrix();
    result.perspective = {projection::contract_version, std::numbers::pi / 2,
        4.0 / 3.0, 1, 9, projection::Handedness::right,
        projection::DepthRange::zero_to_one, false};
    result.viewport = {0, 0, 640, 480, projection::ScreenOrigin::top_left};
    return result;
}

static render::Identity identity(const host::CaptureKey& key) {
    return {key.epoch, key.world, key.camera, key.frame, key.capture};
}

static render::Record model(const projection::CameraContract& guest, std::size_t count) {
    render::Record result;
    result.identity = identity(guest.key);
    result.key = {dimension, 0, 0, -4};
    result.state = "minecraft:stone";
    result.pose = {guest.position[0], guest.position[1], guest.position[2], 0, 0};
    for (std::size_t index = 0; index < count; ++index) {
        render::Quad quad;
        quad.face = 2;
        quad.atlas = atlas;
        quad.sprite = "minecraft:block/sprite" + std::to_string(index % 2);
        const auto z = static_cast<float>(index % 4) * 0.125f;
        quad.vertices = {{{0, 0, z, 0.125f, 0.25f, 0xffabcdefU, 0, 0},
                          {1, 0, z, 0.25f, 0.25f, 0xffabcdefU, 0, 0},
                          {1, 1, z, 0.25f, 0.5f, 0xffabcdefU, 0, 0},
                          {0, 1, z, 0.125f, 0.5f, 0xffabcdefU, 0, 0}}};
        result.quads.push_back(std::move(quad));
    }
    return result;
}

static texture::Record sprite(const render::Record& source, std::size_t index) {
    texture::Record result;
    const auto& key = source.identity;
    result.identity = {key.epoch, key.world, key.camera, key.frame, key.capture};
    result.atlas = atlas;
    result.sprite = "minecraft:block/sprite" + std::to_string(index);
    result.width = result.height = 2;
    result.min_u = 0.125f;
    result.max_u = 0.25f;
    result.min_v = 0.25f;
    result.max_v = 0.5f;
    result.argb = {0xff112233U, 0xff445566U, 0xff778899U, 0xffaabbccU};
    return result;
}

static geometry::Frame shape(const render::Record& source, std::size_t count) {
    geometry::Frame result{geometry::Kind::snapshot, source.identity.epoch, 1,
        {source.key.dimension, source.key.x, source.key.y, source.key.z}, source.state, {}};
    for (std::size_t index = 0; index < count; ++index) {
        const double minimum = static_cast<double>(index) / static_cast<double>(count);
        const double maximum = static_cast<double>(index + 1) / static_cast<double>(count);
        result.boxes.push_back({minimum, 0, 0, maximum, 1, 1});
    }
    return result;
}

struct Fixture {
    host::Observer observer{std::vector<host::ImageIdentity>{image()}};
    material::MaterialCoordinator materials{100ms};
    geometry::GeometryReceiver shapes;
    CaptureProjector projector{observer, materials, shapes};
    host::Evidence context = evidence();
    geometry::CoordinateMap actual_mapping;
    projection::Calibration calibration_value = calibration();
    projection::CameraContract guest = camera({17, 7, 9, 10, 1});
    projection::CameraContract host_camera = camera({1, 71, 91, 100, 200});
    render::Record record;
    geometry::Frame receipt;
    projection::GeometryAssociation association;
    CaptureProjector::TimePoint now = start;

    explicit Fixture(std::size_t quads = 2, std::size_t boxes = 1,
                     geometry::CoordinateMap map = mapping())
        : actual_mapping(std::move(map)), record(model(guest, quads)),
          receipt(shape(record, boxes)) {
        const auto epoch = observer.begin(image(), context);
        require(epoch && *epoch == host_camera.key.epoch, "Host fixture epoch changed");
        materials.begin(guest.key.epoch, guest.key.world, guest.key.camera, dimension, now);
        shapes.begin(guest.key.epoch, actual_mapping);
        associate();
    }

    void associate() {
        association = {guest.key, receipt.epoch, receipt.revision,
            {record.key.dimension, record.key.x, record.key.y, record.key.z}, record.state};
    }

    void sync_model() {
        record.identity = identity(guest.key);
        record.pose = {guest.position[0], guest.position[1], guest.position[2], 0, 0};
        associate();
    }

    void advance_host() {
        host_camera.key.frame += 10;
        ++host_camera.key.capture;
    }

    void advance() {
        guest.key.frame += 10;
        ++guest.key.capture;
        advance_host();
        ++receipt.revision;
        sync_model();
    }

    void restart_guest(std::uint64_t epoch) {
        guest.key.epoch = epoch;
        guest.key.frame = 10;
        guest.key.capture = 1;
        receipt.epoch = epoch;
        receipt.revision = 1;
        sync_model();
        materials.begin(epoch, guest.key.world, guest.key.camera, dimension, now);
        shapes.begin(epoch, actual_mapping);
    }

    host::CameraSample host_sample() const {
        const auto matrix = projection::perspective_matrix(host_camera.perspective);
        require(matrix.has_value(), "Invalid controlled host projection");
        return {host_camera.key, host_camera.view, *matrix, host_camera.position};
    }

    void observe() {
        require(observer.capture(context, host_sample()), "Controlled host capture refused");
    }

    void feed_model() {
        require(materials.feed_models(render::encode(record), now), "Model receiver refused fixture");
    }

    void feed_sprite(std::size_t index) {
        require(materials.feed_textures(texture::encode(sprite(record, index)), now),
                "Texture receiver refused fixture");
    }

    void feed_shape() {
        require(shapes.feed(geometry::encode(receipt)), "Geometry receiver refused fixture");
    }

    void publish() {
        observe();
        feed_shape();
        feed_model();
        for (std::size_t index = 0; index < std::min(std::size_t{2}, record.quads.size()); ++index) {
            feed_sprite(index);
        }
    }

    std::optional<ProjectionKey> capture() {
        return projector.capture(calibration_value, guest, host_camera, association, context, now);
    }

    ProjectionKey captured() {
        const auto key = capture();
        require(key.has_value(), "Complete controlled capture was refused");
        require(projector.current_key(context, now) == key, "Current projection key changed");
        return *key;
    }

    projection::ProjectedCapture snapshot(const ProjectionKey& key) {
        auto result = projector.snapshot(key, context, now);
        require(result.has_value(), "Current projection snapshot was unavailable");
        return std::move(*result);
    }

    void cleared(const ProjectionKey& previous) {
        require(!projector.current_key(context, now), "Revoked projection key remained current");
        require(!projector.snapshot(previous, context, now), "Revoked projection snapshot survived");
        require(projector.retained_points() == 0, "Revoked projection retained owned points");
        require(!capture(), "Old source capture was replayed after revocation");
    }
};

static void complete_and_texture_order() {
    for (const bool texture_first : {false, true}) {
        Fixture f;
        f.observe();
        f.feed_shape();
        if (texture_first) {
            f.feed_sprite(0);
            f.feed_sprite(1);
        } else {
            f.feed_model();
            f.feed_sprite(0);
        }
        require(!f.capture() && f.projector.retained_points() == 0,
                "Incomplete material became a projected capture");
        if (texture_first) f.feed_model();
        else f.feed_sprite(1);
        const auto key = f.captured();
        require(key.guest_capture == f.guest.key && key.host_capture == f.host_camera.key
                    && key.guest_capture != key.host_capture && key.geometry == f.association,
                "Independent runtime capture identities were conflated");
        const auto value = f.snapshot(key);
        require(value.quads.size() == 2 && value.boxes.size() == 1
                    && f.projector.retained_points() == 16,
                "Projection omitted decoded quad vertices or collision corners");
        const auto& point = value.quads[0][0];
        require(point.guest_world == host::Vector3{0, 0, -4}
                    && point.host_world == point.guest_world
                    && point.guest_screen && point.host_screen
                    && point.guest_screen->inside && point.host_screen->inside,
                "Projected model did not preserve the actual block-local vertex");
        close_to(point.host_screen->pixels[0], 320, "Wrong horizontal viewport center");
        close_to(point.host_screen->pixels[1], 240, "Wrong vertical viewport center");
        close_to(point.host_screen->depth, 0.84375, "Unexpected controlled perspective depth");
        require(value.boxes[0][0].guest_world == host::Vector3{0, 0, -4}
                    && value.boxes[0][7].guest_world == host::Vector3{1, 1, -3},
                "Collision corners did not come from the decoded geometry receipt");
        auto copy = value;
        copy.quads.clear();
        require(f.snapshot(key).quads.size() == 2, "A returned copy mutated owned projection data");
    }
}

static void alter(host::CaptureKey& key, int field) {
    switch (field) {
        case 0: ++key.epoch; break;
        case 1: ++key.world; break;
        case 2: ++key.camera; break;
        case 3: ++key.frame; break;
        case 4: ++key.capture; break;
        default: throw std::logic_error("Unknown capture field fixture");
    }
}

static void exact_capture_identities() {
    for (int source = 0; source < 3; ++source) {
        for (int field = 0; field < 5; ++field) {
            Fixture f;
            f.publish();
            auto guest = f.guest;
            auto host_camera = f.host_camera;
            auto association = f.association;
            if (source == 0) {
                alter(guest.key, field);
                association.guest_capture = guest.key;
                association.epoch = guest.key.epoch;
            } else if (source == 1) {
                alter(host_camera.key, field);
            } else {
                alter(association.guest_capture, field);
                association.epoch = association.guest_capture.epoch;
            }
            require(!f.projector.capture(f.calibration_value, guest, host_camera,
                        association, f.context, f.now),
                    "A mismatched camera or geometry association identity was accepted");
            require(f.projector.retained_points() == 0, "Identity refusal retained points");
            const auto key = f.captured();
            for (int side = 0; side < 2; ++side) {
                auto wrong = key;
                alter(side == 0 ? wrong.guest_capture : wrong.host_capture, field);
                require(!f.projector.snapshot(wrong, f.context, f.now),
                        "A snapshot request ignored an identity field");
                require(f.projector.current_key(f.context, f.now) == key,
                        "A wrong snapshot query changed the valid owner capture");
            }
        }
    }
}

static void geometry_associations() {
    for (int fault = 0; fault < 6; ++fault) {
        Fixture f;
        f.publish();
        const auto original = f.association;
        switch (fault) {
            case 0: ++f.association.epoch; break;
            case 1: ++f.association.revision; break;
            case 2: ++f.association.position.x; break;
            case 3: f.association.position.dimension = "minecraft:the_nether"; break;
            case 4: f.association.canonical_state = "minecraft:dirt"; break;
            case 5: f.association.revision = 0; break;
            default: throw std::logic_error("Unknown association fixture");
        }
        require(!f.capture() && f.projector.retained_points() == 0,
                "An incorrect geometry association became visible");
        f.association = original;
        (void)f.captured();
    }
    for (int fault = 0; fault < 5; ++fault) {
        Fixture f;
        f.observe();
        f.feed_model();
        f.feed_sprite(0);
        f.feed_sprite(1);
        switch (fault) {
            case 0: f.receipt.state = "minecraft:dirt"; break;
            case 1: ++f.receipt.key.z; break;
            case 2:
                f.receipt.epoch = 18;
                f.shapes.begin(18, mapping());
                break;
            case 3:
                f.receipt.kind = geometry::Kind::retire;
                f.receipt.state.clear();
                f.receipt.boxes.clear();
                break;
            case 4: break;
            default: throw std::logic_error("Unknown receipt fixture");
        }
        if (fault != 4) f.feed_shape();
        require(!f.capture() && f.projector.retained_points() == 0,
                "A foreign, retired or missing decoded geometry receipt was accepted");
    }
    auto different = mapping();
    different.origin[0] = 1;
    Fixture mismatch(2, 1, different);
    mismatch.publish();
    require(mismatch.shapes.find({dimension, 0, 0, -4}).has_value(),
            "Mapping mismatch fixture did not stage a real native candidate");
    require(!mismatch.capture() && mismatch.projector.retained_points() == 0,
            "A native candidate under another coordinate mapping was projected");
}

static void geometry_receipt_freshness() {
    Fixture f;
    f.publish();
    const auto old = f.captured();
    f.advance();
    f.observe();
    f.feed_model();
    f.feed_sprite(0);
    f.feed_sprite(1);
    --f.association.revision;
    require(!f.capture() && f.projector.retained_points() == 0,
            "Fresh camera frames relabeled an old geometry receipt");
    require(!f.projector.snapshot(old, f.context, f.now),
            "Failed receipt replacement retained the old projection");
    ++f.association.revision;
    f.feed_shape();
    const auto replacement = f.captured();
    require(replacement.serial == old.serial + 1 && replacement.geometry.revision == 2,
            "A failed association consumed serial history or blocked the fresh receipt");
}

static void empty_shapes_and_capacity() {
    for (const bool maximum : {false, true}) {
        Fixture f(maximum ? render::max_quads : 2, maximum ? geometry::max_boxes : 0);
        f.publish();
        const auto receipt = f.shapes.last_record();
        require(receipt && receipt->kind == geometry::Kind::snapshot,
                "The decoded geometry snapshot was not retained");
        const auto key = f.captured();
        const auto value = f.snapshot(key);
        if (maximum) {
            require(value.quads.size() == 512 && value.boxes.size() == 128
                        && f.projector.retained_points() == CaptureProjector::max_points
                        && CaptureProjector::max_points == 3072,
                    "Maximum decoded capture exceeded or failed its exact point capacity");
            require(value.quads.back()[3].guest_world == host::Vector3{0, 1, -3.625}
                        && value.boxes.back()[7].guest_world == host::Vector3{1, 1, -3},
                    "The final bounded quad or collision box was omitted");
            for (const auto& quad : value.quads) {
                for (const auto& point : quad) {
                    require(point.guest_screen && point.host_screen,
                            "A maximum-size visible vertex lost its screen projection");
                }
            }
        } else {
            require(receipt->boxes.empty() && !f.shapes.find({dimension, 0, 0, -4})
                        && value.boxes.empty() && value.quads.size() == 2
                        && f.projector.retained_points() == 8,
                    "A legal empty collision snapshot was confused with missing geometry");
        }
        f.shapes.end_of_stream();
        require(!f.shapes.last_record(), "Geometry EOF retained its decoded receipt");
        f.cleared(key);
    }
}

static void material_lifetime() {
    for (int event = 0; event < 12; ++event) {
        Fixture f;
        f.publish();
        const auto key = f.captured();
        const auto copy = f.snapshot(key);
        if (event <= 2) {
            f.advance();
            if (event != 0) {
                f.record.quads.clear();
                f.record.state = event == 1 ? "" : "minecraft:air";
                if (event == 1) {
                    f.record.kind = render::Kind::retire;
                    f.record.pose = {};
                }
            }
            f.feed_model();
        } else if (event == 3 || event == 4) {
            f.materials.end_of_stream(event == 3 ? material::Channel::models
                                                : material::Channel::textures);
        } else if (event == 5 || event == 6) {
            f.materials.fail(event == 5 ? material::Channel::models
                                      : material::Channel::textures);
        } else if (event == 7) {
            auto pending = f.record;
            ++pending.identity.capture;
            pending.identity.frame += 10;
            ++pending.key.x;
            require(f.materials.feed_models(render::encode(pending), start),
                    "An independent pending material fixture was refused");
            require(f.projector.poll(f.context, start + 99ms),
                    "A valid capture was retired before the unrelated assembly deadline");
            f.now = start + 100ms;
        } else if (event == 8) {
            f.materials.stop();
        } else if (event == 11) {
            auto withdrawn = sprite(f.record, 0);
            withdrawn.kind = texture::Kind::retire;
            withdrawn.width = withdrawn.height = 0;
            withdrawn.min_u = withdrawn.max_u = withdrawn.min_v = withdrawn.max_v = 0;
            withdrawn.argb.clear();
            require(f.materials.feed_textures(texture::encode(withdrawn), f.now)
                        && f.materials.state() == material::CoordinationState::revoked,
                    "A matched source-pixel withdrawal did not revoke the material session");
        } else {
            const auto bytes = event == 9 ? render::encode(f.record)
                                         : texture::encode(sprite(f.record, 0));
            const bool accepted = event == 9
                ? f.materials.feed_models(std::span(bytes).first(5), f.now)
                : f.materials.feed_textures(std::span(bytes).first(5), f.now);
            require(accepted, "The controlled partial material record was refused");
            f.materials.end_of_stream(event == 9 ? material::Channel::models
                                                : material::Channel::textures);
        }
        require(!f.projector.poll(f.context, f.now), "Material revocation retained a live projection");
        require(copy.key == key && !copy.quads.empty(), "The caller's immutable-copy fixture changed");
        require(!f.projector.snapshot(copy.key, f.context, f.now)
                    && f.projector.retained_points() == 0,
                "A retained caller copy bypassed current-source validation");
        if (event == 7) {
            require(f.materials.state() == material::CoordinationState::timed_out
                        && f.materials.retained_bytes() == 0,
                    "Idle projection polling did not enforce the material assembly deadline");
        }
        if (event == 9 || event == 10) {
            require(f.materials.state() == material::CoordinationState::truncated,
                    "Partial material EOF lost its terminal status");
        }
        f.projector.stop();
        f.projector.stop();
        require(f.projector.retained_points() == 0, "Repeated projection stop retained data");
    }
}

static void pending_replacement_retry() {
    Fixture f;
    f.publish();
    const auto previous = f.captured();
    f.advance();
    f.observe();
    f.feed_shape();
    f.feed_model();
    f.feed_sprite(0);
    require(!f.capture() && f.projector.retained_points() == 0
                && !f.projector.snapshot(previous, f.context, f.now),
            "A new capture with a missing sprite retained the previous projected material");
    f.feed_sprite(1);
    const auto completed = f.captured();
    require(completed.serial == previous.serial + 1
                && completed.guest_capture == f.guest.key
                && completed.host_capture == f.host_camera.key
                && completed.geometry.revision == f.receipt.revision,
            "Completing the pending sprite could not retry the uncommitted capture");
}

static void geometry_lifetime() {
    for (int event = 0; event < 7; ++event) {
        Fixture f;
        f.publish();
        const auto key = f.captured();
        if (event <= 3) {
            ++f.receipt.revision;
            if (event == 1) {
                f.receipt.kind = geometry::Kind::retire;
                f.receipt.state.clear();
                f.receipt.boxes.clear();
            } else if (event == 2) {
                f.receipt.boxes.clear();
            } else if (event == 3) {
                ++f.receipt.key.x;
            }
            f.feed_shape();
        } else if (event == 4) {
            f.shapes.end_of_stream();
        } else if (event == 5) {
            const auto bytes = geometry::encode(f.receipt);
            require(f.shapes.feed(std::span(bytes).first(7)), "Partial geometry fixture was refused");
            f.shapes.end_of_stream();
            require(f.shapes.state() == geometry::ReceiverState::truncated,
                    "Partial geometry EOF lost its terminal status");
        } else {
            f.shapes.stop();
        }
        require(!f.projector.poll(f.context, f.now), "Geometry replacement retained an old projection");
        f.cleared(key);
        if (event >= 4) require(!f.shapes.last_record(), "Ended geometry retained a receipt");
    }
}

static void host_lifetime_and_samples() {
    for (int event = 0; event < 8; ++event) {
        Fixture f;
        f.publish();
        const auto key = f.captured();
        auto changed = f.context;
        switch (event) {
            case 0: changed.mode = host::Mode::cooperative; break;
            case 1: changed.mode = host::Mode::invasion; break;
            case 2: changed.lifecycle = host::Lifecycle::loading; break;
            case 3: changed.phase = host::ReadPhase::window_or_present; break;
            case 4: ++changed.world; break;
            case 5:
            case 6:
                f.advance_host();
                if (event == 6) ++f.host_camera.key.camera;
                f.observe();
                break;
            case 7: f.observer.stop(); break;
            default: throw std::logic_error("Unknown host lifecycle fixture");
        }
        require(!f.projector.poll(changed, f.now), "Host context revocation retained a projection");
        require(f.projector.retained_points() == 0
                    && !f.projector.snapshot(key, f.context, f.now),
                "A host-context failure left a usable old key");
    }
    for (int fault = 0; fault < 4; ++fault) {
        Fixture f;
        auto observed = f.host_sample();
        switch (fault) {
            case 0: observed.view[12] = 1; break;
            case 1: observed.position[0] = 1; break;
            case 2: observed.projection[0] *= 1.1; break;
            case 3: observed.projection = {}; break;
            default: throw std::logic_error("Unknown camera sample fixture");
        }
        require(f.observer.capture(f.context, observed),
                "The finite-only observer unexpectedly rejected the controlled mismatch");
        f.feed_shape();
        f.feed_model();
        f.feed_sprite(0);
        f.feed_sprite(1);
        require(!f.capture() && f.projector.retained_points() == 0,
                "Projection accepted an observed camera inconsistent with its contract");
    }
    Fixture guest_pose;
    guest_pose.record.pose.x = 1;
    guest_pose.publish();
    require(!guest_pose.capture(), "Model source camera position disagreed with its guest contract");
}

static void failed_replacement_and_replay() {
    Fixture f;
    f.publish();
    const auto first = f.captured();
    f.advance();
    f.publish();
    f.calibration_value.host_from_guest[3] = 1;
    require(!f.capture() && f.projector.retained_points() == 0
                && !f.projector.snapshot(first, f.context, f.now),
            "Failed capture replacement retained the previous valid projection");
    f.calibration_value.host_from_guest[3] = 0;
    const auto second = f.captured();
    require(second.serial == first.serial + 1, "Failed replacement advanced committed serial history");
    require(!f.capture() && f.projector.retained_points() == 0,
            "The exact successful source capture was replayed");
    require(!f.projector.snapshot(second, f.context, f.now), "Replay failure retained the old key");
    f.advance();
    f.publish();
    const auto third = f.captured();
    require(third.serial == second.serial + 1, "Fresh source recovery lost serial ordering");
    f.projector.stop();
    require(!f.capture(), "Explicit stop allowed the committed source capture to replay");
    f.advance();
    f.publish();
    require(f.captured().serial == third.serial + 1, "Fresh capture could not follow explicit stop");
}

static void configuration_changes() {
    Fixture f;
    f.publish();
    auto previous = f.captured();
    for (int side = 0; side < 2; ++side) {
        f.advance();
        auto& resized = side == 0 ? f.host_camera : f.guest;
        resized.viewport.width = 1280;
        resized.viewport.height = 960;
        f.publish();
        require(!f.capture() && f.projector.retained_points() == 0,
                "Same-aspect resize reused a camera configuration revision");
        ++resized.configuration_revision;
        const auto next = f.captured();
        require(!f.projector.snapshot(previous, f.context, f.now),
                "A resize left the old projection key valid");
        const auto result = f.snapshot(next);
        const auto& screen = side == 0 ? result.quads[0][0].host_screen
                                       : result.quads[0][0].guest_screen;
        require(screen.has_value(), "Resized camera lost its controlled visible point");
        close_to(screen->pixels[0], 640, "Resized horizontal screen coordinate stayed stale");
        close_to(screen->pixels[1], 480, "Resized vertical screen coordinate stayed stale");
        previous = next;
    }
    f.advance();
    f.host_camera.perspective.vertical_fov_radians = std::numbers::pi / 3;
    f.publish();
    require(!f.capture(), "FOV change reused a camera configuration revision");
    ++f.host_camera.configuration_revision;
    (void)f.captured();
    f.advance();
    f.publish();
    --f.host_camera.configuration_revision;
    require(!f.capture(), "Camera configuration revision moved backwards");
    f.host_camera.configuration_revision += 2;
    (void)f.captured();
}

static void camera_epoch_and_calibration_changes() {
    Fixture f;
    f.publish();
    auto previous = f.captured();
    f.advance();
    ++f.host_camera.key.camera;
    f.publish();
    auto next = f.captured();
    require(next.host_capture.camera != previous.host_capture.camera
                && !f.projector.snapshot(previous, f.context, f.now),
            "Host camera replacement did not revoke the old key");
    previous = next;

    ++f.guest.key.camera;
    f.restart_guest(18);
    f.advance_host();
    f.publish();
    next = f.captured();
    require(next.guest_capture.epoch == 18 && next.host_capture.epoch == 1
                && next.guest_capture.camera != previous.guest_capture.camera
                && !f.projector.snapshot(previous, f.context, f.now),
            "Guest camera recovery conflated independent runtime epochs");
    previous = next;

    f.advance();
    ++f.context.world;
    const auto host_epoch = f.observer.begin(image(), f.context);
    require(host_epoch && *host_epoch == 2, "Host session did not advance its own epoch");
    f.host_camera.key = {*host_epoch, f.context.world, 92, 1, 1};
    f.publish();
    next = f.captured();
    require(next.guest_capture.epoch == 18 && next.host_capture.epoch == 2
                && !f.projector.snapshot(previous, f.context, f.now),
            "Independent host reattachment revived a previous projection");
    previous = next;

    f.actual_mapping.origin[0] = 4;
    f.calibration_value.host_from_guest[12] = 4;
    f.host_camera.position[0] = 4;
    f.host_camera.view[12] = -4;
    f.restart_guest(19);
    f.advance_host();
    f.publish();
    require(!f.capture() && f.projector.retained_points() == 0,
            "Changed calibration reused its revision even after reattachment");
    ++f.calibration_value.revision;
    next = f.captured();
    const auto result = f.snapshot(next);
    require(result.quads[0][0].host_world == host::Vector3{4, 0, -4}
                && result.quads[0][0].guest_screen && result.quads[0][0].host_screen,
            "New calibration did not map the actual decoded model");
    close_to(result.quads[0][0].guest_screen->pixels[0],
             result.quads[0][0].host_screen->pixels[0],
             "Corresponding translated cameras disagreed on a controlled point");
    require(!f.projector.snapshot(previous, f.context, f.now),
            "Calibration replacement retained its previous projection key");

    f.advance();
    f.publish();
    --f.calibration_value.revision;
    require(!f.capture(), "Calibration revision moved backwards");
    ++f.calibration_value.revision;
    (void)f.captured();
}

static void end_and_new_epoch_recovery() {
    Fixture f;
    f.publish();
    const auto previous = f.captured();
    f.materials.end_of_stream(material::Channel::textures);
    f.shapes.end_of_stream();
    f.cleared(previous);
    f.restart_guest(18);
    f.advance_host();
    f.publish();
    const auto current = f.captured();
    require(current.serial == previous.serial + 1
                && !f.projector.snapshot(previous, f.context, f.now),
            "New-epoch receiver recovery reused the previous projection");
    auto old_guest = f.guest;
    old_guest.key = previous.guest_capture;
    auto old_host = f.host_camera;
    old_host.key = previous.host_capture;
    require(!f.projector.capture(f.calibration_value, old_guest, old_host,
                previous.geometry, f.context, f.now)
                && f.projector.retained_points() == 0,
            "Retired runtime keys were accepted after receiver recovery");
}

static void association_bounds() {
    Fixture f;
    f.publish();
    require(projection::valid(f.association), "The valid bounded association was refused");
    for (int field = 0; field < 5; ++field) {
        auto bad = f.association;
        auto& key = bad.guest_capture;
        const auto beyond = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
        switch (field) {
            case 0: key.epoch = beyond; bad.epoch = beyond; break;
            case 1: key.world = beyond; break;
            case 2: key.camera = beyond; break;
            case 3: key.frame = beyond; break;
            case 4: key.capture = beyond; break;
            default: throw std::logic_error("Unknown oversized identity fixture");
        }
        require(!projection::valid(bad), "Geometry association accepted an unsigned wire identity");
    }
    for (int fault = 0; fault < 7; ++fault) {
        auto bad = f.association;
        switch (fault) {
            case 0: bad.position.dimension.assign(65, 'a'); break;
            case 1: bad.canonical_state.assign(193, 'a'); break;
            case 2: bad.position.dimension.clear(); break;
            case 3: bad.canonical_state.clear(); break;
            case 4: bad.position.dimension = "minecraft:over world"; break;
            case 5: bad.canonical_state.push_back('\n'); break;
            case 6:
                bad.revision = static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) + 1;
                break;
            default: throw std::logic_error("Unknown bounded metadata fixture");
        }
        require(!projection::valid(bad), "Malformed association metadata passed its early bound");
        require(!f.projector.capture(f.calibration_value, f.guest, f.host_camera,
                    bad, f.context, f.now) && f.projector.retained_points() == 0,
                "Malformed bounded metadata became a projection");
    }
    (void)f.captured();
}

static void owner_thread() {
    Fixture f;
    f.publish();
    const auto key = f.captured();
    const auto point_count = f.projector.retained_points();
    auto foreign = std::async(std::launch::async, [&f, key] {
        std::array<bool, 7> rejected{};
        for (std::size_t operation = 0; operation < rejected.size(); ++operation) {
            try {
                switch (operation) {
                    case 0: (void)f.capture(); break;
                    case 1: (void)f.projector.poll(f.context, f.now); break;
                    case 2: (void)f.projector.current_key(f.context, f.now); break;
                    case 3: (void)f.projector.snapshot(key, f.context, f.now); break;
                    case 4: f.projector.stop(); break;
                    case 5: (void)f.projector.retained_points(); break;
                    case 6: (void)f.shapes.last_record(); break;
                    default: throw std::runtime_error("Unknown owner-thread fixture");
                }
            } catch (const std::logic_error&) {
                rejected[operation] = true;
            }
        }
        return rejected;
    });
    const auto results = foreign.get();
    for (const bool rejected : results) require(rejected, "A foreign projection owner was accepted");
    require(f.projector.current_key(f.context, f.now) == key
                && f.projector.retained_points() == point_count
                && f.materials.models_received() == 1 && f.shapes.records() == 1,
            "Foreign access modified the valid owner state or its sources");
    (void)f.snapshot(key);
}

template<class Test>
static void run(const char* name, Test&& test) {
    try { test(); }
    catch (const std::exception& error) {
        throw std::runtime_error(std::string(name) + ": " + error.what());
    }
}

int main() {
    try {
        run("complete and texture order", complete_and_texture_order);
        run("exact capture identities", exact_capture_identities);
        run("geometry associations", geometry_associations);
        run("geometry receipt freshness", geometry_receipt_freshness);
        run("empty shapes and capacity", empty_shapes_and_capacity);
        run("material lifetime", material_lifetime);
        run("pending replacement retry", pending_replacement_retry);
        run("geometry lifetime", geometry_lifetime);
        run("host lifetime and samples", host_lifetime_and_samples);
        run("failed replacement and replay", failed_replacement_and_replay);
        run("configuration changes", configuration_changes);
        run("camera epoch and calibration changes", camera_epoch_and_calibration_changes);
        run("end and new epoch recovery", end_and_new_epoch_recovery);
        run("association bounds", association_bounds);
        run("owner thread", owner_thread);
        std::cout << "capture-projection: passed " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Capture projection test failed: " << error.what() << '\n';
        return 1;
    }
}
