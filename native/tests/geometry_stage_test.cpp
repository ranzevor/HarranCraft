#include <harrancraft/geometry_stage.hpp>

#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace harrancraft::geometry;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static Frame snapshot(std::uint64_t epoch, std::uint64_t revision,
                      std::int32_t x = -17) {
    return {Kind::snapshot, epoch, revision,
            {"minecraft:overworld", x, 64, 33}, "minecraft:oak_slab[type=bottom,waterlogged=false]",
            {{0, 0, 0, 1, 0.5, 1}}};
}

static Position position(std::int32_t x = -17) {
    return {"minecraft:overworld", x, 64, 33};
}

int main() {
    try {
        CandidateLedger ledger;
        CoordinateMap mapping;
        mapping.dimension = "minecraft:overworld";
        mapping.source_axis = {2, 1, 0};
        mapping.scale = {-2, 1, 0.5};
        mapping.origin = {100, 0, -4};
        require(valid_map(mapping), "Valid signed permutation was refused");

        auto invalid_map = mapping;
        invalid_map.source_axis = {0, 0, 2};
        require(!valid_map(invalid_map), "Repeated axis was accepted");
        invalid_map = mapping;
        invalid_map.scale[0] = 0;
        require(!valid_map(invalid_map), "Zero scale was accepted");
        invalid_map = mapping;
        invalid_map.origin[2] = std::numeric_limits<double>::infinity();
        require(!valid_map(invalid_map), "Infinite origin was accepted");

        bool invalid_begin = false;
        try { ledger.begin(0, mapping); }
        catch (const std::invalid_argument&) { invalid_begin = true; }
        require(invalid_begin, "Zero session epoch was accepted");
        ledger.begin(17, mapping);
        require(ledger.epoch() == 17 && ledger.size() == 0, "New session not empty");
        require(ledger.apply(snapshot(16, 1)) == StageResult::stale,
                "Old session record was accepted");
        require(ledger.apply(snapshot(17, 1)) == StageResult::applied,
                "First collision record was not staged");
        auto staged = ledger.find(position());
        require(staged && staged->boxes.size() == 1
                && staged->canonical_state == "minecraft:oak_slab[type=bottom,waterlogged=false]",
                "Candidate state was not retained");
        require(staged->boxes[0].minimum == std::array<double, 3>{32, 64, -12.5}
                && staged->boxes[0].maximum == std::array<double, 3>{34, 64.5, -12},
                "Signed guest-to-host transform changed collision bounds");
        require(ledger.apply(snapshot(17, 1)) == StageResult::stale,
                "Duplicate revision was accepted");
        require(ledger.size() == 1, "Replay duplicated a collision candidate");

        auto bad = snapshot(17, 2);
        bad.boxes[0].max_x = 0;
        require(ledger.apply(bad) == StageResult::invalid,
                "Invalid box was accepted");
        require(ledger.epoch() == 0 && ledger.size() == 0,
                "Invalid geometry did not revoke prior candidates");
        ledger.begin(18, mapping);
        require(ledger.apply(snapshot(17, 3)) == StageResult::stale,
                "Old epoch survived restart");
        require(ledger.apply(snapshot(18, 1)) == StageResult::applied,
                "Reconnected record was not staged");

        auto other_dimension = snapshot(18, 2);
        other_dimension.key.dimension = "minecraft:the_nether";
        require(ledger.apply(other_dimension) == StageResult::invalid,
                "Unmapped dimension was accepted");
        require(ledger.size() == 0 && ledger.epoch() == 0,
                "Dimension mismatch did not retire candidates");

        ledger.begin(19, mapping);
        for (std::int32_t x = 0; x < 128; ++x) {
            require(ledger.apply(snapshot(19, static_cast<std::uint64_t>(x) + 1, x))
                        == StageResult::applied, "Candidate capacity underflow");
        }
        require(ledger.size() == 128, "Unexpected candidate count");
        require(ledger.apply(snapshot(19, 129, 128)) == StageResult::capacity,
                "Excess candidate was accepted");
        require(ledger.size() == 0 && ledger.epoch() == 0,
                "Capacity refusal retained unsynchronized candidates");

        ledger.begin(20, mapping);
        auto cube = snapshot(20, 1);
        require(ledger.apply(cube) == StageResult::applied, "Failed to stage collision");
        auto air = cube;
        air.revision = 2;
        air.state = "minecraft:air";
        air.boxes.clear();
        require(ledger.apply(air) == StageResult::retired && ledger.size() == 0,
                "Air snapshot retained a collider");
        cube.revision = 3;
        require(ledger.apply(cube) == StageResult::applied, "Repeated placement failed");
        auto retirement = Frame{Kind::retire, 20, 4, cube.key, "", {}};
        require(ledger.apply(retirement) == StageResult::retired && ledger.size() == 0,
                "Explicit retirement failed");

        require(ledger.apply(snapshot(20, 5)) == StageResult::applied,
                "Another candidate could not be staged");
        auto foreign = std::async(std::launch::async, [&ledger] {
            try { (void)ledger.size(); }
            catch (const std::logic_error&) { return true; }
            return false;
        });
        require(foreign.get(), "Foreign thread could inspect staging");
        require(ledger.size() == 1, "Thread refusal modified candidate ownership");
        ledger.stop();
        ledger.stop();
        require(ledger.size() == 0, "Stop retained staged geometry");
        require(ledger.apply(snapshot(20, 6)) == StageResult::stale,
                "Retired session accepted a frame");

        auto too_large = snapshot(21, 1);
        too_large.key.x = std::numeric_limits<std::int32_t>::max();
        auto huge = mapping;
        huge.scale = {1e300, 1e300, 1e300};
        require(valid_map(huge), "Finite large mapping was rejected");
        ledger.begin(21, huge);
        require(ledger.apply(too_large) == StageResult::invalid && ledger.size() == 0,
                "Coordinate overflow was not rejected");

        ledger.begin(22, mapping);
        require(ledger.apply(snapshot(22, 2)) == StageResult::invalid,
                "Missing intermediate revision was accepted");
        require(ledger.epoch() == 0 && ledger.size() == 0,
                "Revision gap retained untrusted staged geometry");

        std::cout << "geometry-stage: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
