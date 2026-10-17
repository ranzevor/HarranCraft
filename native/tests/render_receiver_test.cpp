#include <harrancraft/render_receiver.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace harrancraft::render;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static Record sample(std::uint64_t epoch, std::uint64_t capture,
                     std::uint64_t frame, std::int32_t x = 0) {
    Record record;
    record.identity = {epoch, 7, 9, frame, capture};
    record.key = {"minecraft:overworld", x, 80, 0};
    record.state = "minecraft:stone";
    record.pose = {0.5, 82, -4, 90, -10};
    Quad quad;
    quad.face = 2;
    quad.atlas = "minecraft:textures/atlas/blocks.png";
    quad.sprite = "minecraft:block/stone";
    for (std::size_t i = 0; i < 4; ++i) {
        quad.vertices[i] = {static_cast<float>(i), 1, 0.5f, 0.25f, 0.75f,
                            0xff00aa55U, 0xf000f0U, 0x7f0000U};
    }
    record.quads.push_back(quad);
    return record;
}

static void fragmented(RenderReceiver& receiver, const Bytes& bytes,
                       std::size_t fragment) {
    for (std::size_t at = 0; at < bytes.size(); at += fragment) {
        require(receiver.feed(std::span<const std::uint8_t>(
                    bytes.data() + at, std::min(fragment, bytes.size() - at))),
                "Valid render fragment was rejected");
        require(receiver.buffered_bytes() <= max_bytes,
                "Renderer exceeded the bounded receive buffer");
    }
}

int main() {
    try {
        RenderReceiver receiver;
        require(receiver.state() == ReceiveState::idle, "Wrong initial state");
        bool rejected = false;
        try { receiver.begin({0, 7, 9, 0, 0}, "minecraft:overworld"); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Zero epoch was accepted");

        receiver.begin({17, 7, 9, 0, 0}, "minecraft:overworld");
        const auto first = encode(sample(17, 1, 10));
        fragmented(receiver, first, 1);
        require(receiver.records() == 1 && receiver.last_capture() == 1
                    && receiver.last_frame() == 10, "First frame identity changed");
        auto stored = receiver.find({"minecraft:overworld", 0, 80, 0});
        require(stored && stored->quads.size() == 1
                    && stored->quads[0].vertices[0].color == 0xff00aa55U,
                "Source render quad was not retained");

        const auto second = encode(sample(17, 2, 12));
        fragmented(receiver, second, 517);
        auto air = sample(17, 3, 20);
        air.state = "minecraft:air";
        air.quads.clear();
        auto retire_record = sample(17, 4, 22);
        retire_record.kind = Kind::retire;
        retire_record.state.clear();
        retire_record.pose = {};
        retire_record.quads.clear();
        Bytes combined = encode(air);
        const auto retirement = encode(retire_record);
        combined.insert(combined.end(), retirement.begin(), retirement.end());
        require(receiver.feed(combined) && receiver.records() == 4
                    && receiver.staged_positions() == 0,
                "Combined retirement records retained geometry");
        receiver.end_of_stream();
        require(receiver.state() == ReceiveState::ended
                    && receiver.epoch() == 0 && receiver.staged_positions() == 0,
                "Clean EOF did not clear staged render models");

        rejected = false;
        try { receiver.begin({17, 7, 9, 0, 0}, "minecraft:overworld"); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Retired render epoch was accepted");

        receiver.begin({18, 7, 9, 0, 0}, "minecraft:overworld");
        require(!receiver.feed(first) && receiver.state() == ReceiveState::rejected,
                "Old epoch frame accepted");

        receiver.begin({19, 7, 9, 0, 0}, "minecraft:overworld");
        require(receiver.feed(encode(sample(19, 1, 1))), "New epoch frame rejected");
        require(!receiver.feed(encode(sample(19, 1, 2)))
                    && receiver.state() == ReceiveState::rejected
                    && receiver.staged_positions() == 0,
                "Repeated capture did not revoke staged quads");

        receiver.begin({20, 7, 9, 0, 0}, "minecraft:overworld");
        require(receiver.feed(encode(sample(20, 1, 1))), "First capture rejected");
        require(!receiver.feed(encode(sample(20, 3, 2)))
                    && receiver.state() == ReceiveState::rejected,
                "Missing capture was accepted");

        receiver.begin({21, 7, 9, 0, 0}, "minecraft:overworld");
        require(receiver.feed(encode(sample(21, 1, 5))), "First frame rejected");
        require(!receiver.feed(encode(sample(21, 2, 5)))
                    && receiver.state() == ReceiveState::rejected,
                "Stale logical frame was accepted");

        receiver.begin({22, 7, 9, 0, 0}, "minecraft:overworld");
        auto changed = sample(22, 1, 1);
        changed.identity.camera = 10;
        require(!receiver.feed(encode(changed))
                    && receiver.state() == ReceiveState::rejected,
                "Camera identity change was accepted");

        receiver.begin({23, 7, 9, 0, 0}, "minecraft:overworld");
        changed = sample(23, 1, 1);
        changed.key.dimension = "minecraft:the_nether";
        require(!receiver.feed(encode(changed))
                    && receiver.state() == ReceiveState::rejected,
                "Dimension change was accepted");

        receiver.begin({24, 7, 9, 0, 0}, "minecraft:overworld");
        auto invalid = encode(sample(24, 1, 1));
        invalid[4] ^= 1;
        require(!receiver.feed(invalid) && receiver.state() == ReceiveState::malformed,
                "Unknown format version accepted");

        receiver.begin({25, 7, 9, 0, 0}, "minecraft:overworld");
        invalid = encode(sample(25, 1, 1));
        put(invalid, 8, max_bytes + 1, 4);
        require(!receiver.feed(std::span<const std::uint8_t>(invalid.data(), 12))
                    && receiver.state() == ReceiveState::malformed,
                "Unbounded frame length accepted");

        receiver.begin({26, 7, 9, 0, 0}, "minecraft:overworld");
        invalid = encode(sample(26, 1, 1));
        put(invalid, header_bytes + 272,
            std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()), 4);
        require(!receiver.feed(invalid) && receiver.state() == ReceiveState::malformed,
                "Non-finite vertex was accepted");

        receiver.begin({27, 7, 9, 0, 0}, "minecraft:overworld");
        invalid = encode(sample(27, 1, 1));
        require(receiver.feed(std::span<const std::uint8_t>(invalid.data(), 7)),
                "Fragmented header prematurely failed");
        receiver.end_of_stream();
        require(receiver.state() == ReceiveState::truncated
                    && receiver.staged_positions() == 0,
                "Truncated render header went unnoticed");

        receiver.begin({28, 7, 9, 0, 0}, "minecraft:overworld");
        invalid = encode(sample(28, 1, 1));
        require(receiver.feed(std::span<const std::uint8_t>(
                    invalid.data(), invalid.size() - 1)),
                "Incomplete payload rejected prematurely");
        receiver.end_of_stream();
        require(receiver.state() == ReceiveState::truncated,
                "Truncated render quad at EOF went unnoticed");

        receiver.begin({29, 7, 9, 0, 0}, "minecraft:overworld");
        for (std::size_t i = 0; i < RenderReceiver::max_positions; ++i) {
            require(receiver.feed(encode(sample(29, static_cast<std::uint64_t>(i) + 1,
                                                static_cast<std::uint64_t>(i) + 1,
                                                static_cast<std::int32_t>(i)))),
                    "Render staging exhausted before configured capacity");
        }
        require(receiver.staged_positions() == RenderReceiver::max_positions,
                "Staged position count incorrect");
        const auto extra = RenderReceiver::max_positions;
        require(!receiver.feed(encode(sample(29, static_cast<std::uint64_t>(extra) + 1,
                                              static_cast<std::uint64_t>(extra) + 1,
                                              static_cast<std::int32_t>(extra))))
                    && receiver.state() == ReceiveState::capacity
                    && receiver.staged_positions() == 0,
                "Render staging overflow did not revoke session");

        receiver.begin({30, 7, 9, 0, 0}, "minecraft:overworld");
        auto foreign = std::async(std::launch::async, [&receiver] {
            try { (void)receiver.feed({}); }
            catch (const std::logic_error&) { return true; }
            return false;
        });
        require(foreign.get(), "Foreign receive thread was accepted");
        require(receiver.state() == ReceiveState::active,
                "Foreign receive thread changed lifecycle");
        require(receiver.feed(encode(sample(30, 1, 1)))
                    && receiver.staged_positions() == 1,
                "Valid staging after owner-thread refusal failed");
        receiver.stop();
        receiver.stop();
        require(receiver.state() == ReceiveState::stopped
                    && receiver.staged_positions() == 0 && receiver.epoch() == 0,
                "Render receiver stop failed to clear staged models");
        std::cout << "render-receiver: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
