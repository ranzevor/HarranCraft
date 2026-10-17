#include <harrancraft/material_coordinator.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace harrancraft;
using material::Channel;
using material::CoordinationState;
using material::MaterialCoordinator;
using namespace std::chrono_literals;

static int assertions = 0;
static constexpr auto start = MaterialCoordinator::TimePoint{};
static constexpr auto atlas = "minecraft:textures/atlas/blocks.png";

static void require(bool value, const char* message) {
    ++assertions;
    if (!value) throw std::runtime_error(message);
}

static render::Record model(std::uint64_t capture = 1, std::int32_t x = 0,
                            std::size_t sprites = 2, std::uint64_t epoch = 17) {
    render::Record record;
    record.identity = {epoch, 7, 9, capture * 10, capture};
    record.key = {"minecraft:overworld", x, 80, 0};
    record.state = "minecraft:stone";
    record.pose = {0.5, 82, -4, 90, -10};
    for (std::size_t i = 0; i < sprites; ++i) {
        render::Quad quad;
        quad.face = 2;
        quad.atlas = atlas;
        quad.sprite = "minecraft:block/sprite" + std::to_string(i);
        record.quads.push_back(std::move(quad));
    }
    return record;
}

static texture::Record sprite(const render::Record& source, std::size_t index = 0,
                               bool large = false) {
    texture::Record record;
    const auto id = source.identity;
    record.identity = {id.epoch, id.world, id.camera, id.frame, id.capture};
    record.atlas = atlas;
    record.sprite = "minecraft:block/sprite" + std::to_string(index);
    record.width = large ? 64 : 2;
    record.height = large ? 64 : 2;
    record.min_u = 0.125f;
    record.max_u = 0.25f;
    record.min_v = 0.375f;
    record.max_v = 0.5f;
    record.argb.assign(static_cast<std::size_t>(record.width) * record.height,
                       0xff000000U + static_cast<std::uint32_t>(id.capture * 16 + index));
    return record;
}

static render::Position position(std::int32_t x = 0) {
    return {"minecraft:overworld", x, 80, 0};
}

static void begin(MaterialCoordinator& coordinator, std::uint64_t epoch = 17) {
    coordinator.begin(epoch, 7, 9, "minecraft:overworld", start);
}

static void cleared(MaterialCoordinator& coordinator, CoordinationState state) {
    require(coordinator.state() == state && coordinator.epoch() == 0,
            "Wrong terminal coordination state");
    require(coordinator.pending_captures() == 0 && coordinator.ready_positions() == 0
                && coordinator.retained_records() == 0 && coordinator.retained_bytes() == 0
                && coordinator.buffered_bytes() == 0 && !coordinator.find(position(), start),
            "Terminated coordinator retained source resources");
    require(!coordinator.feed_models({}, start) && !coordinator.feed_textures({}, start),
            "Closed coordination session accepted input");
}

static void send_model(MaterialCoordinator& coordinator, const render::Record& record,
                        MaterialCoordinator::TimePoint now = start) {
    require(coordinator.feed_models(render::encode(record), now), "Valid model was rejected");
}

static void send_sprite(MaterialCoordinator& coordinator, const render::Record& record,
                         std::size_t index, MaterialCoordinator::TimePoint now = start) {
    require(coordinator.feed_textures(texture::encode(sprite(record, index)), now),
            "Valid texture was rejected");
}

static void exact(MaterialCoordinator& coordinator, const render::Record& expected) {
    const auto result = coordinator.find(position(expected.key.x), start);
    require(result && result->model == expected, "Wrong ready model capture");
    require(result->sprites.size() == material::sprite_manifest(expected).size(),
            "Incomplete sprite set was made available");
    for (std::size_t i = 0; i < result->sprites.size(); ++i) {
        const auto wanted = sprite(expected, i);
        const auto found = result->sprites.find({wanted.atlas, wanted.sprite});
        require(found != result->sprites.end() && found->second == wanted,
                "Sprite identity or source pixels changed");
    }
}

static void interleavings() {
    const std::array models{model(), model(2), model(3, 1)};
    std::array<int, 9> order{0, 0, 0, 1, 1, 1, 1, 1, 1};
    int permutations = 0;
    do {
        MaterialCoordinator coordinator;
        begin(coordinator);
        std::size_t mi = 0, ti = 0;
        std::array<std::uint64_t, 2> latest{0, 0};
        for (const int channel : order) {
            if (channel == 0) {
                const auto& record = models[mi++];
                send_model(coordinator, record);
                latest[static_cast<std::size_t>(record.key.x)] = record.identity.capture;
            } else {
                send_sprite(coordinator, models[ti / 2], ti % 2);
                ++ti;
            }
            for (std::int32_t x = 0; x < 2; ++x) {
                auto ready = coordinator.find(position(x), start);
                require(!ready || ready->model.identity.capture == latest[static_cast<std::size_t>(x)],
                        "An older completed capture replaced a newer observed model");
            }
            require(coordinator.retained_bytes() <= MaterialCoordinator::max_bytes
                        && coordinator.retained_records() <= MaterialCoordinator::max_records
                        && coordinator.pending_captures() <= MaterialCoordinator::max_pending_captures,
                    "Interleaved capture exceeded ownership bounds");
        }
        exact(coordinator, models[1]);
        exact(coordinator, models[2]);
        require(coordinator.pending_captures() == 0 && coordinator.ready_positions() == 2,
                "Completed captures remained pending");
        coordinator.end_of_stream(Channel::models);
        cleared(coordinator, CoordinationState::ended);
        ++permutations;
    } while (std::next_permutation(order.begin(), order.end()));
    require(permutations == 84, "Not all two-stream FIFO interleavings were executed");
}

static void fragments() {
    const auto first = model();
    const auto model_bytes = render::encode(first);
    const auto pixels = texture::encode(sprite(first));
    for (std::size_t split = 1; split < pixels.size(); ++split) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        require(coordinator.feed_textures(std::span(pixels).first(split), start),
                "Texture fragment before model was rejected");
        require(coordinator.buffered_bytes() == split, "Texture fragment was lost");
        send_model(coordinator, first);
        require(!coordinator.find(position(), start), "Partial texture became visible");
        require(coordinator.feed_textures(std::span(pixels).subspan(split), start),
                "Texture continuation after model was rejected");
        send_sprite(coordinator, first, 1);
        exact(coordinator, first);
    }
    for (std::size_t split = 1; split < model_bytes.size(); ++split) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        require(coordinator.feed_models(std::span(model_bytes).first(split), start),
                "Model fragment was rejected");
        send_sprite(coordinator, first, 1);
        send_sprite(coordinator, first, 0);
        require(!coordinator.find(position(), start), "Texture-only material became visible");
        require(coordinator.feed_models(std::span(model_bytes).subspan(split), start),
                "Model continuation after textures was rejected");
        exact(coordinator, first);
    }
    MaterialCoordinator coordinator;
    begin(coordinator);
    require(coordinator.feed_models(std::span(model_bytes).first(9), start)
                && coordinator.feed_textures(std::span(pixels).first(11), start)
                && coordinator.buffered_bytes() == 20, "Two channel fragments shared storage");
    require(coordinator.feed_models(std::span(model_bytes).subspan(9), start)
                && coordinator.feed_textures(std::span(pixels).subspan(11), start),
            "Interleaved fragments did not complete");
    send_sprite(coordinator, first, 1);
    exact(coordinator, first);
}

static void withdrawal_and_replacement() {
    for (const bool retire : {false, true}) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        const auto first = model();
        send_model(coordinator, first);
        send_sprite(coordinator, first, 0);
        const auto before = coordinator.retained_bytes();
        auto removed = model(2, 0, 0);
        if (retire) {
            removed.kind = render::Kind::retire;
            removed.state.clear();
            removed.pose = {};
        }
        send_model(coordinator, removed);
        require(coordinator.retained_bytes() < before && !coordinator.find(position(), start),
                "Removal retained old model or pixels");
        send_sprite(coordinator, first, 1);
        require(coordinator.pending_captures() == 0 && coordinator.retained_bytes() == 0
                    && coordinator.ready_positions() == 0, "Draining resurrected a removed model");
        require(!coordinator.feed_textures(texture::encode(sprite(first)), start),
                "Drained capture was resurrected by a duplicate");
        cleared(coordinator, CoordinationState::rejected);
    }
    MaterialCoordinator coordinator;
    begin(coordinator);
    const auto first = model();
    send_model(coordinator, first);
    send_sprite(coordinator, first, 0);
    send_sprite(coordinator, first, 1);
    exact(coordinator, first);
    send_model(coordinator, model(2));
    require(!coordinator.find(position(), start) && coordinator.ready_positions() == 0,
            "Replacement exposed previous pixels while waiting for new textures");
    send_sprite(coordinator, model(2), 0);
    send_sprite(coordinator, model(2), 1);
    exact(coordinator, model(2));

    for (const bool early : {false, true}) {
        MaterialCoordinator revoked;
        begin(revoked);
        auto record = sprite(first);
        record.kind = texture::Kind::retire;
        record.width = record.height = 0;
        record.min_u = record.max_u = record.min_v = record.max_v = 0;
        record.argb.clear();
        if (!early) send_model(revoked, first);
        require(revoked.feed_textures(texture::encode(record), start), "Valid withdrawal was rejected");
        if (early) {
            require(revoked.state() == CoordinationState::active
                        && !revoked.find(position(), start), "Unmatched withdrawal bypassed manifest");
            send_model(revoked, first);
        }
        cleared(revoked, CoordinationState::revoked);
    }
}

static void missing_and_invalid() {
    for (const bool early : {false, true}) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        if (!early) send_model(coordinator, model());
        send_sprite(coordinator, model(), 0);
        if (early) {
            send_sprite(coordinator, model(2), 0);
            require(!coordinator.feed_models(render::encode(model()), start),
                    "Missing earlier texture escaped delayed manifest validation");
        } else {
            require(!coordinator.feed_textures(texture::encode(sprite(model(2))), start),
                    "A newer texture batch hid a missing sprite");
        }
        cleared(coordinator, CoordinationState::incomplete);
    }
    MaterialCoordinator skipped;
    begin(skipped);
    send_sprite(skipped, model(2), 0);
    send_sprite(skipped, model(2), 1);
    send_model(skipped, model(1, 0, 0));
    send_model(skipped, model(2));
    exact(skipped, model(2));

    for (int field = 0; field < 5; ++field) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        send_model(coordinator, model());
        auto other = sprite(model());
        switch (field) {
            case 0: --other.identity.epoch; break;
            case 1: ++other.identity.world; break;
            case 2: ++other.identity.camera; break;
            case 3: ++other.identity.frame; break;
            case 4: ++other.identity.capture; break;
        }
        require(!coordinator.feed_textures(texture::encode(other), start),
                "Foreign texture identity was associated with the model");
        cleared(coordinator, field == 4 ? CoordinationState::incomplete : CoordinationState::rejected);
    }
    for (const bool early : {false, true}) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        if (!early) send_model(coordinator, model());
        send_sprite(coordinator, model(), 0);
        require(!coordinator.feed_textures(texture::encode(sprite(model())), start),
                "Duplicate texture was accepted");
        cleared(coordinator, CoordinationState::rejected);
    }
    for (const bool air : {false, true}) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        send_sprite(coordinator, model(), air ? 0 : 3);
        require(!coordinator.feed_models(render::encode(model(1, 0, air ? 0 : 2)), start),
                "Model accepted unexpected quarantined textures");
        cleared(coordinator, CoordinationState::rejected);
    }
    MaterialCoordinator duplicate;
    begin(duplicate);
    send_model(duplicate, model());
    require(!duplicate.feed_models(render::encode(model()), start), "Duplicate model was accepted");
    cleared(duplicate, CoordinationState::rejected);
    MaterialCoordinator gap;
    begin(gap);
    require(!gap.feed_models(render::encode(model(2)), start), "Model capture gap was accepted");
    cleared(gap, CoordinationState::rejected);
}

static void termination_and_timeout() {
    for (const Channel channel : {Channel::models, Channel::textures}) {
        for (const bool partial : {false, true}) {
            MaterialCoordinator coordinator;
            begin(coordinator);
            send_model(coordinator, model());
            send_sprite(coordinator, model(), 0);
            send_sprite(coordinator, model(), 1);
            if (partial) {
                const auto models = render::encode(model(2));
                const auto pixels = texture::encode(sprite(model(2)));
                require(coordinator.feed_models(std::span(models).first(9), start)
                            && coordinator.feed_textures(std::span(pixels).first(11), start),
                        "Fragments before channel end failed");
            }
            coordinator.end_of_stream(channel);
            cleared(coordinator, partial ? CoordinationState::truncated : CoordinationState::ended);
            bool rejected = false;
            try { begin(coordinator); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Ended epoch was reused");
            begin(coordinator, 18);
            const auto next = model(1, 0, 2, 18);
            send_sprite(coordinator, next, 0);
            send_sprite(coordinator, next, 1);
            send_model(coordinator, next);
            exact(coordinator, next);
            coordinator.fail(channel);
            cleared(coordinator, CoordinationState::io_failed);
        }
        MaterialCoordinator incomplete;
        begin(incomplete);
        send_sprite(incomplete, model(), 0);
        incomplete.end_of_stream(channel);
        cleared(incomplete, CoordinationState::incomplete);
        MaterialCoordinator malformed;
        begin(malformed);
        auto bytes = channel == Channel::models ? render::encode(model())
                                               : texture::encode(sprite(model()));
        bytes[0] ^= 1;
        const bool accepted = channel == Channel::models ? malformed.feed_models(bytes, start)
                                                        : malformed.feed_textures(bytes, start);
        require(!accepted, "Malformed channel record was accepted");
        cleared(malformed, CoordinationState::malformed);
    }
    for (const bool texture_first : {false, true}) {
        MaterialCoordinator coordinator(100ms);
        begin(coordinator);
        if (texture_first) send_sprite(coordinator, model(), 0);
        else send_model(coordinator, model());
        if (texture_first) send_model(coordinator, model(), start + 90ms);
        else send_sprite(coordinator, model(), 0, start + 90ms);
        send_model(coordinator, model(2, 1), start + 99ms);
        require(!coordinator.poll(start + 100ms), "Other captures refreshed an incomplete deadline");
        cleared(coordinator, CoordinationState::timed_out);
    }
    MaterialCoordinator partial(100ms);
    begin(partial);
    const auto bytes = texture::encode(sprite(model()));
    require(partial.feed_textures(std::span(bytes).first(1), start)
                && partial.feed_textures(std::span(bytes).subspan(1, 10), start + 99ms),
            "Partial record was rejected before its deadline");
    require(!partial.feed_textures(std::span(bytes).subspan(11), start + 100ms),
            "Fragment progress refreshed its deadline");
    cleared(partial, CoordinationState::timed_out);
    MaterialCoordinator reverse;
    begin(reverse);
    require(!reverse.poll(start - 1ms), "A regressed monotonic clock was accepted");
    cleared(reverse, CoordinationState::rejected);
}

static void capacities_and_ownership() {
    for (const bool textures_only : {false, true}) {
        MaterialCoordinator coordinator;
        begin(coordinator);
        for (std::uint64_t i = 1; i <= 16; ++i) {
            if (textures_only) send_sprite(coordinator, model(i), 0);
            else send_model(coordinator, model(i));
        }
        require(coordinator.pending_captures() == 16, "Pending bound was not exercised");
        const auto next = model(17);
        const bool accepted = textures_only ? coordinator.feed_textures(texture::encode(sprite(next)), start)
                                            : coordinator.feed_models(render::encode(next), start);
        require(!accepted, "Pending capture capacity was exceeded");
        cleared(coordinator, CoordinationState::capacity);
    }
    MaterialCoordinator positions;
    begin(positions);
    for (std::int32_t i = 0; i < 16; ++i) {
        const auto record = model(static_cast<std::uint64_t>(i + 1), i);
        send_model(positions, record);
        send_sprite(positions, record, 0);
        send_sprite(positions, record, 1);
    }
    require(positions.ready_positions() == 16, "Sixteen ready positions did not fit");
    send_model(positions, model(17, 0, 0));
    send_model(positions, model(18, 16));
    send_sprite(positions, model(18, 16), 0);
    send_sprite(positions, model(18, 16), 1);
    require(positions.ready_positions() == 16, "Retirement did not free a position");
    require(!positions.feed_models(render::encode(model(19, 17)), start),
            "Seventeenth model position was admitted");
    cleared(positions, CoordinationState::capacity);

    MaterialCoordinator records;
    begin(records);
    for (std::int32_t i = 0; i < 15; ++i) {
        const auto record = model(static_cast<std::uint64_t>(i + 1), i, 16);
        send_model(records, record);
        for (std::size_t j = 0; j < 16; ++j) send_sprite(records, record, j);
    }
    send_model(records, model(16, 15, 16));
    require(records.retained_records() == MaterialCoordinator::max_records,
            "Exact retained-record limit was not tested");
    require(!records.feed_textures(texture::encode(sprite(model(16, 15, 16))), start),
            "Retained-record budget was exceeded");
    cleared(records, CoordinationState::capacity);

    MaterialCoordinator bytes;
    begin(bytes);
    bool full = false;
    for (std::int32_t i = 0; i < 16 && !full; ++i) {
        auto record = model(static_cast<std::uint64_t>(i + 1), i, 16);
        record.quads.resize(render::max_quads, record.quads.front());
        if (!bytes.feed_models(render::encode(record), start)) { full = true; break; }
        for (std::size_t j = 0; j < 16; ++j) {
            if (!bytes.feed_textures(texture::encode(sprite(record, j, true)), start)) {
                full = true;
                break;
            }
            require(bytes.retained_bytes() <= MaterialCoordinator::max_bytes,
                    "Source byte budget was exceeded");
        }
    }
    require(full, "Maximum-size material records did not exercise byte admission");
    cleared(bytes, CoordinationState::capacity);

    MaterialCoordinator owner;
    begin(owner);
    auto foreign = std::async(std::launch::async, [&owner] {
        try { (void)owner.feed_models({}, start); }
        catch (const std::logic_error&) { return true; }
        return false;
    });
    require(foreign.get() && owner.state() == CoordinationState::active,
            "A foreign thread modified coordination state");
    owner.stop();
    owner.stop();
    cleared(owner, CoordinationState::stopped);
}

int main() {
    try {
        interleavings();
        fragments();
        withdrawal_and_replacement();
        missing_and_invalid();
        termination_and_timeout();
        capacities_and_ownership();
        std::cout << "material-coordinator: passed " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Material coordinator test failed: " << error.what() << '\n';
        return 1;
    }
}
