#include <harrancraft/render_wire.hpp>

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

using namespace harrancraft::render;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        require(argc == 2 && std::string_view(argv[1]) == "--echo",
                "Unexpected render peer scenario");
        std::size_t records = 0;
        for (;;) {
            std::array<std::uint8_t, header_bytes> header{};
            std::cin.read(reinterpret_cast<char*>(header.data()),
                          static_cast<std::streamsize>(header.size()));
            const auto received = std::cin.gcount();
            if (received == 0 && std::cin.eof()) break;
            require(received == static_cast<std::streamsize>(header.size()),
                    "Truncated render record header");
            const auto total = get(header, 8, 4);
            require(total >= header_bytes && total <= max_bytes
                    && (total - header_bytes) % quad_bytes == 0,
                    "Render record size rejected");
            Bytes bytes(static_cast<std::size_t>(total), 0);
            std::copy(header.begin(), header.end(), bytes.begin());
            const auto remaining = bytes.size() - header_bytes;
            if (remaining != 0) {
                std::cin.read(reinterpret_cast<char*>(bytes.data() + header_bytes),
                              static_cast<std::streamsize>(remaining));
                require(std::cin.gcount() == static_cast<std::streamsize>(remaining),
                        "Truncated render record quads");
            }
            const Record parsed = decode(bytes);
            const auto reencoded = encode(parsed);
            require(reencoded == bytes, "Noncanonical render record bytes");
            std::cout.write(reinterpret_cast<const char*>(reencoded.data()),
                            static_cast<std::streamsize>(reencoded.size()));
            require(static_cast<bool>(std::cout), "Render output failed");
            ++records;
            require(records <= 8, "Render record count limit reached");
        }
        require(records != 0, "No render records received");
        std::cout.flush();
        require(static_cast<bool>(std::cout), "Render output flush failed");
        std::cerr << "render-peer: passed " << records << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Render peer failed: " << error.what() << '\n';
        return 1;
    }
}
