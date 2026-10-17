#pragma once

#include <harrancraft/host_observation.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace harrancraft::host {

inline constexpr std::size_t max_build_profiles = 16;
inline constexpr std::size_t max_build_modules = 8;

struct ModuleImage {
    std::string basename;
    ImageIdentity image;
    bool operator==(const ModuleImage&) const = default;
};

struct BuildImage {
    ImageIdentity executable;
    std::vector<ModuleImage> modules;
    bool operator==(const BuildImage&) const = default;
};

inline bool valid_module_basename(std::string_view name) {
    if (name.size() < 5 || name.size() > 64
        || name.substr(name.size() - 4) != ".dll") {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char value) {
        return (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9')
            || value == '_' || value == '-' || value == '.';
    });
}

// Windows module names are case-insensitive; duplicate names cannot identify
// distinct expected images. A complete build has exactly the required modules.
inline std::optional<BuildImage> normalize_build(BuildImage observed) {
    if (!valid_image(observed.executable)
        || observed.modules.empty() || observed.modules.size() > max_build_modules) {
        return std::nullopt;
    }
    for (auto& module : observed.modules) {
        for (char& value : module.basename) {
            if (value >= 'A' && value <= 'Z') {
                value = static_cast<char>(value + ('a' - 'A'));
            }
        }
        if (!valid_module_basename(module.basename) || !valid_image(module.image)) {
            return std::nullopt;
        }
    }
    std::sort(observed.modules.begin(), observed.modules.end(),
              [](const ModuleImage& left, const ModuleImage& right) {
                  return left.basename < right.basename;
              });
    for (std::size_t index = 1; index < observed.modules.size(); ++index) {
        if (observed.modules[index - 1].basename == observed.modules[index].basename) {
            return std::nullopt;
        }
    }
    return observed;
}

inline std::vector<BuildImage> validate_profiles(std::vector<BuildImage> profiles) {
    if (profiles.size() > max_build_profiles) {
        throw std::invalid_argument("Too many host build profiles");
    }
    std::vector<BuildImage> accepted;
    accepted.reserve(profiles.size());
    for (auto& profile : profiles) {
        auto canonical = normalize_build(std::move(profile));
        if (!canonical || std::find(accepted.begin(), accepted.end(), *canonical)
                              != accepted.end()) {
            throw std::invalid_argument("Invalid or duplicate host build profile");
        }
        accepted.push_back(std::move(*canonical));
    }
    return accepted;
}

inline std::vector<ImageIdentity> profile_executables(
    const std::vector<BuildImage>& profiles) {
    std::vector<ImageIdentity> result;
    result.reserve(profiles.size());
    for (const auto& profile : profiles) result.push_back(profile.executable);
    return result;
}

// Probe before or between sessions: hashing performs blocking file I/O and
// cannot execute in a renderer, game mutation callback or loader lock.
class BuildSession {
public:
    explicit BuildSession(std::vector<BuildImage> profiles)
        : profiles_(validate_profiles(std::move(profiles))),
          observer_(profile_executables(profiles_)),
          owner_(std::this_thread::get_id()) {}

    std::optional<std::uint64_t> begin(
        const std::optional<BuildImage>& observed, const Evidence& evidence) {
        check_owner();
        stop();
        if (!observed) return std::nullopt;
        auto canonical = normalize_build(*observed);
        if (!canonical || std::find(profiles_.begin(), profiles_.end(), *canonical)
                              == profiles_.end()) {
            return std::nullopt;
        }
        const auto epoch = observer_.begin(canonical->executable, evidence);
        if (epoch) {
            active_ = std::move(*canonical);
            context_ = evidence;
        }
        return epoch;
    }

    bool refresh(const std::optional<BuildImage>& observed) {
        check_owner();
        if (!active_) return false;
        auto canonical = observed ? normalize_build(*observed) : std::nullopt;
        if (!canonical || *canonical != *active_) {
            stop();
            return false;
        }
        return true;
    }

    bool capture(const Evidence& evidence, const CameraSample& sample) {
        check_owner();
        if (!valid_context(evidence)) return false;
        return observer_.capture(evidence, sample);
    }

    std::optional<CameraSample> snapshot(const Evidence& evidence, const CaptureKey& key) {
        check_owner();
        if (!valid_context(evidence)) return std::nullopt;
        return observer_.snapshot(evidence, key);
    }

    void stop() {
        check_owner();
        observer_.stop();
        active_.reset();
        context_.reset();
    }

private:
    void check_owner() const {
        if (owner_ != std::this_thread::get_id()) {
            throw std::logic_error("Host build session accessed from another thread");
        }
    }

    bool valid_context(const Evidence& evidence) {
        if (!active_ || !context_ || evidence != *context_ || !readable(evidence)) {
            stop();
            return false;
        }
        return true;
    }

    const std::vector<BuildImage> profiles_;
    Observer observer_;
    const std::thread::id owner_;
    std::optional<BuildImage> active_;
    std::optional<Evidence> context_;
};

} // namespace harrancraft::host
