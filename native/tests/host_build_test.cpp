#include <harrancraft/host_build.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace harrancraft::host;

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

static ImageIdentity identity(std::uint8_t value) {
    ImageIdentity image{};
    image.sha256[0] = value;
    image.file_bytes = 4096;
    image.machine = 0x8664;
    return image;
}

static BuildImage build() {
    return {
        identity(1),
        {{"engine_x64_rwdi.dll", identity(2)}, {"gamedll_x64_rwdi.dll", identity(3)}},
    };
}

static Evidence evidence() {
    return {Mode::single_player, Lifecycle::playing, ReadPhase::verified_engine_read, 17};
}

static Matrix4 matrix() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

static CameraSample frame(std::uint64_t epoch, std::uint64_t index) {
    return {{epoch, 17, 9, index, index}, matrix(), matrix(), {2, 3, 4}};
}

static void invalid_profile(const BuildImage& profile) {
    bool rejected = false;
    try {
        BuildSession session({profile});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "Invalid host build profile was accepted");
}

int main() {
    try {
        const auto approved = build();
        auto reordered = approved;
        std::reverse(reordered.modules.begin(), reordered.modules.end());
        for (auto& module : reordered.modules) {
            std::transform(module.basename.begin(), module.basename.end(),
                           module.basename.begin(), [](char value) {
                               return value >= 'a' && value <= 'z'
                                   ? static_cast<char>(value - ('a' - 'A')) : value;
                           });
        }
        const auto normalized = normalize_build(reordered);
        require(normalized && *normalized == *normalize_build(approved),
                "Module ordering and case changed build identity");

        auto duplicate = approved;
        duplicate.modules.push_back(duplicate.modules.front());
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.modules[1].basename = "ENGINE_X64_RWDI.DLL";
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.modules[0].basename = "../engine_x64_rwdi.dll";
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.modules[0].image = {};
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.modules.clear();
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.executable = {};
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.modules.front().basename = "module.dll/evil";
        invalid_profile(duplicate);
        duplicate = approved;
        duplicate.modules.clear();
        for (int index = 0; index < 9; ++index) {
            duplicate.modules.push_back(
                {"module" + std::to_string(index) + ".dll",
                 identity(static_cast<std::uint8_t>(index + 1))});
        }
        invalid_profile(duplicate);

        bool rejected_duplicate_profile = false;
        try {
            BuildSession invalid({approved, reordered});
        } catch (const std::invalid_argument&) {
            rejected_duplicate_profile = true;
        }
        require(rejected_duplicate_profile, "Duplicate build profile was accepted");
        std::vector<BuildImage> too_many(17, approved);
        bool rejected_profile_count = false;
        try {
            BuildSession invalid(std::move(too_many));
        } catch (const std::invalid_argument&) {
            rejected_profile_count = true;
        }
        require(rejected_profile_count, "Profile count limit was not enforced");

        BuildSession empty({});
        require(!empty.begin(approved, evidence()), "Empty allowlist accepted an image");

        BuildSession gate({approved});
        auto mismatched = approved;
        mismatched.modules[0].image.sha256[0] ^= 1;
        require(!gate.begin(mismatched, evidence()), "Different DLL was accepted");
        mismatched = approved;
        mismatched.modules.erase(mismatched.modules.begin());
        require(!gate.begin(mismatched, evidence()), "Missing DLL was accepted");
        mismatched = approved;
        mismatched.executable.sha256[0] ^= 1;
        require(!gate.begin(mismatched, evidence()), "Different executable was accepted");
        require(!gate.begin(std::nullopt, evidence()), "Missing build observation was accepted");

        auto invalid_mode = evidence();
        invalid_mode.mode = Mode::cooperative;
        require(!gate.begin(approved, invalid_mode), "Cooperative context was accepted");
        invalid_mode.mode = Mode::invasion;
        require(!gate.begin(approved, invalid_mode), "Invasion context was accepted");
        invalid_mode = evidence();
        invalid_mode.phase = ReadPhase::window_or_present;
        require(!gate.begin(approved, invalid_mode), "Present was accepted as a read phase");

        const auto first = gate.begin(reordered, evidence());
        require(first && *first == 1, "Expected first accepted build generation");
        const auto camera = frame(*first, 1);
        require(gate.capture(evidence(), camera), "Camera capture failed");
        require(gate.snapshot(evidence(), camera.key) == camera, "Camera snapshot missing");
        require(gate.refresh(approved), "Equivalent build observation was rejected");
        require(gate.snapshot(evidence(), camera.key) == camera,
                "Equal build refresh invalidated camera state");

        auto cross_thread = std::async(std::launch::async, [&gate, approved] {
            try {
                (void)gate.refresh(approved);
            } catch (const std::logic_error&) {
                return true;
            }
            return false;
        });
        require(cross_thread.get(), "Cross-thread host revalidation was allowed");
        require(gate.snapshot(evidence(), camera.key) == camera,
                "Cross-thread refusal modified the observation");

        mismatched = approved;
        mismatched.modules[1].image.sha256[0] ^= 1;
        require(!gate.refresh(mismatched), "Module replacement did not revoke the session");
        require(!gate.snapshot(evidence(), camera.key), "Retired camera remained available");
        require(!gate.refresh(approved), "Revoked session resumed without begin");
        require(!gate.capture(evidence(), camera), "Old capture was replayed");

        const auto second = gate.begin(approved, evidence());
        require(second && *second > *first, "Reattachment reused the old epoch");
        require(!gate.capture(evidence(), camera), "Old-epoch camera was accepted");
        const auto new_camera = frame(*second, 1);
        require(gate.capture(evidence(), new_camera), "New session capture failed");
        require(!gate.refresh(std::nullopt), "Missing refresh result did not revoke");
        require(!gate.snapshot(evidence(), new_camera.key), "Failed refresh retained a camera");

        const auto third = gate.begin(approved, evidence());
        require(third && *third > *second, "New observation session did not advance epoch");
        const auto latest = frame(*third, 1);
        require(gate.capture(evidence(), latest), "New observation capture failed");
        invalid_mode = evidence();
        invalid_mode.lifecycle = Lifecycle::shutting_down;
        require(!gate.snapshot(invalid_mode, latest.key),
                "Host shutdown did not revoke the observation");
        require(!gate.snapshot(evidence(), latest.key),
                "Host reactivation restored a stale frame");

        gate.stop();
        gate.stop();
        require(!gate.begin(std::nullopt, evidence()), "Stopped session accepted missing image");
        std::cout << "host-build: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
