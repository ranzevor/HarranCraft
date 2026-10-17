#include <harrancraft/wire.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::wire;

static constexpr int diagnostic_close_failure = 86;

[[noreturn]] static void failure(const char* operation, int error) {
    throw std::runtime_error(std::string(operation) + " (errno=" + std::to_string(error) + ")");
}

static void write_bytes(const Bytes& bytes, std::size_t size) {
    if (std::fwrite(bytes.data(), 1, size, stdout) != size) failure("Pipe write failed", errno);
    if (std::fflush(stdout) != 0) failure("Pipe flush failed", errno);
}

static void fragment(const Bytes& bytes) {
    std::size_t offset = 0;
    for (const std::size_t part : {1U, 7U, 31U, 113U, 360U}) {
        const auto size = std::min(part, bytes.size() - offset);
        if (std::fwrite(bytes.data() + offset, 1, size, stdout) != size)
            failure("Fragment write failed", errno);
        if (std::fflush(stdout) != 0) failure("Fragment flush failed", errno);
        offset += size;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

static void diagnostic(const char* value) {
    if (std::fputs(value, stderr) == EOF || std::fputc('\n', stderr) == EOF)
        failure("Diagnostic write failed", errno);
    if (std::fflush(stderr) != 0) failure("Diagnostic flush failed", errno);
}

static void flood(const Bytes& bytes) {
#ifdef _WIN32
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        throw std::runtime_error("Cannot obtain flood output (win32=" + std::to_string(error) + ")");
    }
    if (output == nullptr) throw std::runtime_error("Missing inherited flood output handle");
    SetLastError(ERROR_SUCCESS);
    const DWORD type = GetFileType(output);
    const DWORD type_error = GetLastError();
    if (type != FILE_TYPE_PIPE)
        throw std::runtime_error("Flood output is not a pipe (type=" + std::to_string(type)
                + ", win32=" + std::to_string(type_error) + ")");
#else
    // This scenario observes EPIPE as an explicit receiver-cancellation acknowledgement.
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) failure("Cannot observe closed flood output", errno);
#endif
    diagnostic("ready");
    for (;;) {
#ifdef _WIN32
        DWORD written = 0;
        const DWORD requested = static_cast<DWORD>(bytes.size());
        SetLastError(ERROR_SUCCESS);
        const BOOL succeeded = WriteFile(output, bytes.data(), requested, &written, nullptr);
        const DWORD error = GetLastError();
        if (!succeeded) {
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) break;
            throw std::runtime_error("Flood WriteFile failed (win32=" + std::to_string(error) + ")");
        }
        if (written != requested)
            throw std::runtime_error("Short successful flood write (bytes=" + std::to_string(written)
                    + ", win32=" + std::to_string(error) + ")");
#else
        errno = 0;
        const auto written = std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        const int write_error = errno;
        if (written != bytes.size()) {
            if (write_error == EPIPE) break;
            failure("Flood write failed", write_error);
        }
        errno = 0;
        const int flushed = std::fflush(stdout);
        const int flush_error = errno;
        if (flushed != 0) {
            if (flush_error == EPIPE) break;
            failure("Flood flush failed", flush_error);
        }
#endif
    }
    diagnostic("flood-output-closed");
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    try {
        if (argc != 2) throw std::invalid_argument("Expected a pipe scenario");
        const std::string_view scenario(argv[1]);
        const auto hello = encode(Frame{});
        if (scenario == "fragment") {
            fragment(hello);
            diagnostic("ready");
            Bytes reply{};
            if (std::fread(reply.data(), 1, reply.size(), stdin) != reply.size())
                throw std::runtime_error("Missing echo frame");
            fragment(reply);
        } else if (scenario == "partial") {
            diagnostic("ready");
            write_bytes(hello, 127);
            std::fclose(stdout);
        } else if (scenario == "eof") {
            diagnostic("ready");
            std::fclose(stdout);
        } else if (scenario == "flood" || scenario == "flood-diagnostic" || scenario == "flood-close-failure") {
            flood(hello);
            if (scenario == "flood-diagnostic") diagnostic("unexpected-flood-diagnostic");
        } else if (scenario == "broken") {
            std::fclose(stdin);
            diagnostic("ready");
        } else if (scenario == "idle") {
            write_bytes(hello, hello.size());
            diagnostic("ready");
        } else if (scenario == "trickle") {
            write_bytes(hello, 1);
            diagnostic("ready");
        } else {
            throw std::invalid_argument("Unknown pipe scenario");
        }
        diagnostic("complete");
        if (std::fclose(stderr) != 0) return diagnostic_close_failure;
        if (scenario == "flood-close-failure") return diagnostic_close_failure;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Pipe peer failed: %s\n", error.what());
        return 1;
    }
    // No protocol I/O remains; the owning test must still cancel this non-cooperating child.
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
}
