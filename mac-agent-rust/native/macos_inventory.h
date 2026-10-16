#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 on a completed lookup and writes a bitmask: 0b01 for Codex,
// 0b10 for Trae. Returns -1 when LaunchServices cannot complete either lookup.
int32_t sw_detect_installed_agents(uint8_t *mask);

#ifdef __cplusplus
}
#endif
