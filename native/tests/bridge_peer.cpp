#include <harrancraft/wire.hpp>

#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace harrancraft::wire;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static Frame receive() {
    Bytes bytes{};
    std::cin.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(std::cin.gcount() == static_cast<std::streamsize>(bytes.size()), "Peer stream ended");
    return decode(bytes);
}
static void send(const Frame& frame) {
    auto bytes = encode(frame);
    std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::cout.flush();
    require(static_cast<bool>(std::cout), "Peer write failed");
}
static std::uint64_t number(std::string_view value) {
    std::uint64_t result = 0;
    auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
    require(converted.ec == std::errc{} && converted.ptr == value.data() + value.size(), "Invalid peer argument");
    return result;
}
class Scenario {
public:
    explicit Scenario(std::uint64_t prior) : old_epoch(prior) {
        send(Frame{});
        auto welcome = receive();
        require(welcome.kind == Kind::welcome && welcome.origin == guest_origin && welcome.epoch != 0,
                "Expected guest welcome");
        epoch = welcome.epoch;
    }
    Frame command(Kind kind, std::string expected = "", std::string state = "") {
        Frame f;
        f.kind = kind; f.epoch = epoch; f.id = ++sequence;
        if (kind == Kind::get || kind == Kind::set) {
            f.dimension = "minecraft:overworld";
            f.x = 0; f.y = 80; f.z = 0;
            f.expected = std::move(expected); f.state = std::move(state);
        }
        return f;
    }
    void event(const Frame& f) {
        require(f.epoch == epoch && f.kind == Kind::event && f.id == ++last_event,
                "Event epoch or sequence mismatch");
        require(events.size() < 1024, "Too many peer events");
        events.push_back(f);
    }
    Frame exchange(const Frame& request) {
        send(request);
        for (;;) {
            auto f = receive();
            if (f.kind == Kind::event) { event(f); continue; }
            require(f.epoch == epoch && f.id == request.id && f.origin == guest_origin,
                    "Response correlation mismatch");
            require(f.kind == Kind::result || f.kind == Kind::pong, "Unexpected response kind");
            return f;
        }
    }
    void cycle() {
        auto first = exchange(command(Kind::get));
        require(first.status == Status::ok && first.state == "minecraft:air", "Expected initial air");
        auto place = command(Kind::set, "minecraft:air", "minecraft:diamond_block");
        auto placed = exchange(place);
        require(placed.status == Status::ok && placed.state == place.state, "Placement not applied");
        require(exchange(place) == placed, "Duplicate did not preserve its receipt");
        auto conflict = place;
        conflict.state = "minecraft:gold_block";
        require(exchange(conflict).status == Status::conflict, "Changed duplicate was accepted");
        auto wrong = exchange(command(Kind::set, "minecraft:air", "minecraft:stone"));
        require(wrong.status == Status::conflict && wrong.state == place.state, "CAS mismatch was accepted");
        require(exchange(command(Kind::set, place.state, "minecraft:chest[facing=north,type=single,waterlogged=false]")).status == Status::unsupported_state,
                "Block entity replacement was accepted");
        require(exchange(command(Kind::ping)).kind == Kind::pong, "Heartbeat failed");
        require(events.size() == 1 && events.front().origin == native_origin && events.front().cause == place.id,
                "Placement event duplicated or misattributed");
        std::cerr << "{\"stage\":\"placed\"}\n" << std::flush;
        for (;;) {
            auto f = receive();
            event(f);
            if (f.origin == guest_origin && f.cause == 0 && f.expected == "minecraft:diamond_block"
                    && f.state == "minecraft:gold_block") break;
        }
        auto removed = exchange(command(Kind::set, "minecraft:gold_block", "minecraft:air"));
        require(removed.status == Status::ok && removed.state == "minecraft:air", "Removal failed");
        for (int i = 0; i < 16; ++i) {
            auto put = exchange(command(Kind::set, "minecraft:air", "minecraft:oak_log[axis=x]"));
            require(put.status == Status::ok && put.state == "minecraft:oak_log[axis=x]", "Property state lost");
            auto erase = exchange(command(Kind::set, "minecraft:oak_log[axis=x]", "minecraft:air"));
            require(erase.status == Status::ok && erase.state == "minecraft:air", "Repeated removal failed");
        }
        require(exchange(command(Kind::get)).state == "minecraft:air", "Final world state mismatch");
        require(events.size() == 35, "Unexpected mutation event count");
        std::cerr << "{\"stage\":\"removed\"}\n" << std::flush;
    }
    void stale() {
        require(old_epoch != 0 && old_epoch != epoch, "Reconnect reused an epoch");
        auto old = command(Kind::set, "minecraft:air", "minecraft:diamond_block");
        old.epoch = old_epoch;
        require(exchange(old).status == Status::stale_epoch, "Stale session changed the world");
        auto current = exchange(command(Kind::get));
        require(current.status == Status::ok && current.state == "minecraft:air", "Stale command had effects");
        require(events.empty(), "Stale command emitted an event");
    }
    void finish() {
        send(command(Kind::close));
        std::cerr << "{\"stage\":\"passed\",\"epoch\":" << epoch
                  << ",\"events\":" << events.size() << "}\n" << std::flush;
    }
private:
    std::uint64_t epoch = 0, sequence = 0, last_event = 0, old_epoch;
    std::vector<Frame> events;
};
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        require(argc >= 2 && argc <= 3, "Expected a peer scenario");
        const std::string_view scenario(argv[1]);
        if (scenario == "invalid-version") {
            auto bytes = encode(Frame{});
            put(bytes, 4, version + 1, 2);
            std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            std::cout.flush();
            require(std::cin.get() == std::char_traits<char>::eof(), "Invalid version received data");
            std::cerr << "{\"stage\":\"passed\"}\n";
            return 0;
        }
        Scenario test(argc == 3 ? number(argv[2]) : 0);
        if (scenario == "cycle") test.cycle();
        else if (scenario == "stale") test.stale();
        else throw std::invalid_argument("Unknown peer scenario");
        test.finish();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Peer verification failed: " << e.what() << '\n';
        return 1;
    }
}
