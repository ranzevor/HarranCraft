#include <harrancraft/wire.hpp>

#include <iomanip>
#include <iostream>

using namespace harrancraft::wire;

static Frame golden() {
    Frame f;
    f.kind = Kind::set;
    f.epoch = 0x0102030405060708ULL;
    f.id = 42;
    f.x = -17; f.y = 64; f.z = 33;
    f.dimension = "minecraft:overworld";
    f.expected = "minecraft:air";
    f.state = "minecraft:oak_log[axis=x]";
    return f;
}
static void require(bool value) {
    if (!value) throw std::runtime_error("Wire assertion failed");
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--golden") {
            for (const auto b : encode(golden()))
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b);
            std::cout << '\n';
            return 0;
        }
        require(decode(encode(golden())) == golden());
        for (const auto offset : {0U, 4U, 50U, 60U, 127U, 319U, 511U}) {
            auto bytes = encode(golden());
            bytes[offset] ^= 0x7FU;
            bool failed = false;
            try { (void)decode(bytes); } catch (const std::invalid_argument&) { failed = true; }
            require(failed);
        }
        for (const auto offset : {44U, 46U, 48U}) {
            auto bytes = encode(golden());
            put(bytes, offset, 0xFFFF, 2);
            bool failed = false;
            try { (void)decode(bytes); } catch (const std::invalid_argument&) { failed = true; }
            require(failed);
        }
        auto f = golden();
        f.state.assign(193, 'a');
        bool failed = false;
        try { (void)encode(f); } catch (const std::invalid_argument&) { failed = true; }
        require(failed);
        f = golden();
        f.x = std::numeric_limits<std::int32_t>::min();
        f.z = std::numeric_limits<std::int32_t>::max();
        require(decode(encode(f)) == f);
        std::cout << "wire-contract: passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
