#ifndef HARRANCRAFT_HOST_PROBE_H
#define HARRANCRAFT_HOST_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define HC_HOST_IMAGE_ABI_V1 1u

typedef struct HC_HostImageV1 {
    uint32_t size;
    uint32_t version;
    uint16_t machine;
    uint16_t reserved16;
    uint32_t reserved32;
    uint64_t file_bytes;
    uint8_t sha256[32];
} HC_HostImageV1;

typedef enum HC_HostProbeStatus {
    HC_HOST_PROBE_OK = 0,
    HC_HOST_PROBE_INVALID_ARGUMENT = 1,
    HC_HOST_PROBE_UNAVAILABLE = 2,
    HC_HOST_PROBE_UNSUPPORTED_IMAGE = 3,
    HC_HOST_PROBE_IMAGE_TOO_LARGE = 4,
    HC_HOST_PROBE_IMAGE_CHANGED = 5,
    HC_HOST_PROBE_SYSTEM_FAILURE = 6
} HC_HostProbeStatus;

#if defined(_WIN32)
#if defined(HC_HOST_PROBE_BUILD)
#define HC_HOST_PROBE_API __declspec(dllexport)
#else
#define HC_HOST_PROBE_API __declspec(dllimport)
#endif
#else
#define HC_HOST_PROBE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

HC_HOST_PROBE_API uint32_t hc_probe_current_executable_v1(
    HC_HostImageV1* output, uint32_t output_bytes);

HC_HOST_PROBE_API uint32_t hc_probe_loaded_module_v1(
    const wchar_t* module_basename, uint32_t name_chars,
    HC_HostImageV1* output, uint32_t output_bytes);

#ifdef __cplusplus
}
#endif

#endif
