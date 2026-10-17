#include <harrancraft/capture_projection.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace harrancraft;
using projection::CaptureProjector;
using projection::ProjectionKey;

namespace {

class InjectedAllocationFailure final : public std::bad_alloc {
public:
    const char* what() const noexcept override { return "Controlled allocation failure"; }
};

constexpr auto disabled_failure = std::numeric_limits<std::size_t>::max();
thread_local std::size_t allocations_until_failure = disabled_failure;
thread_local std::size_t injected_failures = 0;
thread_local bool counting_allocations = false;
thread_local std::size_t allocation_count = 0;

void* allocate(std::size_t size) {
    if (counting_allocations) ++allocation_count;
    if (allocations_until_failure != disabled_failure) {
        if (allocations_until_failure == 0) {
            allocations_until_failure = disabled_failure;
            ++injected_failures;
            throw InjectedAllocationFailure{};
        }
        --allocations_until_failure;
    }
    if (void* result = std::malloc(size == 0 ? 1 : size)) return result;
    throw std::bad_alloc{};
}

class FailAllocation {
public:
    explicit FailAllocation(std::size_t preceding = 0) { allocations_until_failure = preceding; }
    ~FailAllocation() { allocations_until_failure = disabled_failure; }
    FailAllocation(const FailAllocation&) = delete;
    FailAllocation& operator=(const FailAllocation&) = delete;
};

class CountAllocations {
public:
    CountAllocations() { allocation_count = 0; counting_allocations = true; }
    ~CountAllocations() { counting_allocations = false; }
    std::size_t count() const { return allocation_count; }
    CountAllocations(const CountAllocations&) = delete;
    CountAllocations& operator=(const CountAllocations&) = delete;
};

} // namespace

// Allocation replacement is confined to this regression executable.
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

static std::size_t assertions = 0;
static constexpr auto dimension = "minecraft:controlled_projection";
static constexpr auto state = "minecraft:oak_slab[type=bottom,waterlogged=false]";
static constexpr auto atlas = "minecraft:textures/atlas/blocks.png";
static constexpr auto sprite_name = "minecraft:block/oak_planks";
static constexpr auto now = CaptureProjector::TimePoint{};

static void require(bool condition, const char* reason) {
    ++assertions;
    if (!condition) throw std::runtime_error(reason);
}

static host::Matrix4 identity_matrix() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

static host::ImageIdentity image() {
    host::ImageIdentity value;
    value.sha256[0] = 71;
    value.file_bytes = 4096;
    value.machine = 0x8664;
    return value;
}

static projection::CameraContract camera(host::CaptureKey key) {
    return {key, 1, identity_matrix(), {0, 0, 0},
        {1, std::numbers::pi / 2, 2, 1, 9, projection::Handedness::left,
            projection::DepthRange::zero_to_one, false},
        {0, 0, 800, 400, projection::ScreenOrigin::top_left}, 1e-6};
}

static render::Record model(const projection::CameraContract& guest) {
    render::Record value;
    const auto& key = guest.key;
    value.identity = {key.epoch, key.world, key.camera, key.frame, key.capture};
    value.key = {dimension, 0, 0, 3};
    value.state = state;
    render::Quad quad;
    quad.face = 2;
    quad.atlas = atlas;
    quad.sprite = sprite_name;
    quad.vertices = {{{0, 0, 0, 0, 0, 0xffffffffU, 0, 0},
                      {1, 0, 0, 1, 0, 0xffffffffU, 0, 0},
                      {1, 1, 0, 1, 1, 0xffffffffU, 0, 0},
                      {0, 1, 0, 0, 1, 0xffffffffU, 0, 0}}};
    value.quads.push_back(std::move(quad));
    return value;
}

static texture::Record sprite(const render::Record& record) {
    texture::Record value;
    const auto& key = record.identity;
    value.identity = {key.epoch, key.world, key.camera, key.frame, key.capture};
    value.atlas = atlas;
    value.sprite = sprite_name;
    value.width = value.height = 1;
    value.max_u = value.max_v = 1;
    value.argb = {0xff123456U};
    return value;
}

struct Fixture {
    host::Observer observer{std::vector<host::ImageIdentity>{image()}};
    material::MaterialCoordinator materials;
    geometry::GeometryReceiver shapes;
    CaptureProjector projector{observer, materials, shapes};
    host::Evidence evidence{host::Mode::single_player, host::Lifecycle::playing,
        host::ReadPhase::verified_engine_read, 31};
    geometry::CoordinateMap mapping{dimension, {0, 1, 2}, {1, 1, 1}, {0, 0, 0}};
    projection::Calibration calibration{1, 1, dimension, identity_matrix(), 1, false, 1e-6};
    projection::CameraContract guest = camera({17, 7, 9, 10, 1});
    projection::CameraContract host_camera = camera({1, 31, 41, 100, 200});
    geometry::Frame receipt{geometry::Kind::snapshot, 17, 1, {dimension, 0, 0, 3},
        state, {{0, 0, 0, 1, 0.5, 1}}};
    projection::GeometryAssociation association;

    Fixture() {
        require(observer.begin(image(), evidence) == 1, "Controlled observer did not begin");
        materials.begin(guest.key.epoch, guest.key.world, guest.key.camera, dimension, now);
        shapes.begin(guest.key.epoch, mapping);
        publish();
    }

    void publish() {
        const auto record = model(guest);
        require(materials.feed_models(render::encode(record), now), "Controlled model was rejected");
        require(materials.feed_textures(texture::encode(sprite(record)), now),
                "Controlled sprite was rejected");
        require(shapes.feed(geometry::encode(receipt)), "Controlled geometry was rejected");
        const host::Matrix4 projection{
            0.5, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1.125, 1, 0, 0, -1.125, 0
        };
        require(observer.capture(evidence,
            {host_camera.key, host_camera.view, projection, host_camera.position}),
            "Controlled host camera was rejected");
        association = {guest.key, receipt.epoch, receipt.revision,
            {dimension, 0, 0, 3}, state};
    }

    std::optional<ProjectionKey> capture() {
        return projector.capture(calibration, guest, host_camera, association, evidence, now);
    }

    void advance() {
        ++guest.key.frame;
        ++guest.key.capture;
        ++host_camera.key.frame;
        ++host_camera.key.capture;
        ++receipt.revision;
        publish();
    }

    void restart() {
        ++guest.key.epoch;
        guest.key.frame = 1;
        guest.key.capture = 1;
        receipt.epoch = guest.key.epoch;
        receipt.revision = 1;
        ++host_camera.key.frame;
        ++host_camera.key.capture;
        materials.begin(guest.key.epoch, guest.key.world, guest.key.camera, dimension, now);
        shapes.begin(guest.key.epoch, mapping);
        publish();
    }
};

static ProjectionKey activated(Fixture& fixture, std::uint64_t serial) {
    auto key = fixture.capture();
    require(key && key->serial == serial && fixture.projector.retained_points() == 12,
            "Controlled projection did not activate");
    return std::move(*key);
}

static void rejected_geometry_retires_projection() {
    Fixture fixture;
    const auto old_key = activated(fixture, 1);
    auto replacement = fixture.receipt;
    ++replacement.revision;
    const auto bytes = geometry::encode(replacement);
    const auto failures_before = injected_failures;
    bool propagated = false;
    {
        FailAllocation fault;
        try { (void)fixture.shapes.feed(bytes); }
        catch (const InjectedAllocationFailure&) { propagated = true; }
    }
    require(propagated && injected_failures == failures_before + 1,
            "Geometry decoding did not preserve the injected bad_alloc type");
    require(fixture.shapes.state() == geometry::ReceiverState::rejected
            && fixture.shapes.epoch() == 0, "Allocation failure left geometry active");
    require(!fixture.shapes.last_record() && fixture.shapes.staged_positions() == 0
            && fixture.shapes.buffered_bytes() == 0, "Allocation failure retained geometry ownership");
    require(!fixture.shapes.feed(bytes), "Rejected geometry resumed without a new epoch");
    require(!fixture.projector.poll(fixture.evidence, now)
            && fixture.projector.retained_points() == 0, "Failed geometry retained its old projection");
    require(!fixture.projector.snapshot(old_key, fixture.evidence, now)
            && !fixture.projector.current_key(fixture.evidence, now), "Failed geometry left an observable old key");
    fixture.restart();
    const auto recovered = activated(fixture, 2);
    require(recovered.guest_capture.epoch == 18
            && fixture.projector.snapshot(recovered, fixture.evidence, now).has_value(),
            "Fresh geometry epoch did not recover projection ownership");
    require(!fixture.projector.snapshot(old_key, fixture.evidence, now),
            "New epoch revived an old projection key");

    auto malformed = fixture.receipt;
    ++malformed.revision;
    auto malformed_bytes = geometry::encode(malformed);
    malformed_bytes[0] ^= 1;
    require(!fixture.shapes.feed(malformed_bytes)
            && fixture.shapes.state() == geometry::ReceiverState::malformed,
            "Exception cleanup changed malformed-record classification");
    require(!fixture.shapes.last_record() && !fixture.projector.poll(fixture.evidence, now)
            && fixture.projector.retained_points() == 0, "Malformed geometry retained projection ownership");
}

static void failed_result_copy(bool full_snapshot) {
    Fixture fixture;
    const auto old_key = activated(fixture, 1);
    bool live = false;
    std::size_t source_allocations = 0;
    {
        CountAllocations count;
        live = fixture.projector.poll(fixture.evidence, now);
        source_allocations = count.count();
    }
    require(live && source_allocations != 0, "Source-copy allocation baseline was not observed");
    // The next call validates unchanged sources through the same poll path.
    // Failing its following allocation targets the owned result copy, not polling.
    const auto failures_before = injected_failures;
    bool propagated = false;
    {
        FailAllocation fault(source_allocations);
        try {
            if (full_snapshot) (void)fixture.projector.snapshot(old_key, fixture.evidence, now);
            else (void)fixture.projector.current_key(fixture.evidence, now);
        } catch (const InjectedAllocationFailure&) { propagated = true; }
    }
    require(propagated && injected_failures == failures_before + 1,
            "Result-copy failure did not preserve the injected bad_alloc type");
    require(fixture.projector.retained_points() == 0
            && !fixture.projector.current_key(fixture.evidence, now)
            && !fixture.projector.snapshot(old_key, fixture.evidence, now),
            "Failed result copy left owned projection data observable");
    require(fixture.shapes.state() == geometry::ReceiverState::active
            && fixture.shapes.last_record().has_value()
            && fixture.materials.state() == material::CoordinationState::active
            && fixture.materials.ready_positions() == 1,
            "Result-copy failure destroyed independently owned source data");
    fixture.advance();
    const auto next = activated(fixture, 2);
    require(fixture.projector.snapshot(next, fixture.evidence, now).has_value(),
            "Result-copy failure prevented a fresh capture");
}

static void failed_capture_key_copy_does_not_commit() {
    Fixture baseline;
    (void)activated(baseline, 1);
    baseline.advance();
    std::optional<ProjectionKey> result;
    std::size_t capture_allocations = 0;
    {
        CountAllocations count;
        result = baseline.capture();
        capture_allocations = count.count();
    }
    require(result && result->serial == 2 && capture_allocations != 0,
            "Capture allocation baseline was not observed");

    Fixture fixture;
    const auto old_key = activated(fixture, 1);
    fixture.advance();
    const auto failures_before = injected_failures;
    bool propagated = false;
    {
        // All allocations in this unchanged capture path precede the move-only
        // commit. Its final allocation copies the returned key's bounded state.
        FailAllocation fault(capture_allocations - 1);
        try { (void)fixture.capture(); }
        catch (const InjectedAllocationFailure&) { propagated = true; }
    }
    require(propagated && injected_failures == failures_before + 1,
            "Capture result failure did not preserve the injected bad_alloc type");
    require(fixture.projector.retained_points() == 0
            && !fixture.projector.snapshot(old_key, fixture.evidence, now),
            "Failed capture result retained partially committed ownership");
    const auto retry = activated(fixture, 2);
    require(retry.guest_capture == fixture.guest.key && retry.geometry.revision == 2,
            "Failed return-copy advanced capture history before commit");
    require(fixture.projector.snapshot(retry, fixture.evidence, now).has_value(),
            "Retry of the uncommitted capture was not observable");
}

int main() {
    try {
        rejected_geometry_retires_projection();
        failed_result_copy(true);
        failed_result_copy(false);
        failed_capture_key_copy_does_not_commit();
        require(injected_failures == 4, "Unexpected number of injected allocation failures");
        std::cout << "geometry-exception: passed " << assertions
                  << " assertions, " << injected_failures << " controlled bad_alloc failures\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "geometry-exception: " << error.what() << '\n';
        return 1;
    }
}
