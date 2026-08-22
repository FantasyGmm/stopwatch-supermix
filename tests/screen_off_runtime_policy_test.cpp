#include "runtime/power/screen_off_runtime_policy.h"

#include <cassert>
#include <cstdint>

int main()
{
    ScreenOffRuntimeCadence cadence;

    // Bright-screen work remains immediate and unthrottled.
    assert(cadence.background_work_due(false, 100U));
    assert(cadence.background_work_due(false, 101U));
    assert(screen_off_main_loop_delay_ms(false) == 0U);
    assert(lvgl_runtime_period_ms(false) == 10U);

    // Entering screen-off mode performs one maintenance pass, then coalesces
    // work until the bounded background interval expires.
    assert(cadence.background_work_due(true, 200U));
    assert(!cadence.background_work_due(true, 249U));
    assert(cadence.background_work_due(true, 250U));
    assert(screen_off_main_loop_delay_ms(true) == 10U);
    assert(lvgl_runtime_period_ms(true) == 50U);

    // Waking the screen must restore immediate work on the first pass.
    assert(cadence.background_work_due(false, 251U));
    assert(cadence.background_work_due(false, 252U));

    // Elapsed-time checks remain correct across the uint32_t timer wrap.
    ScreenOffRuntimeCadence wrapping;
    assert(wrapping.background_work_due(true, UINT32_MAX - 20U));
    assert(!wrapping.background_work_due(true, 28U));
    assert(wrapping.background_work_due(true, 29U));
    return 0;
}
