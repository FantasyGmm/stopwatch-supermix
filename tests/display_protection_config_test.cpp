#include "runtime/display/display_protection_config.h"

#include <cassert>

int main()
{
    const auto defaults = defaultDisplayProtectionConfig();
    assert(defaults.dim_choice == 1);
    assert(defaults.screen_off_choice == 1);
    assert(!defaults.protect_while_charging);
    assert(displayProtectionDimMs(defaults) == 15000U);
    assert(displayProtectionScreenOffMs(defaults) == 60000U);
    assert(displayProtectionDimLabel(5) == "5m");
    assert(displayProtectionScreenOffLabel(4) == "10m");

    DisplayProtectionConfig invalid{99, 0, true};
    const auto repaired = sanitizeDisplayProtectionConfig(invalid);
    assert(repaired.dim_choice == 5);
    assert(repaired.screen_off_choice == 4);
    assert(repaired.protect_while_charging);

    auto linked = withDisplayProtectionDimChoice(defaults, 4);
    assert(linked.dim_choice == 4);
    assert(linked.screen_off_choice == 3);
    linked = withDisplayProtectionDimChoice(linked, 5);
    assert(linked.screen_off_choice == 4);

    linked = withDisplayProtectionScreenOffChoice(defaults, 0);
    assert(linked.screen_off_choice == 0);
    assert(linked.dim_choice == 1);
    return 0;
}
