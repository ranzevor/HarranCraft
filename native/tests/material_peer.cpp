#include <harrancraft/material_receiver.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using harrancraft::material::MaterialReceiver;
using harrancraft::material::ReceiveState;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    try {
        require(argc == 2 && std::string_view(argv[1]) == "--materials",
                "Unknown material peer scenario");
        std::array<std::uint8_t, harrancraft::render::header_bytes> header{};
        std::cin.read(reinterpret_cast<char*>(header.data()),
                      static_cast<std::streamsize>(header.size()));
        require(std::cin.gcount() == static_cast<std::streamsize>(header.size()),
                "Missing baked model record");
        const auto size = static_cast<std::size_t>(
            harrancraft::render::get(header, 8, 4));
        require(size >= harrancraft::render::header_bytes
                && size <= harrancraft::render::max_bytes
                && (size - harrancraft::render::header_bytes)
                    % harrancraft::render::quad_bytes == 0,
                "Invalid baked model length");
        harrancraft::render::Bytes bytes(size, 0);
        std::copy(header.begin(), header.end(), bytes.begin());
        const auto tail = size - header.size();
        if (tail != 0) {
            std::cin.read(reinterpret_cast<char*>(bytes.data() + header.size()),
                          static_cast<std::streamsize>(tail));
            require(std::cin.gcount() == static_cast<std::streamsize>(tail),
                    "Truncated model quads");
        }
        const auto model = harrancraft::render::decode(bytes);
        MaterialReceiver receiver;
        receiver.begin(model.identity.epoch, model.identity.world,
                       model.identity.camera, model.key.dimension);
        require(receiver.begin_model(model), "Invalid native material manifest");

        std::array<std::uint8_t, 517> fragment{};
        for (;;) {
            std::cin.read(reinterpret_cast<char*>(fragment.data()),
                          static_cast<std::streamsize>(fragment.size()));
            const auto count = std::cin.gcount();
            if (count > 0) {
                require(receiver.feed(std::span<const std::uint8_t>(
                            fragment.data(), static_cast<std::size_t>(count))),
                        "Malformed, stale or duplicate source sprite record");
            }
            if (std::cin.eof()) break;
            require(static_cast<bool>(std::cin), "Source texture input stream failed");
        }
        require(receiver.ready() || receiver.required() == 0,
                "Incomplete model material was not rejected");
        const auto expected = receiver.required();
        const auto complete = receiver.complete();
        receiver.end_of_stream();
        require(receiver.state() == ReceiveState::ended && receiver.epoch() == 0
                    && receiver.complete() == 0,
                "Native source pixels survived channel shutdown");
        std::cout << "{\"expected\":" << expected
                  << ",\"received\":" << complete << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Material peer failed: " << error.what() << '\n';
        return 1;
    }
}
