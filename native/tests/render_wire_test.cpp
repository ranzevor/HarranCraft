#include <harrancraft/render_wire.hpp>

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace harrancraft::render;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Record golden() {
    Record record;
    record.kind = Kind::snapshot;
    record.identity = {0x0102030405060708ULL, 7, 9, 42, 43};
    record.key = {"minecraft:overworld", -17, 80, 33};
    record.state = "minecraft:oak_slab[type=bottom,waterlogged=false]";
    record.pose = {0.5, 82, -4, 90, -10};
    Quad quad;
    quad.cull_face = -1;
    quad.face = 2;
    quad.tint_index = -1;
    quad.shade = true;
    quad.emission = 0;
    quad.atlas = "minecraft:textures/atlas/blocks.png";
    quad.sprite = "minecraft:block/stone";
    for (std::size_t index = 0; index < 4; ++index) {
        quad.vertices[index] = {
            index == 1 || index == 2 ? 1.0f : 0.0f,
            index >= 2 ? 1.0f : 0.0f, 0.5f,
            static_cast<float>(index) / 4.0f,
            static_cast<float>(index) / 8.0f,
            0xff00aa55U, 0x00f000f0U, 0x007f0000U
        };
    }
    record.quads.push_back(quad);
    return record;
}

static bool rejects(std::span<const std::uint8_t> bytes) {
    try { (void)decode(bytes); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}
static bool rejects_encode(const Record& record) {
    try { (void)encode(record); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--golden") {
            for (const auto value : encode(golden())) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(value);
            }
            std::cout << '\n';
            return 0;
        }
        require(argc == 1, "Unexpected render test argument");
        const auto first = golden();
        const auto raw = encode(first);
        require(raw.size() == header_bytes + quad_bytes, "Golden record length changed");
        require(decode(raw) == first, "Golden render model did not round-trip");
        Record air = first;
        air.state = "minecraft:air";
        air.quads.clear();
        require(encode(air).size() == header_bytes && decode(encode(air)) == air,
                "Empty baked model did not round-trip");
        Record retire = air;
        retire.kind = Kind::retire;
        retire.state.clear();
        retire.pose = {};
        require(decode(encode(retire)) == retire, "Render retirement did not round-trip");

        Record max = first;
        max.quads.assign(max_quads, first.quads.front());
        require(encode(max).size() == max_bytes, "Maximum render frame length wrong");
        require(decode(encode(max)) == max, "Maximum render frame changed");
        max.quads.push_back(first.quads.front());
        require(rejects_encode(max), "Too many render quads were accepted");

        for (std::size_t length = 0; length < raw.size(); ++length) {
            require(rejects(Bytes(raw.begin(), raw.begin()
                           + static_cast<std::ptrdiff_t>(length))), "Truncated record accepted");
        }
        for (const auto at : {0U, 4U, 6U, 8U, 12U, 68U, 70U, 72U, 74U,
                              108U, 127U, 367U, 368U, 383U, 390U, 396U, 400U,
                              527U, 655U}) {
            auto damaged = raw;
            damaged[at] ^= 0xff;
            require(rejects(damaged), "Malformed header or metadata was accepted");
        }
        for (const auto at : {76U, 84U, 92U}) {
            auto damaged = raw;
            put(damaged, at, std::bit_cast<std::uint64_t>(
                std::numeric_limits<double>::quiet_NaN()), 8);
            require(rejects(damaged), "Non-finite camera position accepted");
        }
        for (const auto at : {100U, 104U}) {
            auto damaged = raw;
            put(damaged, at, std::bit_cast<std::uint32_t>(
                std::numeric_limits<float>::quiet_NaN()), 4);
            require(rejects(damaged), "Non-finite camera angle accepted");
        }
        for (const auto index : {0U, 4U, 8U, 12U, 16U}) {
            auto damaged = raw;
            put(damaged, header_bytes + 272 + index,
                std::bit_cast<std::uint32_t>(
                    std::numeric_limits<float>::quiet_NaN()), 4);
            require(rejects(damaged), "Non-finite baked vertex accepted");
        }
        auto malformed = first;
        malformed.identity.capture = 0;
        require(rejects_encode(malformed), "Missing capture identity accepted");
        malformed = first;
        malformed.quads.front().atlas = "invalid\natlas";
        require(rejects_encode(malformed), "Invalid texture ID accepted");
        malformed = first;
        malformed.quads.front().face = 6;
        require(rejects_encode(malformed), "Invalid quad face accepted");
        malformed = retire;
        malformed.state = "minecraft:stone";
        require(rejects_encode(malformed), "Render retirement retained state");
        std::cout << "render-wire: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
