#pragma once

#include <runtime/display/display_protection_config.h>

class DisplayProtectionSettingsModel {
public:
    explicit DisplayProtectionSettingsModel(DisplayProtectionConfig config)
        : _config(sanitizeDisplayProtectionConfig(config))
    {
    }

    const DisplayProtectionConfig& config() const
    {
        return _config;
    }

    void setDimChoice(int choice)
    {
        _config = withDisplayProtectionDimChoice(_config, choice);
    }

    void setScreenOffChoice(int choice)
    {
        _config = withDisplayProtectionScreenOffChoice(_config, choice);
    }

    void setProtectWhileCharging(bool enabled)
    {
        _config.protect_while_charging = enabled;
    }

private:
    DisplayProtectionConfig _config;
};
