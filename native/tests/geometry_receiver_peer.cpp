#include <harrancraft/geometry_receiver.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::geometry;

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    try {
        require(argc == 2 && std::string_view(argv[1]) == "--receive",
                "Unknown native receiver scenario");
        GeometryReceiver receiver;
        CoordinateMap coordinates;
        coordinates.dimension = "minecraft:overworld";
        receiver.begin(17, coordinates);
        std::array<std::uint8_t, 257> buffer{};
        for (;;) {
            std::cin.read(reinterpret_cast<char*>(buffer.data()),
                          static_cast<std::streamsize>(buffer.size()));
            const auto count = std::cin.gcount();
            if (count > 0 && !receiver.feed(std::span<const std::uint8_t>(
                    buffer.data(), static_cast<std::size_t>(count)))) {
                require(receiver.staged_positions() == 0,
                        "Rejected native geometry retained candidates");
                std::cerr << "Geometry rejected: " << static_cast<int>(receiver.state()) << '\n';
                return 1;
            }
            if (std::cin.eof()) break;
            require(static_cast<bool>(std::cin), "Geometry input stream failed");
        }
        const auto records = receiver.records();
        const auto staged = receiver.staged_positions();
        receiver.end_of_stream();
        require(receiver.state() == ReceiverState::ended
                    && receiver.staged_positions() == 0
                    && receiver.epoch() == 0, "Native EOF did not clear candidates");
        std::cout << "{\"records\":" << records
                  << ",\"staged_before_close\":" << staged << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native geometry receive failed: " << error.what() << '\n';
        return 1;
    }
}
