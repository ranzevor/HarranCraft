#include <harrancraft/texture_wire.hpp>

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace harrancraft::texture;

static void require(bool result, const char* reason) {
    if (!result) throw std::runtime_error(reason);
}
static Record golden() {
    Record r;
    r.identity = {0x0102030405060708ULL, 7, 9, 42, 43};
    r.atlas = "minecraft:textures/atlas/blocks.png";
    r.sprite = "minecraft:block/stone";
    r.atlas_x = 16;
    r.atlas_y = 32;
    r.width = 2;
    r.height = 2;
    r.min_u = 0.125f; r.max_u = 0.25f;
    r.min_v = 0.375f; r.max_v = 0.5f;
    r.argb = {0xff112233U, 0x80123456U, 0x00000000U, 0xffffffffU};
    return r;
}
static bool refused(std::span<const std::uint8_t> raw) {
    try { (void)decode(raw); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}
static bool refused_encode(const Record& record) {
    try { (void)encode(record); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--golden") {
            for (const auto c : encode(golden())) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(c);
            }
            std::cout << '\n';
            return 0;
        }
        require(argc == 1, "Unexpected texture test argument");
        auto normal = golden();
        const auto raw = encode(normal);
        require(raw.size() == header_bytes + 16, "Golden texture frame changed");
        require(decode(raw) == normal, "ARGB texture data changed");
        auto retire = normal;
        retire.kind = Kind::retire;
        retire.atlas_x = 0; retire.atlas_y = 0;
        retire.width = 0; retire.height = 0;
        retire.min_u = 0; retire.max_u = 0;
        retire.min_v = 0; retire.max_v = 0;
        retire.argb.clear();
        require(decode(encode(retire)) == retire, "Texture retirement changed");
        auto maximum = normal;
        maximum.width = max_edge;
        maximum.height = max_edge;
        maximum.argb.assign(max_pixels, 0xff123456U);
        require(encode(maximum).size() == max_bytes, "Maximum record limit changed");
        require(decode(encode(maximum)) == maximum, "Maximum pixels changed");
        maximum.width = static_cast<std::uint16_t>(max_edge + 1);
        require(refused_encode(maximum), "Oversize source sprite was accepted");

        for (std::size_t at = 0; at < raw.size(); ++at) {
            require(refused(Bytes(raw.begin(), raw.begin()
                            + static_cast<std::ptrdiff_t>(at))), "Truncated image was accepted");
        }
        for (const auto at : {0U, 4U, 6U, 8U, 12U, 59U, 63U, 64U, 66U, 68U,
                               88U, 90U, 92U, 96U, 191U, 192U, 319U}) {
            auto damaged = raw;
            damaged[at] ^= 0xff;
            require(refused(damaged), "Invalid texture header was accepted");
        }
        for (const auto at : {72U, 76U, 80U, 84U}) {
            auto damaged = raw;
            put(damaged, at, std::bit_cast<std::uint32_t>(
                std::numeric_limits<float>::quiet_NaN()), 4);
            require(refused(damaged), "NaN atlas UV was accepted");
        }
        normal.argb.clear();
        require(refused_encode(normal), "Missing source pixels were accepted");
        normal = golden();
        normal.min_u = normal.max_u;
        require(refused_encode(normal), "Degenerate texture UV accepted");
        normal = golden();
        normal.identity.epoch = 0;
        require(refused_encode(normal), "Invalid epoch accepted");
        std::cout << "texture-wire: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
