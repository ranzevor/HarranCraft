#include <harrancraft/host_observation.hpp>
#include <harrancraft/host_probe.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static_assert(sizeof(HC_HostImageV1) == 56);
static_assert(offsetof(HC_HostImageV1, sha256) == 24);

using Probe = std::uint32_t(__cdecl*)(HC_HostImageV1*, std::uint32_t);
using ModuleProbe = std::uint32_t(__cdecl*)(
    const wchar_t*, std::uint32_t, HC_HostImageV1*, std::uint32_t);

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::wstring executable_path() {
    std::vector<wchar_t> chars(32768);
    const DWORD length = GetModuleFileNameW(nullptr, chars.data(), static_cast<DWORD>(chars.size()));
    require(length > 0 && length < chars.size(), "Executable path unavailable");
    return {chars.data(), length};
}

struct Module {
    HMODULE value;
    explicit Module(const std::wstring& path) : value(LoadLibraryW(path.c_str())) {
        require(value != nullptr, "Cannot load host probe DLL");
    }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    ~Module() { FreeLibrary(value); }
};

static HC_HostImageV1 capture(Probe probe) {
    HC_HostImageV1 result{};
    require(probe(&result, sizeof(result)) == HC_HOST_PROBE_OK, "Host image probe failed");
    require(result.size == sizeof(result) && result.version == HC_HOST_IMAGE_ABI_V1,
            "Wrong host image ABI version");
    require(result.machine == 0x8664 && result.file_bytes > 0, "Wrong image format");
    require(result.reserved16 == 0 && result.reserved32 == 0, "Reserved output was not zero");
    require(std::any_of(std::begin(result.sha256), std::end(result.sha256),
                        [](std::uint8_t byte) { return byte != 0; }), "Empty digest");
    return result;
}

int main(int argc, char** argv) {
    try {
        const bool exe_digest = argc == 2 && std::string_view(argv[1]) == "--sha256";
        const bool module_digest = argc == 2 && std::string_view(argv[1]) == "--module-sha256";
        require(argc == 1 || exe_digest || module_digest, "Unknown host probe test argument");
        const auto self = executable_path();
        const auto slash = self.find_last_of(L"\\/");
        require(slash != std::wstring::npos, "Executable directory unavailable");
        const auto dll = self.substr(0, slash + 1) + L"hc-host-probe.dll";
        Module module(dll);
        auto* address = GetProcAddress(module.value, "hc_probe_current_executable_v1");
        require(address != nullptr, "Versioned host probe export missing");
        static_assert(sizeof(Probe) == sizeof(FARPROC));
        const auto probe = std::bit_cast<Probe>(address);
        auto* module_address = GetProcAddress(module.value, "hc_probe_loaded_module_v1");
        require(module_address != nullptr, "Versioned loaded-module export missing");
        static_assert(sizeof(ModuleProbe) == sizeof(FARPROC));
        const auto probe_module = std::bit_cast<ModuleProbe>(module_address);

        HC_HostImageV1 unchanged{};
        unchanged.version = 99;
        require(probe(nullptr, sizeof(unchanged)) == HC_HOST_PROBE_INVALID_ARGUMENT,
                "Null output was accepted");
        require(probe(&unchanged, sizeof(unchanged) - 1) == HC_HOST_PROBE_INVALID_ARGUMENT,
                "Short output buffer was accepted");
        require(unchanged.version == 99, "Invalid argument changed caller data");

        const auto first = capture(probe);
        const auto second = capture(probe);
        require(std::memcmp(&first, &second, sizeof(first)) == 0,
                "Repeated host image probes disagree");

        constexpr wchar_t library_name[] = L"hc-host-probe.dll";
        constexpr auto library_chars = static_cast<std::uint32_t>(
            sizeof(library_name) / sizeof(library_name[0]) - 1);
        HC_HostImageV1 library{};
        require(probe_module(library_name, library_chars, &library, sizeof(library))
                    == HC_HOST_PROBE_OK, "Loaded module probe failed");
        require(library.version == HC_HOST_IMAGE_ABI_V1 && library.machine == 0x8664
                    && library.file_bytes > 0, "Loaded DLL identity is invalid");
        const auto library_repeat = [&] {
            HC_HostImageV1 result{};
            require(probe_module(library_name, library_chars, &result, sizeof(result))
                        == HC_HOST_PROBE_OK, "Repeated module probe failed");
            return result;
        };
        const auto repeated_library = library_repeat();
        require(std::memcmp(&library, &repeated_library, sizeof(library)) == 0,
                "Loaded DLL fingerprint changed");

        HC_HostImageV1 invalid_name{};
        invalid_name.version = 99;
        constexpr wchar_t path_name[] = L"../hc-host-probe.dll";
        require(probe_module(path_name, static_cast<std::uint32_t>(
                    sizeof(path_name) / sizeof(path_name[0]) - 1),
                    &invalid_name, sizeof(invalid_name)) == HC_HOST_PROBE_INVALID_ARGUMENT,
                "Module path was accepted");
        require(probe_module(nullptr, 0, &invalid_name, sizeof(invalid_name))
                    == HC_HOST_PROBE_INVALID_ARGUMENT, "Null module name was accepted");
        require(invalid_name.version == 99, "Invalid module name wrote output");
        constexpr wchar_t absent[] = L"harrancraft-absent-module.dll";
        HC_HostImageV1 missing{};
        missing.version = 99;
        require(probe_module(absent, static_cast<std::uint32_t>(
                    sizeof(absent) / sizeof(absent[0]) - 1),
                    &missing, sizeof(missing)) == HC_HOST_PROBE_UNAVAILABLE
                    && missing.version == 0, "Missing module did not fail closed");

        HANDLE file = CreateFileW(self.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(file != INVALID_HANDLE_VALUE, "Cannot independently open current executable");
        LARGE_INTEGER size{};
        const BOOL measured = GetFileSizeEx(file, &size);
        CloseHandle(file);
        require(measured && size.QuadPart > 0
                    && static_cast<std::uint64_t>(size.QuadPart) == first.file_bytes,
                "Current executable length mismatch");

        harrancraft::host::ImageIdentity image{};
        std::copy(std::begin(first.sha256), std::end(first.sha256), image.sha256.begin());
        image.file_bytes = first.file_bytes;
        image.machine = first.machine;
        require(harrancraft::host::valid_image(image), "Windows identity cannot enter host gate");
        harrancraft::host::Observer observer({image});
        auto changed = image;
        changed.sha256[0] ^= 1;
        const harrancraft::host::Evidence synthetic{
            harrancraft::host::Mode::single_player,
            harrancraft::host::Lifecycle::playing,
            harrancraft::host::ReadPhase::verified_engine_read, 1};
        require(!observer.begin(changed, synthetic), "Unlisted executable was accepted");
        require(observer.begin(image, synthetic).has_value(), "Matching executable was rejected");
        observer.stop();

        DWORD before = 0;
        DWORD after = 0;
        require(GetProcessHandleCount(GetCurrentProcess(), &before) != 0,
                "Cannot measure process handles");
        for (int iteration = 0; iteration < 16; ++iteration) {
            const auto next = capture(probe);
            require(std::memcmp(&first, &next, sizeof(first)) == 0, "Probe result changed");
            const auto next_module = library_repeat();
            require(std::memcmp(&library, &next_module, sizeof(library)) == 0,
                    "Loaded module identity changed");
        }
        require(GetProcessHandleCount(GetCurrentProcess(), &after) != 0
                    && after <= before + 1, "Repeated probes retained process handles");

        if (exe_digest || module_digest) {
            const auto& digest = module_digest ? library : first;
            for (std::uint8_t value : digest.sha256) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(value);
            }
            std::cout << '\n';
        } else {
            std::cout << "host-probe-process: passed\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
