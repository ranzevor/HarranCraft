#include <harrancraft/material_receiver.hpp>

#include <array>
#include <bit>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace harrancraft;

static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

static render::Record model(std::uint64_t epoch, std::uint64_t frame,
                            std::uint64_t capture) {
    render::Record m;
    m.identity = {epoch, 7, 9, frame, capture};
    m.key = {"minecraft:overworld", 0, 80, 0};
    m.state = "minecraft:stone";
    m.pose = {0.5, 82, -4, 90, -10};
    for (const char* name : {"minecraft:block/stone", "minecraft:block/ore"}) {
        render::Quad quad;
        quad.face = 2;
        quad.atlas = "minecraft:textures/atlas/blocks.png";
        quad.sprite = name;
        m.quads.push_back(quad);
    }
    return m;
}

static texture::Record sprite(std::uint64_t epoch, std::uint64_t frame,
                              std::uint64_t capture, std::string name) {
    texture::Record r;
    r.identity = {epoch, 7, 9, frame, capture};
    r.atlas = "minecraft:textures/atlas/blocks.png";
    r.sprite = std::move(name);
    r.width = 2;
    r.height = 2;
    r.min_u = 0.125f;
    r.max_u = 0.25f;
    r.min_v = 0.375f;
    r.max_v = 0.5f;
    r.argb = {0xff112233U, 0x80123456U, 0U, 0xffffffffU};
    return r;
}

static void fragments(material::MaterialReceiver& receiver,
                      const texture::Bytes& bytes, std::size_t step) {
    for (std::size_t at = 0; at < bytes.size(); at += step) {
        require(receiver.feed(std::span<const std::uint8_t>(
                    bytes.data() + at, std::min(step, bytes.size() - at))),
                "Valid source sprite fragment rejected");
        require(receiver.buffered_bytes() <= texture::max_bytes,
                "Source sprite buffer exceeds fixed size");
    }
}

int main() {
    try {
        material::MaterialReceiver receiver;
        require(receiver.state() == material::ReceiveState::idle,
                "Unexpected initial receiver state");
        receiver.begin(17, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(17, 10, 1)),
                "Model manifest was refused");
        require(receiver.required() == 2 && !receiver.ready(),
                "Model was activated without sprite pixels");
        const auto stone = texture::encode(sprite(17, 10, 1, "minecraft:block/stone"));
        const auto ore = texture::encode(sprite(17, 10, 1, "minecraft:block/ore"));
        fragments(receiver, stone, 1);
        require(receiver.complete() == 1 && !receiver.ready(),
                "Incomplete materials became ready");
        fragments(receiver, ore, 37);
        require(receiver.ready() && receiver.complete() == 2,
                "All materials did not become available");
        auto source = receiver.find({"minecraft:textures/atlas/blocks.png",
                                     "minecraft:block/ore"});
        require(source && source->argb.size() == 4 && source->argb[0] == 0xff112233U,
                "Received source pixel values were lost");
        require(!receiver.feed(ore) && receiver.state() == material::ReceiveState::rejected
                    && receiver.complete() == 0, "Duplicate pixels did not invalidate batch");

        receiver.begin(18, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(18, 1, 1)), "New material epoch failed");
        fragments(receiver, texture::encode(sprite(18, 1, 1, "minecraft:block/stone")), 511);
        receiver.end_of_stream();
        require(receiver.state() == material::ReceiveState::incomplete
                    && receiver.complete() == 0 && receiver.epoch() == 0,
                "Incomplete EOF left source data in cache");

        receiver.begin(19, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(19, 1, 1)), "Session 19 failed");
        const auto old = texture::encode(sprite(18, 1, 1, "minecraft:block/stone"));
        require(!receiver.feed(old) && receiver.state() == material::ReceiveState::rejected,
                "Old epoch pixels were accepted");

        receiver.begin(20, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(20, 1, 1)), "Session 20 failed");
        auto damaged = texture::encode(sprite(20, 1, 1, "minecraft:block/stone"));
        damaged[0] ^= 0xff;
        require(!receiver.feed(damaged)
                    && receiver.state() == material::ReceiveState::malformed,
                "Corrupt texture version was accepted");

        receiver.begin(21, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(21, 1, 1)), "Session 21 failed");
        damaged = texture::encode(sprite(21, 1, 1, "minecraft:block/stone"));
        texture::put(damaged, 8, texture::max_bytes + 1, 4);
        require(!receiver.feed(std::span<const std::uint8_t>(damaged.data(), 12))
                    && receiver.state() == material::ReceiveState::malformed
                    && receiver.buffered_bytes() == 0,
                "Oversize texture record was buffered");

        receiver.begin(22, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(22, 1, 1)), "Session 22 failed");
        damaged = texture::encode(sprite(22, 1, 1, "minecraft:block/stone"));
        require(receiver.feed(std::span<const std::uint8_t>(damaged.data(), 11)),
                "Partial header was rejected before EOF");
        receiver.end_of_stream();
        require(receiver.state() == material::ReceiveState::truncated,
                "Truncated source-image header went unnoticed");

        receiver.begin(23, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(23, 1, 1)), "Session 23 failed");
        damaged = texture::encode(sprite(23, 1, 1, "minecraft:block/stone"));
        require(receiver.feed(std::span<const std::uint8_t>(
                    damaged.data(), damaged.size() - 1)),
                "Partial source pixels were refused prematurely");
        receiver.end_of_stream();
        require(receiver.state() == material::ReceiveState::truncated,
                "Incomplete pixels at EOF were accepted");

        receiver.begin(24, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(24, 1, 1)), "Session 24 failed");
        damaged = texture::encode(sprite(24, 1, 1, "minecraft:block/stone"));
        require(receiver.feed(std::span<const std::uint8_t>(damaged.data(), 9)),
                "Partial material record refused");
        require(!receiver.begin_model(model(24, 2, 2))
                    && receiver.state() == material::ReceiveState::rejected,
                "Model replaced a partially buffered source record");

        receiver.begin(25, 7, 9, "minecraft:overworld");
        auto air = model(25, 1, 1);
        air.state = "minecraft:air";
        air.quads.clear();
        require(receiver.begin_model(air) && !receiver.ready()
                    && receiver.required() == 0,
                "Air model did not retire materials");
        receiver.end_of_stream();
        require(receiver.state() == material::ReceiveState::ended
                    && receiver.epoch() == 0, "Air EOF retained epoch");

        receiver.begin(26, 7, 9, "minecraft:overworld");
        require(receiver.begin_model(model(26, 1, 1)), "Session 26 failed");
        auto other = std::async(std::launch::async, [&receiver] {
            try { (void)receiver.ready(); }
            catch (const std::logic_error&) { return true; }
            return false;
        });
        require(other.get(), "Foreign material receiver access was permitted");
        receiver.stop();
        receiver.stop();
        require(receiver.state() == material::ReceiveState::stopped
                    && receiver.epoch() == 0 && receiver.complete() == 0,
                "Explicit material revocation kept pixels alive");

        std::cout << "material-receiver: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
