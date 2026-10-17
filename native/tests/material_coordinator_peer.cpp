#include <harrancraft/material_coordinator.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
using harrancraft::material::Channel;
using harrancraft::material::CoordinationState;
using harrancraft::material::MaterialCoordinator;
constexpr std::size_t read_bytes = 1021;
constexpr std::size_t max_input_bytes = 256 * 1024;
constexpr std::size_t max_reports = 256;

std::uint64_t number(std::string_view text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("Invalid coordinator peer number");
    }
    return value;
}

std::string_view name(CoordinationState state) {
    switch (state) {
    case CoordinationState::idle: return "idle";
    case CoordinationState::active: return "active";
    case CoordinationState::ended: return "ended";
    case CoordinationState::truncated: return "truncated";
    case CoordinationState::incomplete: return "incomplete";
    case CoordinationState::malformed: return "malformed";
    case CoordinationState::rejected: return "rejected";
    case CoordinationState::capacity: return "capacity";
    case CoordinationState::timed_out: return "timed_out";
    case CoordinationState::revoked: return "revoked";
    case CoordinationState::io_failed: return "io_failed";
    case CoordinationState::stopped: return "stopped";
    }
    throw std::logic_error("Unknown material coordinator state");
}

std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto value : bytes) {
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 15]);
    }
    return result;
}

// Only these small fixtures are reported; the production coordinator has separate limits.
std::string material(MaterialCoordinator& coordinator, std::int32_t x,
                     MaterialCoordinator::TimePoint now) {
    const auto found = coordinator.find({"minecraft:overworld", x, 80, 0}, now);
    if (!found) return "-";
    if (found->model.quads.size() > 2 || found->sprites.size() > 2) {
        throw std::runtime_error("Coordinator peer report exceeds fixture bound");
    }
    std::string result = hex(harrancraft::render::encode(found->model));
    for (const auto& [key, sprite] : found->sprites) {
        (void)key;
        if (sprite.argb.size() > 4) {
            throw std::runtime_error("Coordinator peer pixel report exceeds fixture bound");
        }
        result += ':';
        result += hex(harrancraft::texture::encode(sprite));
    }
    return result;
}

enum class ReadState { waiting, bytes, ended, failed };
struct ReadResult { ReadState state = ReadState::waiting; std::size_t count = 0; };

// stdin and the named texture endpoint are independent OS pipes with one read owner.
class Inputs {
public:
    Inputs() {
        try {
#ifdef _WIN32
            model_ = GetStdHandle(STD_INPUT_HANDLE);
            if (model_ == nullptr || model_ == INVALID_HANDLE_VALUE
                || GetFileType(model_) != FILE_TYPE_PIPE) {
                throw std::runtime_error("Coordinator peer stdin is not a pipe");
            }
            path_ = "\\\\.\\pipe\\HarranCraft-material-" + std::to_string(GetCurrentProcessId())
                + '-' + std::to_string(MaterialCoordinator::Clock::now().time_since_epoch().count());
            // Default DACL permits write access to the creator, administrators and LocalSystem.
            texture_ = CreateNamedPipeA(path_.c_str(),
                PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, 4096, 4096, 0, nullptr);
            if (texture_ == INVALID_HANDLE_VALUE) {
                throw std::runtime_error("Unable to create private texture pipe");
            }
            const auto initial = connect();
            if (initial == ReadState::failed || initial == ReadState::ended) {
                throw std::runtime_error("Unable to listen on private texture pipe");
            }
#else
            std::array<char, 40> pattern{};
            constexpr std::string_view prefix = "/tmp/harrancraft-material-XXXXXX";
            std::copy(prefix.begin(), prefix.end(), pattern.begin());
            const char* directory = ::mkdtemp(pattern.data());
            if (directory == nullptr) throw std::runtime_error("Unable to create private pipe directory");
            directory_ = directory;
            path_ = directory_ + "/textures";
            if (::mkfifo(path_.c_str(), S_IRUSR | S_IWUSR) != 0) {
                throw std::runtime_error("Unable to create private texture FIFO");
            }
            texture_ = ::open(path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            const int flags = ::fcntl(STDIN_FILENO, F_GETFL);
            if (texture_ < 0 || flags < 0
                || ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
                throw std::runtime_error("Unable to configure nonblocking material inputs");
            }
            model_ = STDIN_FILENO;
#endif
        } catch (...) {
            if (!close()) std::cerr << "Coordinator peer setup cleanup failed\n";
            throw;
        }
    }

    Inputs(const Inputs&) = delete;
    Inputs& operator=(const Inputs&) = delete;
    ~Inputs() {
        if (!close()) std::cerr << "Coordinator peer endpoint cleanup failed\n";
    }

    const std::string& path() const { return path_; }

    static std::uint64_t process_id() {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return static_cast<std::uint64_t>(::getpid());
#endif
    }

    ReadResult read(Channel channel, std::span<std::uint8_t> output) {
#ifdef _WIN32
        if (channel == Channel::textures && !connected_) {
            const auto result = connect();
            if (result != ReadState::bytes) return {result, 0};
        }
        const HANDLE pipe = channel == Channel::models ? model_ : texture_;
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            const auto error = GetLastError();
            return {error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA
                ? ReadState::ended : ReadState::failed, 0};
        }
        if (available == 0) return {};
        const auto count = static_cast<DWORD>(std::min(output.size(),
            static_cast<std::size_t>(available)));
        DWORD received = 0;
        if (!ReadFile(pipe, output.data(), count, &received, nullptr)) {
            const auto error = GetLastError();
            return {error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA
                ? ReadState::ended : ReadState::failed, 0};
        }
        return {received == 0 ? ReadState::ended : ReadState::bytes,
                static_cast<std::size_t>(received)};
#else
        const int pipe = channel == Channel::models ? model_ : texture_;
        pollfd descriptor{pipe, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 0);
        if (ready < 0) return {errno == EINTR ? ReadState::waiting : ReadState::failed, 0};
        if (ready == 0) return {};
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) return {ReadState::failed, 0};
        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) return {};
        const auto received = ::read(pipe, output.data(), output.size());
        if (received < 0) {
            return {errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR
                ? ReadState::waiting : ReadState::failed, 0};
        }
        return {received == 0 ? ReadState::ended : ReadState::bytes,
                static_cast<std::size_t>(received)};
#endif
    }

    static void pause() {
#ifdef _WIN32
        Sleep(2);
#else
        if (::poll(nullptr, 0, 2) < 0 && errno != EINTR) {
            throw std::runtime_error("Coordinator peer polling failed");
        }
#endif
    }

    bool close() noexcept {
        bool ok = true;
#ifdef _WIN32
        if (texture_ != INVALID_HANDLE_VALUE) {
            ok = CloseHandle(texture_) != 0 && ok;
            texture_ = INVALID_HANDLE_VALUE;
        }
        if (model_ != INVALID_HANDLE_VALUE && model_ != nullptr) {
            ok = CloseHandle(model_) != 0 && ok;
            model_ = INVALID_HANDLE_VALUE;
        }
#else
        if (texture_ >= 0) {
            ok = ::close(texture_) == 0 && ok;
            texture_ = -1;
        }
        if (model_ >= 0) {
            ok = ::close(model_) == 0 && ok;
            model_ = -1;
        }
        if (!path_.empty()) {
            ok = (::unlink(path_.c_str()) == 0 || errno == ENOENT) && ok;
            path_.clear();
        }
        if (!directory_.empty()) {
            ok = ::rmdir(directory_.c_str()) == 0 && ok;
            directory_.clear();
        }
#endif
        return ok;
    }

private:
    std::string path_;
#ifdef _WIN32
    ReadState connect() {
        if (ConnectNamedPipe(texture_, nullptr)) {
            // PIPE_NOWAIT can report a successful transition into listening state.
            return ReadState::waiting;
        }
        const auto error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            connected_ = true;
            return ReadState::bytes;
        }
        if (error == ERROR_PIPE_LISTENING) return ReadState::waiting;
        if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE) return ReadState::ended;
        return ReadState::failed;
    }
    HANDLE model_ = INVALID_HANDLE_VALUE, texture_ = INVALID_HANDLE_VALUE;
    bool connected_ = false;
#else
    std::string directory_;
    int model_ = -1, texture_ = -1;
#endif
};

void report(MaterialCoordinator& coordinator, std::size_t& sequence, std::string_view reason,
            std::size_t model_bytes, std::size_t texture_bytes) {
    if (++sequence > max_reports) throw std::runtime_error("Coordinator peer report count exceeded");
    const auto now = MaterialCoordinator::Clock::now();
    const std::array<std::string, 3> slots{
        material(coordinator, 0, now), material(coordinator, 1, now), material(coordinator, 2, now)
    };
    std::cout << "STATE " << sequence << ' ' << reason << ' ' << model_bytes << ' '
              << texture_bytes << ' ' << name(coordinator.state()) << ' ' << coordinator.epoch()
              << ' ' << coordinator.models_received() << ' ' << coordinator.textures_received()
              << ' ' << coordinator.pending_captures() << ' ' << coordinator.ready_positions()
              << ' ' << coordinator.retained_bytes() << ' ' << coordinator.retained_records()
              << ' ' << coordinator.buffered_bytes();
    for (const auto& slot : slots) std::cout << ' ' << slot;
    std::cout << '\n' << std::flush;
    if (!std::cout) throw std::runtime_error("Coordinator peer report output failed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string_view(argv[1]) != "--coordinate") {
            throw std::invalid_argument("Expected --coordinate epoch timeout-ms");
        }
        const auto epoch = number(argv[2]);
        const auto timeout = number(argv[3]);
        if (timeout == 0 || timeout > 5000) throw std::invalid_argument("Invalid peer timeout");
        MaterialCoordinator coordinator{std::chrono::milliseconds(timeout)};
        coordinator.begin(epoch, 7, 9, "minecraft:overworld");
        Inputs inputs;
        std::cout << "READY " << Inputs::process_id() << ' ' << inputs.path() << '\n' << std::flush;
        if (!std::cout) throw std::runtime_error("Coordinator peer handshake failed");
        std::array<std::uint8_t, read_bytes> buffer{};
        std::size_t model_bytes = 0, texture_bytes = 0, sequence = 0;
        const auto deadline = MaterialCoordinator::Clock::now() + std::chrono::seconds(20);
        report(coordinator, sequence, "begin", model_bytes, texture_bytes);
        while (coordinator.state() == CoordinationState::active) {
            if (MaterialCoordinator::Clock::now() >= deadline) {
                coordinator.stop();
                throw std::runtime_error("Coordinator peer exceeded its lifetime");
            }
            bool progressed = false;
            for (const auto channel : {Channel::models, Channel::textures}) {
                if (coordinator.state() != CoordinationState::active) break;
                const auto received = inputs.read(channel, buffer);
                if (received.state == ReadState::waiting) continue;
                progressed = true;
                if (received.state == ReadState::ended) coordinator.end_of_stream(channel);
                else if (received.state == ReadState::failed) coordinator.fail(channel);
                else {
                    auto& total = channel == Channel::models ? model_bytes : texture_bytes;
                    if (received.count > max_input_bytes - total) {
                        throw std::runtime_error("Coordinator peer input exceeds fixture bound");
                    }
                    total += received.count;
                    const auto bytes = std::span<const std::uint8_t>(buffer.data(), received.count);
                    if (channel == Channel::models) coordinator.feed_models(bytes);
                    else coordinator.feed_textures(bytes);
                }
                report(coordinator, sequence, channel == Channel::models ? "models" : "textures",
                       model_bytes, texture_bytes);
            }
            if (coordinator.state() == CoordinationState::active && !coordinator.poll()) {
                report(coordinator, sequence, "poll", model_bytes, texture_bytes);
            }
            if (!progressed && coordinator.state() == CoordinationState::active) Inputs::pause();
        }
        if (!inputs.close()) throw std::runtime_error("Coordinator peer retained an owned endpoint");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
