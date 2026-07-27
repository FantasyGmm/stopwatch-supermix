#pragma once

#include <runtime/display/display_protection_config.h>

class DisplayProtectionSettingsModel {
public:
    static constexpr int DimSliderMax = DisplayProtectionDimChoiceCount;
    static constexpr int ScreenOffSliderMax = DisplayProtectionScreenOffChoiceCount;

    explicit DisplayProtectionSettingsModel(DisplayProtectionConfig config)
        : _config(sanitizeDisplayProtectionConfig(config))
    {
    }

    const DisplayProtectionConfig& config() const
    {
        return _config;
    }

    int dimSliderPosition() const
    {
        return _config.dim_enabled ? _config.dim_choice + 1 : 0;
    }

    int screenOffSliderPosition() const
    {
        return _config.screen_off_enabled ? _config.screen_off_choice + 1 : 0;
    }

    std::string_view dimLabel() const
    {
        return _config.dim_enabled ? displayProtectionDimLabel(_config.dim_choice)
                                   : std::string_view{"Off"};
    }

    std::string_view screenOffLabel() const
    {
        return _config.screen_off_enabled
                   ? displayProtectionScreenOffLabel(_config.screen_off_choice)
                   : std::string_view{"Off"};
    }

    void setDimSliderPosition(int position)
    {
        if (position <= 0) {
            _config.dim_enabled = false;
            return;
        }
        _config.dim_enabled = true;
        _config = withDisplayProtectionDimChoice(_config, position - 1);
    }

    void setScreenOffSliderPosition(int position)
    {
        if (position <= 0) {
            _config.screen_off_enabled = false;
            return;
        }
        _config.screen_off_enabled = true;
        _config = withDisplayProtectionScreenOffChoice(_config, position - 1);
    }

    void setDimChoice(int choice)
    {
        setDimSliderPosition(choice + 1);
    }

    void setScreenOffChoice(int choice)
    {
        setScreenOffSliderPosition(choice + 1);
    }

    void setProtectWhileCharging(bool enabled)
    {
        _config.protect_while_charging = enabled;
    }

private:
    DisplayProtectionConfig _config;
};
