#include <harrancraft/geometry_wire.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::geometry;

static void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        require(argc == 2 && std::string_view(argv[1]) == "--echo",
                "Unknown geometry scenario");
        std::size_t received = 0;
        for (;;) {
            std::array<std::uint8_t, header_bytes> header{};
            std::cin.read(reinterpret_cast<char*>(header.data()),
                          static_cast<std::streamsize>(header.size()));
            const auto size = std::cin.gcount();
            if (size == 0 && std::cin.eof()) break;
            require(size == static_cast<std::streamsize>(header.size()), "Truncated geometry header");
            const auto total = get(header, 8, 4);
            require(total >= header_bytes && total <= max_bytes, "Invalid geometry record size");
            Bytes bytes(static_cast<std::size_t>(total), 0);
            std::copy(header.begin(), header.end(), bytes.begin());
            const auto rest = bytes.size() - header_bytes;
            if (rest != 0) {
                std::cin.read(reinterpret_cast<char*>(bytes.data() + header_bytes),
                              static_cast<std::streamsize>(rest));
                require(std::cin.gcount() == static_cast<std::streamsize>(rest),
                        "Truncated geometry box data");
            }
            const auto frame = decode(bytes);
            const auto encoded = encode(frame);
            require(encoded == bytes, "Noncanonical geometry representation");
            std::cout.write(reinterpret_cast<const char*>(encoded.data()),
                            static_cast<std::streamsize>(encoded.size()));
            require(static_cast<bool>(std::cout), "Geometry write failed");
            ++received;
            require(received <= 32, "Too many geometry records");
        }
        require(received != 0, "Empty geometry peer exchange");
        std::cout.flush();
        require(static_cast<bool>(std::cout), "Geometry stream flush failed");
        std::cerr << "geometry-peer: passed " << received << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Geometry peer failed: " << error.what() << '\n';
        return 1;
    }
}
