#include <harrancraft/host_probe.h>

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

static_assert(sizeof(void*) == 8, "The host probe requires Windows x64");
static_assert(sizeof(HC_HostImageV1) == 56);
static_assert(offsetof(HC_HostImageV1, file_bytes) == 16);
static_assert(offsetof(HC_HostImageV1, sha256) == 24);

namespace {
constexpr std::uint64_t max_image_bytes = 512ULL * 1024 * 1024;

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};

struct ModuleRef {
    HMODULE value = nullptr;
    ~ModuleRef() {
        if (value != nullptr) FreeLibrary(value);
    }
};

struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE state = nullptr;
    Hash() = default;
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    ~Hash() {
        if (state != nullptr) BCryptDestroyHash(state);
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    bool start() {
        return BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
            && BCryptCreateHash(algorithm, &state, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    bool feed(std::uint8_t* bytes, DWORD length) const {
        return BCryptHashData(state, bytes, length, 0) >= 0;
    }
    bool finish(std::uint8_t (&digest)[32]) const {
        return BCryptFinishHash(state, digest, static_cast<ULONG>(sizeof(digest)), 0) >= 0;
    }
};

bool module_path(HMODULE module, std::wstring& path) {
    for (DWORD capacity = 512; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = GetModuleFileNameW(module, buffer.data(), capacity);
        if (length == 0) return false;
        if (length < capacity) {
            path.assign(buffer.data(), length);
            return true;
        }
    }
    return false;
}

bool read_exact(HANDLE handle, void* value, DWORD bytes) {
    DWORD read = 0;
    return ReadFile(handle, value, bytes, &read, nullptr) != 0 && read == bytes;
}

bool seek(HANDLE handle, std::int64_t offset) {
    LARGE_INTEGER target{};
    target.QuadPart = offset;
    return SetFilePointerEx(handle, target, nullptr, FILE_BEGIN) != 0;
}

bool is_amd64_image(HANDLE handle, std::uint64_t size, bool expect_dll) {
    IMAGE_DOS_HEADER dos{};
    if (!seek(handle, 0) || !read_exact(handle, &dos, static_cast<DWORD>(sizeof(dos)))
        || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos))) {
        return false;
    }
    const auto nt_offset = static_cast<std::uint64_t>(dos.e_lfanew);
    if (nt_offset > size || sizeof(IMAGE_NT_HEADERS64) > size - nt_offset
        || !seek(handle, static_cast<std::int64_t>(nt_offset))) {
        return false;
    }
    IMAGE_NT_HEADERS64 header{};
    if (!read_exact(handle, &header, static_cast<DWORD>(sizeof(header)))) return false;
    return header.Signature == IMAGE_NT_SIGNATURE
        && header.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
        && header.FileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER64)
        && (header.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) != 0
        && (((header.FileHeader.Characteristics & IMAGE_FILE_DLL) != 0) == expect_dll)
        && header.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
}

bool same_file(const BY_HANDLE_FILE_INFORMATION& left,
               const BY_HANDLE_FILE_INFORMATION& right) {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber
        && left.nFileIndexHigh == right.nFileIndexHigh
        && left.nFileIndexLow == right.nFileIndexLow
        && left.nFileSizeHigh == right.nFileSizeHigh
        && left.nFileSizeLow == right.nFileSizeLow
        && CompareFileTime(&left.ftLastWriteTime, &right.ftLastWriteTime) == 0;
}

bool valid_basename(const wchar_t* name, std::uint32_t length) {
    if (name == nullptr || length < 5 || length > 64) return false;
    const std::wstring_view value(name, length);
    const auto lower = [](wchar_t ch) {
        return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
    };
    const auto extension = value.substr(value.size() - 4);
    if (extension[0] != L'.' || lower(extension[1]) != L'd'
        || lower(extension[2]) != L'l' || lower(extension[3]) != L'l') {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')
            || (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'_' || ch == L'-';
    });
}

std::uint32_t probe(HC_HostImageV1& identity, HMODULE module, bool expect_dll) {
    std::wstring path;
    if (!module_path(module, path)) return HC_HOST_PROBE_UNAVAILABLE;

    File file;
    // Refuse existing or new writers while measuring the on-disk executable.
    file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE || GetFileType(file.handle) != FILE_TYPE_DISK) {
        return HC_HOST_PROBE_UNAVAILABLE;
    }
    LARGE_INTEGER length{};
    BY_HANDLE_FILE_INFORMATION before{};
    if (!GetFileSizeEx(file.handle, &length)
        || !GetFileInformationByHandle(file.handle, &before)) {
        return HC_HOST_PROBE_SYSTEM_FAILURE;
    }
    if (length.QuadPart <= 0) return HC_HOST_PROBE_UNSUPPORTED_IMAGE;
    const auto file_bytes = static_cast<std::uint64_t>(length.QuadPart);
    if (file_bytes > max_image_bytes) return HC_HOST_PROBE_IMAGE_TOO_LARGE;
    if (!is_amd64_image(file.handle, file_bytes, expect_dll) || !seek(file.handle, 0)) {
        return HC_HOST_PROBE_UNSUPPORTED_IMAGE;
    }
    Hash digest;
    if (!digest.start()) return HC_HOST_PROBE_SYSTEM_FAILURE;
    std::array<std::uint8_t, 65536> chunk{};
    std::uint64_t remaining = file_bytes;
    while (remaining != 0) {
        const DWORD target = static_cast<DWORD>(
            std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(chunk.size())));
        DWORD received = 0;
        if (!ReadFile(file.handle, chunk.data(), target, &received, nullptr)) {
            return HC_HOST_PROBE_SYSTEM_FAILURE;
        }
        if (received == 0 || received > target) return HC_HOST_PROBE_IMAGE_CHANGED;
        if (!digest.feed(chunk.data(), received)) return HC_HOST_PROBE_SYSTEM_FAILURE;
        remaining -= received;
    }
    DWORD extra = 0;
    if (!ReadFile(file.handle, chunk.data(), 1, &extra, nullptr)) {
        return HC_HOST_PROBE_SYSTEM_FAILURE;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if (extra != 0 || !GetFileInformationByHandle(file.handle, &after)
        || !same_file(before, after)) {
        return HC_HOST_PROBE_IMAGE_CHANGED;
    }
    if (!digest.finish(identity.sha256)) return HC_HOST_PROBE_SYSTEM_FAILURE;
    identity.size = static_cast<std::uint32_t>(sizeof(HC_HostImageV1));
    identity.version = HC_HOST_IMAGE_ABI_V1;
    identity.machine = IMAGE_FILE_MACHINE_AMD64;
    identity.file_bytes = file_bytes;
    return HC_HOST_PROBE_OK;
}
}  // namespace

extern "C" HC_HOST_PROBE_API uint32_t hc_probe_current_executable_v1(
    HC_HostImageV1* output, uint32_t output_bytes) {
    if (output == nullptr || output_bytes != sizeof(HC_HostImageV1)) {
        return HC_HOST_PROBE_INVALID_ARGUMENT;
    }
    *output = {};
    try {
        HC_HostImageV1 result{};
        const auto status = probe(result, nullptr, false);
        if (status == HC_HOST_PROBE_OK) *output = result;
        return status;
    } catch (...) {
        *output = {};
        return HC_HOST_PROBE_SYSTEM_FAILURE;
    }
}

extern "C" HC_HOST_PROBE_API uint32_t hc_probe_loaded_module_v1(
    const wchar_t* module_basename, uint32_t name_chars,
    HC_HostImageV1* output, uint32_t output_bytes) {
    if (output == nullptr || output_bytes != sizeof(HC_HostImageV1)
        || !valid_basename(module_basename, name_chars)) {
        return HC_HOST_PROBE_INVALID_ARGUMENT;
    }
    *output = {};
    try {
        const std::wstring name(module_basename, name_chars);
        ModuleRef module;
        // This borrows an existing process module only; no library is loaded.
        if (!GetModuleHandleExW(0, name.c_str(), &module.value)) {
            return HC_HOST_PROBE_UNAVAILABLE;
        }
        HC_HostImageV1 result{};
        const auto status = probe(result, module.value, true);
        if (status == HC_HOST_PROBE_OK) *output = result;
        return status;
    } catch (...) {
        *output = {};
        return HC_HOST_PROBE_SYSTEM_FAILURE;
    }
}
