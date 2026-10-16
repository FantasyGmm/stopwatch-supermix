#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SwuiHandle SwuiHandle;
typedef void (*SwuiCallback)(void *context, int32_t command, const char *value);
typedef void (*SwuiTickCallback)(void *context, SwuiHandle *handle);

enum {
    SWUI_RECONNECT = 1,
    SWUI_SELECT_WATCH = 2,
    SWUI_RENAME_AGENT = 3,
    SWUI_FORGET_WATCH = 4,
    SWUI_SET_CONNECTION = 5,
    SWUI_SETUP_VOICE = 6,
    SWUI_OPEN_PET = 7,
    SWUI_PET_CHOOSE = 8,
    SWUI_PET_SEND = 9,
    SWUI_PET_CANCEL = 10,
    SWUI_PET_RESTORE = 11,
    SWUI_QUIT = 12,
};

SwuiHandle *swui_create(void *context, SwuiCallback callback, SwuiTickCallback tick);
int32_t swui_run(SwuiHandle *handle);
void swui_update(SwuiHandle *handle, const char *snapshot_json, const char *connection_json);
void swui_pairing_code(SwuiHandle *handle, const char *code);
void swui_pet_state(SwuiHandle *handle, const char *headline, const char *detail,
                    double progress, int32_t can_send, int32_t can_cancel,
                    int32_t can_restore);
void swui_pet_preview_rgba(SwuiHandle *handle, const uint8_t *bytes,
                           size_t byte_count, int32_t width, int32_t height);
int32_t swui_pet_preview_visible(SwuiHandle *handle);
void swui_show_pet(SwuiHandle *handle);
void swui_shutdown(SwuiHandle *handle);
void swui_destroy(SwuiHandle *handle);
int32_t swui_connection_enabled(void);
void swui_set_connection_enabled(int32_t enabled);

#ifdef __cplusplus
}
#endif
