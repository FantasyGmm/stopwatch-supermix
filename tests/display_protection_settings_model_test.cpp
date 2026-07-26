#include "apps/app_setup/workers/display_protection_settings_model.h"

#include <cassert>

int main()
{
    DisplayProtectionSettingsModel model(defaultDisplayProtectionConfig());
    model.setDimChoice(4);
    assert(model.config().dim_choice == 4);
    assert(model.config().screen_off_choice == 3);
    model.setDimChoice(5);
    assert(model.config().screen_off_choice == 4);
    model.setScreenOffChoice(0);
    assert(model.config().screen_off_choice == 0);
    assert(model.config().dim_choice == 1);
    model.setProtectWhileCharging(true);
    assert(model.config().protect_while_charging);
    return 0;
}
