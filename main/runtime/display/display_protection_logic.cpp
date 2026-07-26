#include "display_protection_logic.h"

#include <algorithm>
#include <limits>

void DisplayProtectionLogic::reset(uint32_t now_ms)
{
    _state = DisplayProtectionState::Active;
    _inactivity_origin_ms = now_ms;
    _reason_mask = 0;
    _automatic_enabled = true;
}

DisplayProtectionDecision DisplayProtectionLogic::update(uint32_t now_ms)
{
    if (!_automatic_enabled || heldAwake()) {
        return {};
    }

    const uint32_t elapsed_ms = now_ms - _inactivity_origin_ms;
    if (elapsed_ms >= _screen_off_after_ms &&
        _state != DisplayProtectionState::ScreenOff) {
        _state = DisplayProtectionState::ScreenOff;
        return {DisplayProtectionAction::ScreenOff, false};
    }
    if (elapsed_ms >= _dim_after_ms && _state == DisplayProtectionState::Active) {
        _state = DisplayProtectionState::Dimmed;
        return {DisplayProtectionAction::Dim, false};
    }
    return {};
}

DisplayProtectionDecision DisplayProtectionLogic::userActivity(uint32_t now_ms)
{
    const bool consume_input = _state == DisplayProtectionState::ScreenOff;
    if (_state != DisplayProtectionState::Active) {
        return restore(now_ms, consume_input);
    }
    _inactivity_origin_ms = now_ms;
    return {};
}

DisplayProtectionDecision DisplayProtectionLogic::setAutomaticEnabled(bool enabled,
                                                                       uint32_t now_ms)
{
    if (_automatic_enabled == enabled) {
        return {};
    }
    _automatic_enabled = enabled;
    if (enabled) {
        _inactivity_origin_ms = now_ms;
        return {};
    }
    if (_state != DisplayProtectionState::Active) {
        return restore(now_ms, false);
    }
    return {};
}

DisplayProtectionDecision DisplayProtectionLogic::setTimeouts(uint32_t dim_after_ms,
                                                               uint32_t screen_off_after_ms,
                                                               uint32_t now_ms)
{
    if (screen_off_after_ms <= dim_after_ms) {
        if (dim_after_ms == std::numeric_limits<uint32_t>::max()) {
            dim_after_ms -= 1U;
            screen_off_after_ms = std::numeric_limits<uint32_t>::max();
        } else {
            screen_off_after_ms = dim_after_ms + 1U;
        }
    }

    _dim_after_ms = dim_after_ms;
    _screen_off_after_ms = screen_off_after_ms;
    _inactivity_origin_ms = now_ms;
    if (_state != DisplayProtectionState::Active) {
        return restore(now_ms, false);
    }
    return {};
}

DisplayProtectionDecision DisplayProtectionLogic::screenOffNow(uint32_t now_ms)
{
    _inactivity_origin_ms = now_ms;
    _state = DisplayProtectionState::ScreenOff;
    return {DisplayProtectionAction::ScreenOff, false};
}

DisplayProtectionDecision DisplayProtectionLogic::setKeepAwake(
    DisplayKeepAwakeReason reason, bool active, uint32_t now_ms)
{
    const uint8_t bit = static_cast<uint8_t>(reason);
    const uint8_t old_mask = _reason_mask;
    const bool was_active = (old_mask & bit) != 0;
    if (active) {
        _reason_mask |= bit;
    } else {
        _reason_mask &= static_cast<uint8_t>(~bit);
    }

    if (_reason_mask == old_mask) {
        return {};
    }
    if (active && !was_active && _state != DisplayProtectionState::Active) {
        return restore(now_ms, false);
    }
    if (!active && _reason_mask == 0) {
        _inactivity_origin_ms = now_ms;
    }
    return {};
}

DisplayProtectionTimingSnapshot DisplayProtectionLogic::timingSnapshot(uint32_t now_ms) const
{
    const uint32_t inactivity_ms = now_ms - _inactivity_origin_ms;
    return {inactivity_ms,
            _dim_after_ms,
            _screen_off_after_ms,
            inactivity_ms >= _dim_after_ms ? 0U : _dim_after_ms - inactivity_ms,
            inactivity_ms >= _screen_off_after_ms ? 0U
                                                  : _screen_off_after_ms - inactivity_ms};
}

DisplayProtectionState DisplayProtectionLogic::state() const
{
    return _state;
}

bool DisplayProtectionLogic::automaticEnabled() const
{
    return _automatic_enabled;
}

uint8_t DisplayProtectionLogic::reasonMask() const
{
    return _reason_mask;
}

int DisplayProtectionLogic::dimBrightness(int user_brightness)
{
    return std::min(std::clamp(user_brightness, 10, 100), 20);
}

DisplayProtectionDecision DisplayProtectionLogic::restore(uint32_t now_ms,
                                                           bool consume_input)
{
    _state = DisplayProtectionState::Active;
    _inactivity_origin_ms = now_ms;
    return {DisplayProtectionAction::Restore, consume_input};
}

bool DisplayProtectionLogic::heldAwake() const
{
    return _reason_mask != 0;
}
