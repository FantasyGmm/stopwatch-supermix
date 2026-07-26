#include "hal/power_led_blink_config.h"

#include <cassert>

int main()
{
    assert(normalize_power_led_blink_interval_seconds(0) == 1);
    assert(normalize_power_led_blink_interval_seconds(2) == 2);
    assert(normalize_power_led_blink_interval_seconds(11) == 10);
    assert(power_led_blink_interval_ms(3) == 3000U);
    return 0;
}
