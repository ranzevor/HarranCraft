#include <harrancraft/render_receiver.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::render;

static void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    try {
        require(argc == 2 && std::string_view(argv[1]) == "--receive",
                "Unexpected render receiver argument");
        RenderReceiver receiver;
        receiver.begin({17, 7, 9, 0, 0}, "minecraft:overworld");
        std::array<std::uint8_t, 1021> buffer{};
        for (;;) {
            std::cin.read(reinterpret_cast<char*>(buffer.data()),
                          static_cast<std::streamsize>(buffer.size()));
            const auto count = std::cin.gcount();
            if (count > 0 && !receiver.feed(std::span<const std::uint8_t>(
                    buffer.data(), static_cast<std::size_t>(count)))) {
                require(receiver.staged_positions() == 0,
                        "Rejected render stream retained staged models");
                std::cerr << "Render stream rejected: "
                          << static_cast<int>(receiver.state()) << '\n';
                return 1;
            }
            if (std::cin.eof()) break;
            require(static_cast<bool>(std::cin), "Render input failed");
        }
        const auto records = receiver.records();
        const auto staged = receiver.staged_positions();
        receiver.end_of_stream();
        require(receiver.state() == ReceiveState::ended
                    && receiver.staged_positions() == 0
                    && receiver.epoch() == 0,
                "Render stream EOF did not retire staged models");
        std::cout << "{\"records\":" << records
                  << ",\"staged_before_close\":" << staged << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Render receive failed: " << error.what() << '\n';
        return 1;
    }
}
