#include "runtime/power/low_battery_alert_logic.h"

LowBatteryAlertActions LowBatteryAlertLogic::update(const LowBatteryAlertInput& input)
{
    LowBatteryAlertActions actions;

    if (input.external_power) {
        rearm(actions);
    } else if (input.battery_sample_due) {
        if (input.battery_sample_valid) {
            if (input.battery_percent >= rearmThreshold) {
                rearm(actions);
            } else if (input.battery_percent < criticalThreshold &&
                       !_critical_alerted) {
                _consecutive_low_samples = 0;
                if (_consecutive_critical_samples < confirmationSamples) {
                    ++_consecutive_critical_samples;
                }
                if (_consecutive_critical_samples >= confirmationSamples) {
                    armAlert(input.battery_percent, true);
                }
            } else {
                _consecutive_critical_samples = 0;
                if (input.battery_percent < lowThreshold && !_episode_active) {
                    if (_consecutive_low_samples < confirmationSamples) {
                        ++_consecutive_low_samples;
                    }
                    if (_consecutive_low_samples >= confirmationSamples) {
                        armAlert(input.battery_percent, false);
                    }
                } else if (!_episode_active) {
                    _consecutive_low_samples = 0;
                }
            }
        }
    }

    if (_text_visible && !input.display_visible) {
        actions.hide_text = true;
        _text_visible = false;
        _text_pending = _episode_active;
    } else if (_text_visible &&
               static_cast<uint32_t>(input.now_ms - _text_visible_since_ms) >=
                   textVisibleMs) {
        actions.hide_text = true;
        _text_visible = false;
    }

    if (_episode_active && _text_pending && input.display_visible) {
        actions.show_text = true;
        actions.battery_percent = _alert_percent;
        _text_pending = false;
        _text_visible = true;
        _text_visible_since_ms = input.now_ms;
    }

    if (_episode_active && _sound_pending) {
        if (!input.speaker_audible) {
            _sound_pending = false;
        } else if (input.playback_available) {
            actions.play_sound = true;
            _sound_pending = false;
        }
    }
    refreshPhase();
    return actions;
}

void LowBatteryAlertLogic::deferSound()
{
    if (_episode_active) {
        _sound_pending = true;
        refreshPhase();
    }
}

void LowBatteryAlertLogic::rearm(LowBatteryAlertActions& actions)
{
    if (_text_visible) {
        actions.hide_text = true;
    }
    _phase = LowBatteryAlertPhase::Armed;
    _consecutive_low_samples = 0;
    _consecutive_critical_samples = 0;
    _alert_percent = 0;
    _episode_active = false;
    _critical_alerted = false;
    _sound_pending = false;
    _text_pending = false;
    _text_visible = false;
    _text_visible_since_ms = 0;
}

void LowBatteryAlertLogic::armAlert(
    uint8_t battery_percent,
    bool critical)
{
    _episode_active = true;
    _critical_alerted = _critical_alerted || critical;
    _sound_pending = true;
    _text_pending = true;
    _alert_percent = battery_percent;
    _consecutive_low_samples = 0;
    _consecutive_critical_samples = 0;
}

void LowBatteryAlertLogic::refreshPhase()
{
    if (!_episode_active) {
        _phase = LowBatteryAlertPhase::Armed;
    } else if (_sound_pending || _text_pending) {
        _phase = LowBatteryAlertPhase::LowPending;
    } else {
        _phase = LowBatteryAlertPhase::LowAlerted;
    }
}
