#pragma once

#include "display_protection_logic.h"

#include <hal/button_input_gate.h>
#include <cstdint>
#include <lvgl.h>

struct DisplayProtectionSnapshot {
    DisplayProtectionState state = DisplayProtectionState::Active;
    int effective_brightness = 0;
    int restore_brightness = 80;
    bool automatic_enabled = true;
    uint8_t reason_mask = 0;
};

class GlobalDisplayProtectionService {
public:
    bool init();
    void update();
    void applyPendingHardwareUpdate();
    void shutdown();
    void setAutomaticProtection(bool enabled, bool restore_display = true);
    void setKeepAwake(DisplayKeepAwakeReason reason, bool active);
    DisplayProtectionSnapshot snapshot() const;

private:
    void acceptDecision(const DisplayProtectionDecision& decision,
                        bool restore_display = true);
    void syncUserBrightness();

    DisplayProtectionLogic _logic;
    int _restore_brightness = 80;
    int _pending_brightness = -1;
    uint32_t _brightness_revision = 0;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    ButtonPhysicalState _last_buttons{};
    bool _initialized = false;
};

GlobalDisplayProtectionService& GetGlobalDisplayProtectionService();
