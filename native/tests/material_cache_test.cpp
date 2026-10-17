#include <harrancraft/material_cache.hpp>

#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace harrancraft;

static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

static render::Record model(std::uint64_t epoch, std::uint64_t frame,
                            std::uint64_t capture) {
    render::Record r;
    r.identity = {epoch, 7, 9, frame, capture};
    r.key = {"minecraft:overworld", 0, 80, 0};
    r.state = "minecraft:stone";
    r.pose = {0.5, 82, -4, 0, 0};
    for (const auto name : {"minecraft:block/stone", "minecraft:block/ore"}) {
        render::Quad quad;
        quad.face = 2;
        quad.atlas = "minecraft:textures/atlas/blocks.png";
        quad.sprite = name;
        r.quads.push_back(quad);
    }
    return r;
}

static texture::Record pixels(std::uint64_t epoch, std::uint64_t frame,
                              std::uint64_t capture, std::string sprite) {
    texture::Record r;
    r.identity = {epoch, 7, 9, frame, capture};
    r.atlas = "minecraft:textures/atlas/blocks.png";
    r.sprite = std::move(sprite);
    r.width = 2;
    r.height = 2;
    r.min_u = 0.125f;
    r.max_u = 0.25f;
    r.min_v = 0.375f;
    r.max_v = 0.5f;
    r.argb = {0xff112233U, 0x80123456U, 0U, 0xffffffffU};
    return r;
}

static material::SpriteKey key(std::string sprite) {
    return {"minecraft:textures/atlas/blocks.png", std::move(sprite)};
}

int main() {
    try {
        material::MaterialCache cache;
        bool refused = false;
        try { cache.begin(0, 7, 9, "minecraft:overworld"); }
        catch (const std::invalid_argument&) { refused = true; }
        require(refused, "Zero epoch accepted");
        cache.begin(17, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(17, 10, 1)) && cache.expected() == 2
                    && !cache.ready(), "Model did not establish material manifest");
        const auto stone = pixels(17, 10, 1, "minecraft:block/stone");
        const auto ore = pixels(17, 10, 1, "minecraft:block/ore");
        require(cache.accept(stone) && cache.received() == 1 && !cache.ready()
                    && !cache.find(key("minecraft:block/stone")),
                "Partial model material was published");
        require(cache.accept(ore) && cache.ready() && cache.received() == 2,
                "Complete model did not become ready");
        const auto obtained = cache.find(key("minecraft:block/stone"));
        require(obtained && obtained->argb == stone.argb, "ARGB model source pixels changed");
        require(!cache.accept(stone) && cache.state() == material::State::invalid
                    && cache.received() == 0 && !cache.ready(),
                "Duplicate source image did not revoke materials");

        cache.begin(18, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(18, 1, 1)), "New session failed");
        auto rogue = pixels(18, 1, 1, "minecraft:block/unexpected");
        require(!cache.accept(rogue) && cache.state() == material::State::invalid,
                "Unknown sprite ID was accepted");

        cache.begin(19, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(19, 1, 1)), "Session 19 model failed");
        auto wrong_camera = pixels(19, 1, 1, "minecraft:block/stone");
        wrong_camera.identity.camera = 10;
        require(!cache.accept(wrong_camera) && cache.received() == 0,
                "Foreign camera image was accepted");

        cache.begin(20, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(20, 1, 1)), "Session 20 model failed");
        auto late = pixels(20, 1, 2, "minecraft:block/stone");
        require(!cache.accept(late) && cache.state() == material::State::invalid,
                "Late capture was accepted");

        cache.begin(21, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(21, 1, 1)), "Session 21 model failed");
        require(cache.accept(pixels(21, 1, 1, "minecraft:block/stone")),
                "First source pixel rejected");
        cache.end_of_stream();
        require(cache.state() == material::State::ended && cache.received() == 0,
                "Partial EOF retained stale source pixels");

        cache.begin(22, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(22, 1, 1)), "Session 22 model failed");
        auto withdrawal = pixels(22, 1, 1, "minecraft:block/stone");
        withdrawal.kind = texture::Kind::retire;
        withdrawal.width = 0;
        withdrawal.height = 0;
        withdrawal.min_u = 0;
        withdrawal.max_u = 0;
        withdrawal.min_v = 0;
        withdrawal.max_v = 0;
        withdrawal.argb.clear();
        require(cache.accept(withdrawal) && cache.state() == material::State::retired
                    && cache.received() == 0, "Sprite invalidation left active material");

        cache.begin(23, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(23, 1, 1)), "Session 23 model failed");
        require(cache.accept(pixels(23, 1, 1, "minecraft:block/stone"))
                    && cache.accept(pixels(23, 1, 1, "minecraft:block/ore"))
                    && cache.ready(), "First complete batch failed");
        auto replacement = model(23, 2, 2);
        replacement.quads.pop_back();
        require(cache.begin_model(replacement) && cache.expected() == 1
                    && cache.received() == 0 && !cache.ready()
                    && !cache.find(key("minecraft:block/ore")),
                "Model replacement reused previously cached materials");
        require(cache.accept(pixels(23, 2, 2, "minecraft:block/stone"))
                    && cache.ready() && cache.received() == 1,
                "Model resnapshot failed");
        require(!cache.begin_model(model(23, 3, 4))
                    && cache.state() == material::State::invalid,
                "Missing capture identity was accepted");

        cache.begin(24, 7, 9, "minecraft:overworld");
        auto oversized = model(24, 1, 1);
        oversized.quads.clear();
        for (int i = 0; i < 17; ++i) {
            render::Quad quad;
            quad.atlas = "minecraft:textures/atlas/blocks.png";
            quad.sprite = "minecraft:block/sprite" + std::to_string(i);
            oversized.quads.push_back(std::move(quad));
        }
        require(!cache.begin_model(oversized) && cache.state() == material::State::invalid,
                "More than 16 sprites accepted");

        cache.begin(25, 7, 9, "minecraft:overworld");
        auto air = model(25, 1, 1);
        air.state = "minecraft:air";
        air.quads.clear();
        require(cache.begin_model(air) && cache.state() == material::State::retired
                    && !cache.ready(), "Air snapshot retained material resources");

        cache.begin(26, 7, 9, "minecraft:overworld");
        require(cache.begin_model(model(26, 1, 1)), "Session 26 model failed");
        auto foreign = std::async(std::launch::async, [&cache] {
            try { (void)cache.ready(); }
            catch (const std::logic_error&) { return true; }
            return false;
        });
        require(foreign.get() && cache.state() == material::State::pending,
                "Foreign thread crossed material owner boundary");
        cache.stop();
        cache.stop();
        require(cache.state() == material::State::stopped && cache.epoch() == 0,
                "Manual resource reload did not invalidate cache");
        refused = false;
        try { cache.begin(26, 7, 9, "minecraft:overworld"); }
        catch (const std::invalid_argument&) { refused = true; }
        require(refused, "Stopped epoch was reused");
        std::cout << "material-cache: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
