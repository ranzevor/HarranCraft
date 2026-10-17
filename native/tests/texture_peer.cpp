#include <harrancraft/texture_wire.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::texture;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        require(argc == 2 && std::string_view(argv[1]) == "--echo",
                "Unknown texture peer scenario");
        std::size_t records = 0;
        for (;;) {
            std::array<std::uint8_t, header_bytes> header{};
            std::cin.read(reinterpret_cast<char*>(header.data()),
                          static_cast<std::streamsize>(header.size()));
            const auto received = std::cin.gcount();
            if (received == 0 && std::cin.eof()) break;
            require(received == static_cast<std::streamsize>(header_bytes),
                    "Truncated source texture header");
            const auto declared = get(header, 8, 4);
            require(declared >= header_bytes && declared <= max_bytes
                    && (declared - header_bytes) % 4 == 0,
                    "Invalid texture declared length");
            Bytes bytes(static_cast<std::size_t>(declared), 0);
            std::copy(header.begin(), header.end(), bytes.begin());
            const auto tail = bytes.size() - header_bytes;
            if (tail != 0) {
                std::cin.read(reinterpret_cast<char*>(bytes.data() + header_bytes),
                              static_cast<std::streamsize>(tail));
                require(std::cin.gcount() == static_cast<std::streamsize>(tail),
                        "Truncated sprite image payload");
            }
            const auto record = decode(bytes);
            require(encode(record) == bytes, "Noncanonical source texture bytes");
            std::cout.write(reinterpret_cast<const char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
            require(static_cast<bool>(std::cout), "Texture process output failed");
            ++records;
            require(records <= 8, "Too many sprite records");
        }
        require(records != 0, "No source texture records received");
        std::cout.flush();
        require(static_cast<bool>(std::cout), "Sprite transport flush failed");
        std::cerr << "texture-peer: passed " << records << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Texture peer failed: " << error.what() << '\n';
        return 1;
    }
}
