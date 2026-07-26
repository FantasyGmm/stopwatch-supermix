#include "global_display_protection_service.h"

#include <hal/hal.h>
#include <mooncake_log.h>

namespace {
constexpr std::string_view tag = "DisplayProtection";
GlobalDisplayProtectionService global_display_protection_service;
}

GlobalDisplayProtectionService& GetGlobalDisplayProtectionService()
{
    return global_display_protection_service;
}

bool GlobalDisplayProtectionService::init()
{
    shutdown();

    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        mclog::tagError(tag, "LVGL input unavailable");
        return false;
    }

    const uint32_t now_ms = GetHAL().millis();
    _logic.reset(now_ms);
    _restore_brightness = GetHAL().getUserBackLightBrightness();
    _brightness_revision = GetHAL().backLightBrightnessRevision();
    _last_touch_state = lv_indev_get_state(indev);
    _last_buttons = GetHAL().readButtonPhysicalState();
    _initialized = true;
    return true;
}

void GlobalDisplayProtectionService::update()
{
    if (!_initialized) {
        return;
    }

    syncUserBrightness();

    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    const ButtonPhysicalState buttons = GetHAL().readButtonPhysicalState();
    const lv_indev_state_t touch_state = lv_indev_get_state(indev);
    const bool button_pressed =
        (buttons.a_pressed && !_last_buttons.a_pressed) ||
        (buttons.b_pressed && !_last_buttons.b_pressed);
    const bool touch_pressed =
        touch_state == LV_INDEV_STATE_PR && _last_touch_state == LV_INDEV_STATE_REL;

    if (button_pressed || touch_pressed) {
        const DisplayProtectionDecision activity = _logic.userActivity(now_ms);
        if (activity.consume_input) {
            if (button_pressed) {
                GetHAL().suppressButtonInputUntilRelease();
            }
            if (touch_pressed) {
                lv_indev_wait_release(indev);
                lv_indev_reset(indev, nullptr);
            }
        }
        acceptDecision(activity);
    }

    acceptDecision(_logic.update(now_ms));
    _last_buttons = buttons;
    _last_touch_state = touch_state;
}

void GlobalDisplayProtectionService::applyPendingHardwareUpdate()
{
    if (_pending_brightness < 0) {
        return;
    }
    GetHAL().setTemporaryBackLightBrightness(_pending_brightness);
    _pending_brightness = -1;
}

void GlobalDisplayProtectionService::shutdown()
{
    _logic.reset(0);
    _restore_brightness = 80;
    _pending_brightness = -1;
    _brightness_revision = 0;
    _last_touch_state = LV_INDEV_STATE_REL;
    _last_buttons = {};
    _initialized = false;
}

void GlobalDisplayProtectionService::setAutomaticProtection(bool enabled,
                                                             bool restore_display)
{
    if (!_initialized) {
        return;
    }
    acceptDecision(
        _logic.setAutomaticEnabled(enabled, GetHAL().millis()), restore_display);
}

void GlobalDisplayProtectionService::setKeepAwake(DisplayKeepAwakeReason reason,
                                                   bool active)
{
    if (!_initialized) {
        return;
    }
    syncUserBrightness();
    const uint8_t reason_bit = static_cast<uint8_t>(reason);
    const bool was_active = (_logic.reasonMask() & reason_bit) != 0;
    acceptDecision(_logic.setKeepAwake(reason, active, GetHAL().millis()));
    if (active && GetHAL().getBackLightBrightness() <= 0 && !was_active) {
        _pending_brightness = _restore_brightness;
    }
}

DisplayProtectionSnapshot GlobalDisplayProtectionService::snapshot() const
{
    return {
        .state = _logic.state(),
        .effective_brightness = GetHAL().getBackLightBrightness(),
        .restore_brightness = _restore_brightness,
        .automatic_enabled = _logic.automaticEnabled(),
        .reason_mask = _logic.reasonMask(),
    };
}

void GlobalDisplayProtectionService::acceptDecision(
    const DisplayProtectionDecision& decision, bool restore_display)
{
    switch (decision.action) {
        case DisplayProtectionAction::Restore:
            if (restore_display) {
                _pending_brightness = _restore_brightness;
            }
            break;
        case DisplayProtectionAction::Dim:
            _pending_brightness =
                DisplayProtectionLogic::dimBrightness(_restore_brightness);
            break;
        case DisplayProtectionAction::ScreenOff:
            _pending_brightness = 0;
            break;
        case DisplayProtectionAction::None:
            return;
    }

    mclog::tagInfo(tag, "state={} action={} reason_mask={}",
                   static_cast<unsigned>(_logic.state()),
                   static_cast<unsigned>(decision.action),
                   static_cast<unsigned>(_logic.reasonMask()));
}

void GlobalDisplayProtectionService::syncUserBrightness()
{
    const uint32_t revision = GetHAL().backLightBrightnessRevision();
    if (_brightness_revision == revision) {
        return;
    }
    _brightness_revision = revision;
    _restore_brightness = GetHAL().getUserBackLightBrightness();
}
