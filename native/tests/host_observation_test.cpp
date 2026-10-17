#include <harrancraft/host_observation.hpp>

#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace harrancraft::host;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Matrix4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

static ImageIdentity image(std::uint8_t tag) {
    ImageIdentity value;
    value.sha256[0] = tag;
    value.file_bytes = 4096;
    value.machine = 0x8664;
    return value;
}

static Evidence context(std::uint64_t world = 17) {
    return {Mode::single_player, Lifecycle::playing, ReadPhase::verified_engine_read, world};
}

static CameraSample sample(std::uint64_t epoch, std::uint64_t frame = 1) {
    return {{epoch, 17, 9, frame, frame}, identity(), identity(), {2, 3, 4}};
}

int main() {
    try {
        require(!valid_image(image(0)), "Zero digest was accepted");
        require(!Observer({}).begin(image(1), context()), "Unconfigured image was accepted");
        bool invalid_config = false;
        try { Observer ignored({image(0)}); } catch (const std::invalid_argument&) {
            invalid_config = true;
        }
        require(invalid_config, "Invalid allowlist was accepted");
        Observer gate({image(1)});
        require(!gate.begin(image(2), context()), "Unknown build was accepted");
        auto invalid = context();
        invalid.mode = Mode::cooperative;
        require(!gate.begin(image(1), invalid), "Cooperative session was accepted");
        invalid.mode = Mode::invasion;
        require(!gate.begin(image(1), invalid), "Invasion session was accepted");
        invalid = context();
        invalid.lifecycle = Lifecycle::loading;
        require(!gate.begin(image(1), invalid), "Loading session was accepted");
        invalid.lifecycle = Lifecycle::shutting_down;
        require(!gate.begin(image(1), invalid), "Closing session was accepted");
        invalid = context();
        invalid.phase = ReadPhase::window_or_present;
        require(!gate.begin(image(1), invalid), "Present was treated as an engine-read phase");
        invalid.phase = ReadPhase::unverified;
        require(!gate.begin(image(1), invalid), "Unknown phase was accepted");
        require(!gate.begin(image(1), context(0)), "World-less session was accepted");
        const auto epoch = gate.begin(image(1), context());
        require(epoch && *epoch == 1, "Expected initial session generation");
        auto first = sample(*epoch);
        require(gate.capture(context(), first), "Valid camera capture was rejected");
        require(gate.snapshot(context(), first.key) == first, "Camera snapshot lost data");
        require(!gate.capture(context(), first), "Duplicate frame was accepted");
        auto out_of_order = sample(*epoch, 2);
        out_of_order.key.capture = 1;
        require(!gate.capture(context(), out_of_order), "Out-of-order capture was accepted");
        auto old_epoch = sample(*epoch + 1, 2);
        require(!gate.capture(context(), old_epoch), "Foreign epoch was accepted");
        auto wrong_world = sample(*epoch, 2);
        wrong_world.key.world = 99;
        require(!gate.capture(context(), wrong_world), "Foreign world was accepted");
        auto broken = sample(*epoch, 2);
        broken.view[4] = std::numeric_limits<double>::quiet_NaN();
        require(!gate.capture(context(), broken), "Non-finite camera was accepted");
        auto wrong_key = first.key;
        wrong_key.camera += 1;
        require(!gate.snapshot(context(), wrong_key), "Foreign camera was paired");
        wrong_key = first.key;
        wrong_key.frame += 1;
        require(!gate.snapshot(context(), wrong_key), "Foreign frame was paired");
        wrong_key = first.key;
        wrong_key.capture += 1;
        require(!gate.snapshot(context(), wrong_key), "Foreign capture identity was paired");
        require(gate.snapshot(context(), first.key) == first, "Invalid frames altered the latest capture");
        auto next = sample(*epoch, 2);
        next.key.camera = 10;
        require(gate.capture(context(), next), "Camera transition was rejected");
        require(!gate.snapshot(context(), first.key), "Old camera remained published");
        require(gate.snapshot(context(), next.key) == next, "New camera was not published");

        auto model_to_host = identity();
        model_to_host[12] = 5;
        model_to_host[13] = -2;
        model_to_host[14] = 3;
        const auto projected = project_clip(next, model_to_host, {1, 2, 3});
        require(projected == Vector4{6, 0, 6, 1}, "Guest-to-host clip transform was incorrect");
        model_to_host[3] = 0.5;
        require(!project_clip(next, model_to_host, {1, 2, 3}), "Projective world mapping was accepted");
        model_to_host[3] = 0;
        model_to_host[12] = std::numeric_limits<double>::infinity();
        require(!project_clip(next, model_to_host, {1, 2, 3}), "Infinite mapping was accepted");
        model_to_host = identity();
        require(!project_clip(broken, model_to_host, {1, 2, 3}), "Invalid camera was projected");

        auto changed = context(18);
        require(!gate.snapshot(changed, next.key), "World switch retained a snapshot");
        require(!gate.capture(context(), next), "Revoked session accepted a capture");
        const auto second = gate.begin(image(1), context());
        require(second && *second == 2, "Generation reused across a world switch");
        auto second_frame = sample(*second);
        require(gate.capture(context(), second_frame), "Fresh session capture failed");
        invalid = context();
        invalid.mode = Mode::cooperative;
        require(!gate.snapshot(invalid, second_frame.key), "Forbidden mode retained a snapshot");
        require(!gate.snapshot(context(), second_frame.key), "Old mode revocation was undone");
        const auto third = gate.begin(image(1), context());
        require(third && *third == 3, "Generation not advanced after revocation");
        auto final_frame = sample(*third);
        require(gate.capture(context(), final_frame), "Third session capture failed");
        auto other_thread = std::async(std::launch::async, [&gate, final_frame] {
            try { (void)gate.snapshot(context(), final_frame.key); }
            catch (const std::logic_error&) { return true; }
            return false;
        });
        require(other_thread.get(), "Cross-thread host access was accepted");
        require(gate.snapshot(context(), final_frame.key) == final_frame,
                "Thread refusal damaged the owner snapshot");
        gate.stop();
        gate.stop();
        require(!gate.snapshot(context(), final_frame.key), "Stopped snapshot remained available");
        require(!gate.begin(image(2), context()), "Unknown build reopened the session");
        require(!gate.capture(context(), final_frame), "Old generation revived after failed begin");
        std::cout << "host-observation: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
