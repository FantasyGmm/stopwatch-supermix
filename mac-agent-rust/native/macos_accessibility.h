#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t codex_foreground;
    int32_t accessibility_trusted;
    int32_t editable;
    int32_t has_text;
    int32_t semantic; /* 0 unknown, 1 send, 2 queue, 3 steer, 4 running */
    uint64_t handle;   /* opaque, single-use prepared AX target */
} SwaxSnapshot;

int32_t swax_prepare(SwaxSnapshot *output);
int32_t swax_dispatch(uint64_t handle, int32_t dispatch_kind);
void swax_discard(uint64_t handle);
int32_t swax_accessibility_trusted(void);
int32_t swax_open_task(const char *thread_id);
int32_t swax_set_value(uint64_t handle, const char *value, SwaxSnapshot *output);
int32_t swax_restore_value(uint64_t handle);
int32_t swvoice_typeless_running(void);
int32_t swvoice_post_key(uint16_t key_code, int32_t down);

#ifdef __cplusplus
}
#endif
