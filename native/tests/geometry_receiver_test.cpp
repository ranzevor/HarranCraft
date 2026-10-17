#include <harrancraft/geometry_receiver.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace harrancraft::geometry;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static CoordinateMap mapping() {
    CoordinateMap map;
    map.dimension = "minecraft:overworld";
    map.scale = {2, 1, -1};
    map.origin = {10, 0, 10};
    return map;
}

static Frame snapshot(std::uint64_t epoch, std::uint64_t revision,
                      std::int32_t x = 0) {
    return {Kind::snapshot, epoch, revision,
            {"minecraft:overworld", x, 80, 0}, "minecraft:stone",
            {{0, 0, 0, 1, 1, 1}}};
}

static Position position() {
    return {"minecraft:overworld", 0, 80, 0};
}

static void fragments(GeometryReceiver& receiver, const Bytes& bytes, std::size_t step) {
    for (std::size_t at = 0; at < bytes.size(); at += step) {
        const auto remaining = std::min(step, bytes.size() - at);
        require(receiver.feed(std::span<const std::uint8_t>(
                    bytes.data() + at, remaining)), "Valid fragment rejected");
        require(receiver.buffered_bytes() <= max_bytes,
                "Receiver exceeded its fixed frame capacity");
    }
}

int main() {
    try {
        GeometryReceiver receiver;
        require(receiver.state() == ReceiverState::idle, "Expected initial state");
        bool invalid = false;
        try { receiver.begin(0, mapping()); }
        catch (const std::invalid_argument&) { invalid = true; }
        require(invalid && receiver.epoch() == 0, "Zero receiver epoch was accepted");

        receiver.begin(17, mapping());
        const auto first = encode(snapshot(17, 1));
        fragments(receiver, first, 1);
        require(receiver.records() == 1 && receiver.last_revision() == 1,
                "Fragmented frame did not finish");
        auto candidate = receiver.find(position());
        require(candidate && candidate->boxes.size() == 1, "No staged collision candidate");
        require(candidate->boxes[0].minimum == std::array<double, 3>{10, 80, 9}
                && candidate->boxes[0].maximum == std::array<double, 3>{12, 81, 10},
                "Mapped collision bounds differ");

        auto second = snapshot(17, 2);
        second.boxes[0].max_y = 0.5;
        fragments(receiver, encode(second), 73);
        require(receiver.staged_positions() == 1 && receiver.last_revision() == 2,
                "Second frame did not replace the candidate");

        const auto air = Frame{Kind::snapshot, 17, 3,
                               snapshot(17, 1).key, "minecraft:air", {}};
        const auto retire_frame = Frame{Kind::retire, 17, 4, air.key, "", {}};
        const auto air_bytes = encode(air);
        const auto retire_bytes = encode(retire_frame);
        Bytes joined;
        joined.insert(joined.end(), air_bytes.begin(), air_bytes.end());
        joined.insert(joined.end(), retire_bytes.begin(), retire_bytes.end());
        require(receiver.feed(joined), "Coalesced geometry records refused");
        require(receiver.records() == 4 && receiver.staged_positions() == 0,
                "Retirement left staged geometry");
        receiver.end_of_stream();
        require(receiver.state() == ReceiverState::ended && receiver.epoch() == 0,
                "Clean EOF did not retire session");
        require(!receiver.feed(first), "Retired receiver accepted old bytes");

        invalid = false;
        try { receiver.begin(17, mapping()); }
        catch (const std::invalid_argument&) { invalid = true; }
        require(invalid, "Retired epoch was reused");
        receiver.begin(18, mapping());
        require(!receiver.feed(first) && receiver.state() == ReceiverState::rejected,
                "Foreign epoch was accepted");
        require(receiver.staged_positions() == 0, "Foreign epoch retained candidates");

        receiver.begin(19, mapping());
        const auto changed = encode(snapshot(19, 1));
        require(receiver.feed(changed) && receiver.staged_positions() == 1,
                "New valid session rejected");
        require(!receiver.feed(changed) && receiver.state() == ReceiverState::rejected,
                "Duplicate revision did not revoke receiver");
        require(receiver.staged_positions() == 0, "Replay left previously staged shape");

        receiver.begin(20, mapping());
        require(receiver.feed(encode(snapshot(20, 1))), "First gap frame refused");
        require(!receiver.feed(encode(snapshot(20, 3)))
                    && receiver.state() == ReceiverState::rejected,
                "Skipped revision was accepted");
        require(receiver.staged_positions() == 0, "Missing revision retained candidate");

        receiver.begin(21, mapping());
        auto mismatched = snapshot(21, 1);
        mismatched.key.dimension = "minecraft:the_nether";
        require(!receiver.feed(encode(mismatched))
                    && receiver.state() == ReceiverState::rejected,
                "Wrong dimension was accepted");

        receiver.begin(22, mapping());
        auto malformed = encode(snapshot(22, 1));
        malformed[0] ^= 0xff;
        require(!receiver.feed(malformed)
                    && receiver.state() == ReceiverState::malformed,
                "Invalid record header passed");

        receiver.begin(23, mapping());
        auto oversized = encode(snapshot(23, 1));
        put(oversized, 8, max_bytes + 1, 4);
        require(!receiver.feed(std::span<const std::uint8_t>(oversized.data(), 12))
                    && receiver.state() == ReceiverState::malformed,
                "Oversized record length was accepted");
        require(receiver.buffered_bytes() == 0 && receiver.staged_positions() == 0,
                "Malformed frame retained buffer or candidates");

        receiver.begin(24, mapping());
        auto truncated = encode(snapshot(24, 1));
        require(receiver.feed(std::span<const std::uint8_t>(truncated.data(), 9)),
                "Incomplete header rejected prematurely");
        receiver.end_of_stream();
        require(receiver.state() == ReceiverState::truncated,
                "Truncated header at EOF went unnoticed");

        receiver.begin(25, mapping());
        truncated = encode(snapshot(25, 1));
        require(receiver.feed(std::span<const std::uint8_t>(
                    truncated.data(), truncated.size() - 1)),
                "Incomplete body rejected prematurely");
        receiver.end_of_stream();
        require(receiver.state() == ReceiverState::truncated
                    && receiver.staged_positions() == 0,
                "Partial body was not retired");

        receiver.begin(26, mapping());
        require(receiver.feed(encode(snapshot(26, 1))), "Before-EOF frame was refused");
        require(receiver.staged_positions() == 1, "Expected live shape before EOF");
        receiver.end_of_stream();
        require(receiver.state() == ReceiverState::ended && receiver.staged_positions() == 0,
                "Clean EOF retained candidate");
        receiver.begin(27, mapping());
        auto cross = std::async(std::launch::async, [&receiver] {
            try { (void)receiver.feed({}); }
            catch (const std::logic_error&) { return true; }
            return false;
        });
        require(cross.get(), "Cross-thread reader was accepted");
        require(receiver.state() == ReceiverState::active, "Cross-thread attempt modified state");

        auto all = encode(snapshot(27, 1));
        require(receiver.feed(all), "Final receiver input rejected");
        receiver.stop();
        receiver.stop();
        require(receiver.state() == ReceiverState::stopped
                    && receiver.staged_positions() == 0,
                "Explicit stop did not clear candidate state");

        receiver.begin(28, mapping());
        require(receiver.feed(encode(snapshot(28, 1))), "Pre-corruption frame refused");
        auto invalid_float = encode(snapshot(28, 2));
        put(invalid_float, header_bytes + 24,
            std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()), 8);
        require(!receiver.feed(invalid_float)
                    && receiver.state() == ReceiverState::malformed
                    && receiver.staged_positions() == 0,
                "Non-finite payload did not revoke live candidates");

        std::cout << "geometry-receiver: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
