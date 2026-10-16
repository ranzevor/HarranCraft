#include <harrancraft/geometry_wire.hpp>

#include <iostream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace harrancraft::geometry;

static void require(bool result) {
    if (!result) throw std::runtime_error("Geometry assertion failed");
}
static Frame golden() {
    return {Kind::snapshot, 0x0102030405060708ULL, 42,
            {"minecraft:overworld", -17, 64, 33}, "minecraft:oak_slab[type=bottom,waterlogged=false]",
            {{0, 0, 0, 1, 0.5, 1}}};
}
static bool refused(const Bytes& bytes) {
    try { (void)decode(bytes); } catch (const std::invalid_argument&) { return true; }
    return false;
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--golden") {
            for (auto byte : encode(golden())) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(byte);
            }
            std::cout << '\n';
            return 0;
        }
        require(argc == 1);
        require(decode(encode(golden())) == golden());
        auto retire = Frame{Kind::retire, 7, 5, golden().key, "", {}};
        require(decode(encode(retire)) == retire);
        auto empty = Frame{Kind::snapshot, 7, 6, golden().key, "minecraft:air", {}};
        require(decode(encode(empty)) == empty);
        auto max = empty;
        max.boxes.assign(max_boxes, {0, 0, 0, 1, 1, 1});
        require(encode(max).size() == max_bytes);
        require(decode(encode(max)) == max);
        max.boxes.push_back({0, 0, 0, 1, 1, 1});
        bool invalid = false;
        try { (void)encode(max); } catch (const std::invalid_argument&) { invalid = true; }
        require(invalid);
        auto base = encode(golden());
        for (std::size_t size = 0; size < base.size(); ++size) {
            require(refused(Bytes(base.begin(), base.begin() + static_cast<std::ptrdiff_t>(size))));
        }
        for (const auto at : {0U, 4U, 6U, 8U, 12U, 44U, 46U, 48U, 50U, 52U, 56U, 127U, 319U}) {
            auto corrupted = base;
            corrupted[at] ^= 0xff;
            require(refused(corrupted));
        }
        for (std::size_t i = 0; i < 6; ++i) {
            auto corrupted = base;
            put(corrupted, header_bytes + i * 8,
                std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()), 8);
            require(refused(corrupted));
        }
        auto malformed = golden();
        malformed.boxes[0].max_x = 0;
        invalid = false;
        try { (void)encode(malformed); } catch (const std::invalid_argument&) { invalid = true; }
        require(invalid);
        malformed = golden();
        malformed.revision = 0;
        invalid = false;
        try { (void)encode(malformed); } catch (const std::invalid_argument&) { invalid = true; }
        require(invalid);
        malformed = retire;
        malformed.boxes.push_back({0, 0, 0, 1, 1, 1});
        invalid = false;
        try { (void)encode(malformed); } catch (const std::invalid_argument&) { invalid = true; }
        require(invalid);
        std::cout << "geometry-wire: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
