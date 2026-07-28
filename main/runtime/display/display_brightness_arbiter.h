#pragma once

#include "display_protection_logic.h"

#include <algorithm>

class DisplayBrightnessArbiter {
public:
    void reset(int user_brightness, DisplayProtectionState state)
    {
        _user_brightness = std::clamp(user_brightness, 0, 100);
        _state = state;
    }

    void setDisplayState(DisplayProtectionState state)
    {
        _state = state;
    }

    void setUserBrightness(int brightness)
    {
        _user_brightness = std::clamp(brightness, 0, 100);
    }

    int userBrightness() const
    {
        return _user_brightness;
    }

    int effectiveBrightness() const
    {
        if (_state == DisplayProtectionState::ScreenOff) {
            return 0;
        }
        if (_state == DisplayProtectionState::Dimmed) {
            return DisplayProtectionLogic::dimBrightness(_user_brightness);
        }
        return _user_brightness;
    }

private:
    int _user_brightness = 80;
    DisplayProtectionState _state = DisplayProtectionState::Active;
};
