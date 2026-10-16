#include "global_display_protection_service.h"
#include "display_button_wake_policy.h"

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
        loaded.dim_enabled =
            settings.GetBool("dim_on", defaults.dim_enabled);
        loaded.screen_off_enabled =
            settings.GetBool("off_on", defaults.screen_off_enabled);
        _config = sanitizeDisplayProtectionConfig(loaded);
    }

    _restore_brightness = GetHAL().getUserBackLightBrightness();
    _brightness_revision = GetHAL().backLightBrightnessRevision();
    _logic.reset(now_ms);
    _brightness_arbiter.reset(_restore_brightness, _logic.state());
    acceptDecision(_logic.setPolicy(
        DisplayProtectionPolicy{
            _config.dim_enabled,
            _config.screen_off_enabled,
            displayProtectionDimMs(_config),
            displayProtectionScreenOffMs(_config),
        },
        now_ms));
    _external_power = GetHAL().getExternalPowerSnapshot();
    _power_health_log_policy.reset(_external_power.latest_sample_valid, now_ms);
    recalculateAutomaticProtection(now_ms);
    _last_touch_state = lv_indev_get_state(indev);
    GetHAL().prepareTouchInputForActiveDisplay();
    _last_buttons = GetHAL().readButtonPhysicalState();
    _last_wake_reason = DisplayWakeReason::Initialization;
    _initialized = true;
    syncDisplayRendering();
    return true;
}

void GlobalDisplayProtectionService::update()
{
    if (!_initialized) {
        return;
    }

    syncDisplayRendering();
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

    const ExternalPowerHealthLogEvent health_event =
        _power_health_log_policy.update(latest_power.latest_sample_valid,
                                        now_ms);
    switch (health_event) {
        case ExternalPowerHealthLogEvent::Healthy:
            mclog::tagInfo(tag, "power sample healthy state={}",
                           static_cast<unsigned>(latest_power.state));
            break;
        case ExternalPowerHealthLogEvent::Unhealthy:
            mclog::tagWarn(tag, "power sample unhealthy state={} retry_ms={}",
                           static_cast<unsigned>(latest_power.state),
                           ExternalPowerHealthLogPolicy::RetryMs);
            break;
        case ExternalPowerHealthLogEvent::None:
            break;
    }

    if (accepted_power_changed) {
        mclog::tagInfo(tag,
                       "power transition previous={} current={} revision={}",
                       static_cast<unsigned>(previous_power.state),
                       static_cast<unsigned>(latest_power.state),
                       static_cast<unsigned>(latest_power.revision));
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
        syncDisplayRendering();
        return;
    }

    const ButtonPhysicalState buttons = GetHAL().readButtonPhysicalState();
    const lv_indev_state_t touch_state = lv_indev_get_state(indev);
    const bool touch_wake_requested = GetHAL().consumeTouchWakeRequest();
    const bool left_pressed_edge =
        buttons.a_pressed && !_last_buttons.a_pressed;
    const bool right_pressed_edge =
        buttons.b_pressed && !_last_buttons.b_pressed;
    const bool right_wake_deferred = shouldDeferRightButtonWake(
        display_was_off,
        buttons.a_pressed,
        right_pressed_edge,
        _defer_right_button_wake);
    const bool button_pressed =
        left_pressed_edge || (right_pressed_edge && !right_wake_deferred);
    const bool touch_pressed =
        touch_state == LV_INDEV_STATE_PR && _last_touch_state == LV_INDEV_STATE_REL;

    if (button_pressed || touch_wake_requested || touch_pressed) {
        const DisplayProtectionDecision activity = _logic.userActivity(now_ms);
        if ((display_was_off || activity.consume_input) && button_pressed) {
            suppressWakeButton(true);
        }
        _last_wake_reason = button_pressed ? DisplayWakeReason::Button
                                           : DisplayWakeReason::Touch;
        acceptDecision(activity);
    }

    acceptDecision(_logic.update(now_ms));
    _last_buttons = buttons;
    _last_touch_state = touch_state;
    syncDisplayRendering();
}

void GlobalDisplayProtectionService::applyPendingHardwareUpdate()
{
    if (_pending_brightness < 0) {
        return;
    }
    GetHAL().setTemporaryBackLightBrightness(_pending_brightness);
    _pending_brightness = -1;
    completeTouchGateReleaseAfterHardwareUpdate();
}

void GlobalDisplayProtectionService::shutdown()
{
    _logic.reset(0);
    releaseDisplayRendering();
    _config = defaultDisplayProtectionConfig();
    _external_power = {};
    _last_wake_reason = DisplayWakeReason::Initialization;
    _restore_brightness = 80;
    _brightness_arbiter.reset(80, DisplayProtectionState::Active);
    _pending_brightness = -1;
    _touch_gate_release_pending = false;
    _brightness_revision = 0;
    _power_health_log_policy = {};
    GetHAL().prepareTouchInputForActiveDisplay();
    _last_touch_state = LV_INDEV_STATE_REL;
    _last_buttons = {};
    _defer_right_button_wake = false;
    _initialized = false;
    GetHAL().setLvglScreenOffMode(false);
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
    if (!active && was_active && _logic.reasonMask() == 0) {
        _last_wake_reason = DisplayWakeReason::KeepAwakeReleased;
    }
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
        settings.SetBool("dim_on", sanitized.dim_enabled);
        settings.SetBool("off_on", sanitized.screen_off_enabled);
        mclog::tagInfo(tag,
                       "configuration committed dim_choice={} off_choice={} "
                       "charging_guard={} dim_enabled={} off_enabled={}",
                       static_cast<unsigned>(sanitized.dim_choice),
                       static_cast<unsigned>(sanitized.screen_off_choice),
                       static_cast<unsigned>(sanitized.protect_while_charging),
                       static_cast<unsigned>(sanitized.dim_enabled),
                       static_cast<unsigned>(sanitized.screen_off_enabled));
    } else {
        mclog::tagInfo(tag,
                       "configuration applied dim_choice={} off_choice={} "
                       "charging_guard={} dim_enabled={} off_enabled={}",
                       static_cast<unsigned>(sanitized.dim_choice),
                       static_cast<unsigned>(sanitized.screen_off_choice),
                       static_cast<unsigned>(sanitized.protect_while_charging),
                       static_cast<unsigned>(sanitized.dim_enabled),
                       static_cast<unsigned>(sanitized.screen_off_enabled));
    }

    _config = sanitized;
    if (!_initialized) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    syncUserBrightness();
    _last_wake_reason = DisplayWakeReason::ConfigurationChanged;
    acceptDecision(_logic.setPolicy(
        DisplayProtectionPolicy{
            _config.dim_enabled,
            _config.screen_off_enabled,
            displayProtectionDimMs(_config),
            displayProtectionScreenOffMs(_config),
        },
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

void GlobalDisplayProtectionService::wakeFromDeferredButton()
{
    if (!_initialized || _logic.state() != DisplayProtectionState::ScreenOff) {
        return;
    }
    restoreAndReset(GetHAL().millis(), DisplayWakeReason::Button);
}

void GlobalDisplayProtectionService::setRightButtonWakeDeferred(bool deferred)
{
    _defer_right_button_wake = deferred;
}

void GlobalDisplayProtectionService::applyUserBrightness(int brightness,
                                                          bool persist)
{
    GetHAL().setBackLightBrightness(brightness, persist);
    _brightness_revision = GetHAL().backLightBrightnessRevision();
    _brightness_arbiter.setUserBrightness(
        GetHAL().getUserBackLightBrightness());
    _restore_brightness = _brightness_arbiter.userBrightness();

    // HAL's user-brightness write also updates the panel. Re-apply the
    // protection state's temporary level immediately so ScreenOff always wins.
    GetHAL().setTemporaryBackLightBrightness(
        _brightness_arbiter.effectiveBrightness());
    _pending_brightness = -1;
    completeTouchGateReleaseAfterHardwareUpdate();
}

bool GlobalDisplayProtectionService::touchInputSuppressed() const
{
    return _initialized && GetHAL().touchInputSuppressed();
}

DisplayProtectionState GlobalDisplayProtectionService::state() const
{
    return _logic.state();
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
    if (decision.action != DisplayProtectionAction::None) {
        _brightness_arbiter.setDisplayState(_logic.state());
    }
    switch (decision.action) {
        case DisplayProtectionAction::Restore:
            GetHAL().setLvglScreenOffMode(false);
            syncUserBrightness();
            if (restore_display) {
                _pending_brightness =
                    _brightness_arbiter.effectiveBrightness();
                _touch_gate_release_pending = true;
            }
            break;
        case DisplayProtectionAction::Dim:
            GetHAL().setLvglScreenOffMode(false);
            _pending_brightness = _brightness_arbiter.effectiveBrightness();
            break;
        case DisplayProtectionAction::ScreenOff:
            GetHAL().setLvglScreenOffMode(true);
            GetHAL().armTouchWakeInput();
            _touch_gate_release_pending = false;
            _pending_brightness = _brightness_arbiter.effectiveBrightness();
            break;
        case DisplayProtectionAction::None:
            return;
    }

    mclog::tagInfo(tag, "display action state={} action={} reason_mask={}",
                   static_cast<unsigned>(_logic.state()),
                   static_cast<unsigned>(decision.action),
                   static_cast<unsigned>(_logic.reasonMask()));
}

void GlobalDisplayProtectionService::suppressWakeButton(bool button_pressed)
{
    if (button_pressed) {
        GetHAL().suppressButtonInputUntilRelease();
    }
}

void GlobalDisplayProtectionService::completeTouchGateReleaseAfterHardwareUpdate()
{
    if (!_touch_gate_release_pending) {
        return;
    }
    GetHAL().releaseTouchWakeInputAfterNextSample();
    _touch_gate_release_pending = false;
}

void GlobalDisplayProtectionService::syncUserBrightness()
{
    const uint32_t revision = GetHAL().backLightBrightnessRevision();
    if (_brightness_revision == revision) {
        return;
    }
    _brightness_revision = revision;
    _restore_brightness = GetHAL().getUserBackLightBrightness();
    _brightness_arbiter.setUserBrightness(_restore_brightness);
    _restore_brightness = _brightness_arbiter.userBrightness();
    if (_logic.state() != DisplayProtectionState::Active) {
        _pending_brightness = _brightness_arbiter.effectiveBrightness();
    }
}

bool GlobalDisplayProtectionService::automaticProtectionEnabledForPolicy() const
{
    return (_config.dim_enabled || _config.screen_off_enabled) &&
           (_config.protect_while_charging ||
            _external_power.state != ExternalPowerState::ExternalPower);
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

void GlobalDisplayProtectionService::syncDisplayRendering()
{
    lv_display_t* display = lv_display_get_default();
    if (_render_display != display) {
        releaseDisplayRendering();
        _render_display = display;
        if (_render_display != nullptr) {
            lv_display_add_event_cb(
                _render_display, displayRenderingEvent, LV_EVENT_ALL, this);
        }
    }
    if (_render_display == nullptr) {
        return;
    }

    const bool screen_off = _logic.state() == DisplayProtectionState::ScreenOff;
    if (screen_off == _render_suspended) {
        return;
    }

    _render_suspended = screen_off;
    lv_timer_t* timer = lv_display_get_refr_timer(_render_display);
    if (screen_off) {
        // Balance exactly one invalidation disable on wake. Timers and input
        // sampling remain alive, but invisible animations cannot request draws.
        lv_display_enable_invalidation(_render_display, false);
        if (timer != nullptr) {
            lv_timer_pause(timer);
        }
        return;
    }

    lv_display_enable_invalidation(_render_display, true);
    // Models/widgets can change while off. Repaint their latest state once,
    // without waiting for a new BLE revision to arrive after wake.
    if (lv_obj_t* screen = lv_display_get_screen_active(_render_display)) {
        lv_obj_invalidate(screen);
    }
    if (timer != nullptr) {
        lv_timer_resume(timer);
        lv_timer_ready(timer);
    }
}

void GlobalDisplayProtectionService::releaseDisplayRendering()
{
    if (_render_display == nullptr) {
        return;
    }
    lv_display_remove_event_cb_with_user_data(
        _render_display, displayRenderingEvent, this);
    if (_render_suspended) {
        lv_display_enable_invalidation(_render_display, true);
        if (lv_timer_t* timer = lv_display_get_refr_timer(_render_display)) {
            lv_timer_resume(timer);
        }
    }
    _render_display = nullptr;
    _render_suspended = false;
}

void GlobalDisplayProtectionService::displayRenderingEvent(lv_event_t* event)
{
    auto* self = static_cast<GlobalDisplayProtectionService*>(
        lv_event_get_user_data(event));
    if (self == nullptr || lv_event_get_target(event) != self->_render_display) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        self->_render_display = nullptr;
        self->_render_suspended = false;
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_REFR_REQUEST ||
        !self->_render_suspended) {
        return;
    }

    // Layout changes can resume the refresh timer even when invalidation is
    // disabled. Keep those requests asleep too; do not stop the input timers.
    if (lv_timer_t* timer = lv_display_get_refr_timer(self->_render_display)) {
        lv_timer_pause(timer);
    }
    lv_event_stop_processing(event);
}
