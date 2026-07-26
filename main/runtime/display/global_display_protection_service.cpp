#include "global_display_protection_service.h"

#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>

#include <algorithm>

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
    const DisplayProtectionConfig defaults = defaultDisplayProtectionConfig();
    {
        Settings settings("disp_guard");
        const int32_t dim_choice =
            settings.GetInt("dim_idx", static_cast<int32_t>(defaults.dim_choice));
        const int32_t screen_off_choice =
            settings.GetInt("off_idx",
                            static_cast<int32_t>(defaults.screen_off_choice));
        DisplayProtectionConfig loaded = defaults;
        loaded.dim_choice = static_cast<uint8_t>(std::clamp<int32_t>(
            dim_choice, 0, DisplayProtectionDimChoiceCount - 1));
        loaded.screen_off_choice = static_cast<uint8_t>(std::clamp<int32_t>(
            screen_off_choice, 0, DisplayProtectionScreenOffChoiceCount - 1));
        loaded.protect_while_charging =
            settings.GetBool("chg_guard", defaults.protect_while_charging);
        _config = sanitizeDisplayProtectionConfig(loaded);
    }

    _restore_brightness = GetHAL().getUserBackLightBrightness();
    _brightness_revision = GetHAL().backLightBrightnessRevision();
    _logic.reset(now_ms);
    acceptDecision(_logic.setTimeouts(displayProtectionDimMs(_config),
                                      displayProtectionScreenOffMs(_config),
                                      now_ms));
    _external_power = GetHAL().getExternalPowerSnapshot();
    recalculateAutomaticProtection(now_ms);
    _last_touch_state = lv_indev_get_state(indev);
    _last_buttons = GetHAL().readButtonPhysicalState();
    _last_wake_reason = DisplayWakeReason::Initialization;
    _initialized = true;
    return true;
}

void GlobalDisplayProtectionService::update()
{
    if (!_initialized) {
        return;
    }

    const bool display_was_off =
        _logic.state() == DisplayProtectionState::ScreenOff;
    syncUserBrightness();

    const uint32_t now_ms = GetHAL().millis();
    const ExternalPowerSnapshot previous_power = _external_power;
    const ExternalPowerSnapshot latest_power =
        GetHAL().getExternalPowerSnapshot();
    const bool accepted_power_changed =
        latest_power.revision != previous_power.revision ||
        latest_power.state != previous_power.state;
    _external_power = latest_power;

    if (accepted_power_changed) {
        const bool inserted =
            latest_power.state == ExternalPowerState::ExternalPower &&
            previous_power.state != ExternalPowerState::ExternalPower;
        const bool removed =
            previous_power.state == ExternalPowerState::ExternalPower &&
            latest_power.state == ExternalPowerState::Battery;
        if (inserted) {
            restoreAndReset(now_ms, DisplayWakeReason::PowerInserted);
        } else if (removed) {
            restoreAndReset(now_ms, DisplayWakeReason::PowerRemoved);
        } else {
            recalculateAutomaticProtection(now_ms);
        }
    }

    lv_indev_t* indev = GetHAL().lvTouchpad;
    if (indev == nullptr) {
        acceptDecision(_logic.update(now_ms));
        return;
    }

    const ButtonPhysicalState buttons = GetHAL().readButtonPhysicalState();
    const lv_indev_state_t touch_state = lv_indev_get_state(indev);
    const bool button_pressed =
        (buttons.a_pressed && !_last_buttons.a_pressed) ||
        (buttons.b_pressed && !_last_buttons.b_pressed);
    const bool touch_pressed =
        touch_state == LV_INDEV_STATE_PR && _last_touch_state == LV_INDEV_STATE_REL;

    if (button_pressed || touch_pressed) {
        const DisplayProtectionDecision activity = _logic.userActivity(now_ms);
        if (display_was_off || activity.consume_input) {
            suppressWakeInput(indev, button_pressed, touch_pressed);
        }
        _last_wake_reason = button_pressed ? DisplayWakeReason::Button
                                           : DisplayWakeReason::Touch;
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
    _config = defaultDisplayProtectionConfig();
    _external_power = {};
    _last_wake_reason = DisplayWakeReason::Initialization;
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
    syncUserBrightness();
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
    if (active && !was_active) {
        _last_wake_reason = DisplayWakeReason::KeepAwakeActivated;
    }
    acceptDecision(_logic.setKeepAwake(reason, active, GetHAL().millis()));
    if (active && GetHAL().getBackLightBrightness() <= 0 && !was_active) {
        _pending_brightness = _restore_brightness;
    }
}

const DisplayProtectionConfig& GlobalDisplayProtectionService::config() const
{
    return _config;
}

void GlobalDisplayProtectionService::applyConfig(
    const DisplayProtectionConfig& config, bool persist)
{
    const DisplayProtectionConfig sanitized =
        sanitizeDisplayProtectionConfig(config);
    if (persist) {
        Settings settings("disp_guard", true);
        settings.SetInt("dim_idx", sanitized.dim_choice);
        settings.SetInt("off_idx", sanitized.screen_off_choice);
        settings.SetBool("chg_guard", sanitized.protect_while_charging);
    }

    _config = sanitized;
    if (!_initialized) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    syncUserBrightness();
    _last_wake_reason = DisplayWakeReason::ConfigurationChanged;
    acceptDecision(_logic.setTimeouts(displayProtectionDimMs(_config),
                                      displayProtectionScreenOffMs(_config),
                                      now_ms));
    _pending_brightness = _restore_brightness;
    recalculateAutomaticProtection(now_ms);
}

void GlobalDisplayProtectionService::screenOffNow()
{
    if (!_initialized) {
        return;
    }
    acceptDecision(_logic.screenOffNow(GetHAL().millis()));
}

DisplayProtectionSnapshot GlobalDisplayProtectionService::snapshot() const
{
    return {
        .state = _logic.state(),
        .effective_brightness = GetHAL().getBackLightBrightness(),
        .restore_brightness = _restore_brightness,
        .automatic_enabled =
            _logic.automaticEnabled() && _logic.reasonMask() == 0,
        .reason_mask = _logic.reasonMask(),
        .config = _config,
        .external_power = _external_power,
        .timing = _logic.timingSnapshot(GetHAL().millis()),
        .last_wake_reason = _last_wake_reason,
    };
}

void GlobalDisplayProtectionService::acceptDecision(
    const DisplayProtectionDecision& decision, bool restore_display)
{
    switch (decision.action) {
        case DisplayProtectionAction::Restore:
            syncUserBrightness();
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

void GlobalDisplayProtectionService::suppressWakeInput(
    lv_indev_t* indev, bool button_pressed, bool touch_pressed)
{
    if (button_pressed) {
        GetHAL().suppressButtonInputUntilRelease();
    }
    if (touch_pressed) {
        lv_indev_wait_release(indev);
        lv_indev_reset(indev, nullptr);
    }
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

bool GlobalDisplayProtectionService::automaticProtectionEnabledForPolicy() const
{
    return _config.protect_while_charging || _external_power.state != ExternalPowerState::ExternalPower;
}

void GlobalDisplayProtectionService::recalculateAutomaticProtection(
    uint32_t now_ms)
{
    syncUserBrightness();
    acceptDecision(_logic.setAutomaticEnabled(
        automaticProtectionEnabledForPolicy(), now_ms));
}

void GlobalDisplayProtectionService::restoreAndReset(
    uint32_t now_ms, DisplayWakeReason reason)
{
    syncUserBrightness();
    acceptDecision(_logic.userActivity(now_ms));
    _pending_brightness = _restore_brightness;
    _last_wake_reason = reason;
    recalculateAutomaticProtection(now_ms);
}
