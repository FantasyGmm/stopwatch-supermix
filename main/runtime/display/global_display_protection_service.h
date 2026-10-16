#pragma once

#include "display_protection_config.h"
#include "display_brightness_arbiter.h"
#include "display_protection_diagnostics.h"
#include "display_protection_logic.h"

#include <hal/button_input_gate.h>
#include <hal/external_power_state.h>
#include <cstdint>
#include <lvgl.h>

struct DisplayProtectionSnapshot {
    DisplayProtectionState state = DisplayProtectionState::Active;
    int effective_brightness = 0;
    int restore_brightness = 80;
    bool automatic_enabled = true;
    uint8_t reason_mask = 0;
    DisplayProtectionConfig config = defaultDisplayProtectionConfig();
    ExternalPowerSnapshot external_power{};
    DisplayProtectionTimingSnapshot timing{};
    DisplayWakeReason last_wake_reason = DisplayWakeReason::Initialization;
};

class GlobalDisplayProtectionService {
public:
    bool init();
    void update();
    void applyPendingHardwareUpdate();
    void shutdown();
    void setKeepAwake(DisplayKeepAwakeReason reason, bool active);
    const DisplayProtectionConfig& config() const;
    void applyConfig(const DisplayProtectionConfig& config, bool persist);
    void screenOffNow();
    void wakeFromDeferredButton();
    void setRightButtonWakeDeferred(bool deferred);
    void applyUserBrightness(int brightness, bool persist);
    bool touchInputSuppressed() const;
    DisplayProtectionState state() const;
    DisplayProtectionSnapshot snapshot() const;

private:
    void acceptDecision(const DisplayProtectionDecision& decision,
                        bool restore_display = true);
    void suppressWakeButton(bool button_pressed);
    void completeTouchGateReleaseAfterHardwareUpdate();
    void syncUserBrightness();
    bool automaticProtectionEnabledForPolicy() const;
    void recalculateAutomaticProtection(uint32_t now_ms);
    void restoreAndReset(uint32_t now_ms, DisplayWakeReason reason);
    // Called only while holding the LVGL lock, unlike the policy entry points.
    void syncDisplayRendering();
    void releaseDisplayRendering();
    static void displayRenderingEvent(lv_event_t* event);

    DisplayProtectionLogic _logic;
    DisplayProtectionConfig _config = defaultDisplayProtectionConfig();
    ExternalPowerSnapshot _external_power{};
    DisplayWakeReason _last_wake_reason = DisplayWakeReason::Initialization;
    int _restore_brightness = 80;
    DisplayBrightnessArbiter _brightness_arbiter;
    int _pending_brightness = -1;
    bool _touch_gate_release_pending = false;
    uint32_t _brightness_revision = 0;
    ExternalPowerHealthLogPolicy _power_health_log_policy;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    ButtonPhysicalState _last_buttons{};
    bool _defer_right_button_wake = false;
    bool _initialized = false;
    lv_display_t* _render_display = nullptr;
    bool _render_suspended = false;
};

GlobalDisplayProtectionService& GetGlobalDisplayProtectionService();
