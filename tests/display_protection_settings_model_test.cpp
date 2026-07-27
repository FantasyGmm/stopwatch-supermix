#include "apps/app_setup/workers/display_protection_settings_model.h"

#include <cassert>

int main()
{
    DisplayProtectionSettingsModel model(defaultDisplayProtectionConfig());
    assert(model.dimSliderPosition() == 2);
    assert(model.screenOffSliderPosition() == 2);
    assert(model.dimLabel() == "15s");
    assert(model.screenOffLabel() == "1m");

    model.setDimSliderPosition(0);
    assert(!model.config().dim_enabled);
    assert(model.config().dim_choice == 1);
    assert(model.dimSliderPosition() == 0);
    assert(model.dimLabel() == "Off");
    assert(model.config().screen_off_enabled);

    model.setScreenOffSliderPosition(0);
    assert(!model.config().screen_off_enabled);
    assert(model.config().screen_off_choice == 1);
    assert(model.screenOffSliderPosition() == 0);
    assert(model.screenOffLabel() == "Off");

    model.setDimSliderPosition(6);
    assert(model.config().dim_enabled);
    assert(model.config().dim_choice == 5);
    assert(!model.config().screen_off_enabled);
    assert(model.config().screen_off_choice == 1);

    model.setScreenOffSliderPosition(1);
    assert(model.config().screen_off_enabled);
    assert(model.config().screen_off_choice == 0);
    assert(model.config().dim_choice == 1);

    model.setDimSliderPosition(5);
    assert(model.config().dim_choice == 4);
    assert(model.config().screen_off_choice == 3);

    model.setProtectWhileCharging(true);
    assert(model.config().protect_while_charging);
    return 0;
}
