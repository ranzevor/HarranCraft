#include <harrancraft/wire.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::wire;

static void write_bytes(const Bytes& bytes, std::size_t size) {
    if (std::fwrite(bytes.data(), 1, size, stdout) != size || std::fflush(stdout) != 0)
        throw std::runtime_error("Pipe write failed");
}

static void fragment(const Bytes& bytes) {
    std::size_t offset = 0;
    for (const std::size_t part : {1U, 7U, 31U, 113U, 360U}) {
        const auto size = std::min(part, bytes.size() - offset);
        if (std::fwrite(bytes.data() + offset, 1, size, stdout) != size || std::fflush(stdout) != 0)
            throw std::runtime_error("Fragment write failed");
        offset += size;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

static void ready() {
    std::fputs("ready\n", stderr);
    std::fflush(stderr);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        if (argc != 2) throw std::invalid_argument("Expected a pipe scenario");
        const std::string_view scenario(argv[1]);
        const auto hello = encode(Frame{});
        if (scenario == "fragment") {
            fragment(hello);
            ready();
            Bytes reply{};
            if (std::fread(reply.data(), 1, reply.size(), stdin) != reply.size())
                throw std::runtime_error("Missing echo frame");
            fragment(reply);
        } else if (scenario == "partial") {
            ready();
            write_bytes(hello, 127);
            std::fclose(stdout);
        } else if (scenario == "eof") {
            ready();
            std::fclose(stdout);
        } else if (scenario == "flood") {
            ready();
            for (int i = 0; i < 1024; ++i) write_bytes(hello, hello.size());
        } else if (scenario == "broken") {
            std::fclose(stdin);
            ready();
        } else if (scenario == "idle") {
            write_bytes(hello, hello.size());
            ready();
        } else if (scenario == "trickle") {
            write_bytes(hello, 1);
            ready();
        } else {
            throw std::invalid_argument("Unknown pipe scenario");
        }
        // The owning test must cancel a deliberately non-cooperating child.
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Pipe peer failed: %s\n", error.what());
        return 1;
    }
}
