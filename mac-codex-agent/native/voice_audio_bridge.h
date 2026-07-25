#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define SWVA_EXPORT __attribute__((visibility("default")))
#else
#define SWVA_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

SWVA_EXPORT int swva_find_device_uid(const char* name, char* output, size_t output_size);
SWVA_EXPORT int swva_get_default_input_uid(char* output, size_t output_size);
SWVA_EXPORT int swva_set_default_input_uid(const char* uid);
SWVA_EXPORT int swva_open(const char* output_device_uid);
SWVA_EXPORT int swva_start(void);
SWVA_EXPORT size_t swva_write_mono_16k(const int16_t* samples, size_t count);
SWVA_EXPORT uint64_t swva_dropped_frames(void);
SWVA_EXPORT void swva_stop(void);
SWVA_EXPORT void swva_close(void);

#ifdef __cplusplus
}
#endif

#undef SWVA_EXPORT
