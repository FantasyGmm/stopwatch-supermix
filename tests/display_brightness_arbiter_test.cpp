#include "runtime/display/display_brightness_arbiter.h"

#include <cassert>

int main()
{
    DisplayBrightnessArbiter arbiter;
    arbiter.reset(55, DisplayProtectionState::Active);
    assert(arbiter.userBrightness() == 55);
    assert(arbiter.effectiveBrightness() == 55);

    // A queued edge-control value cannot win over a same-frame screen-off.
    arbiter.setDisplayState(DisplayProtectionState::ScreenOff);
    arbiter.setUserBrightness(80);
    assert(arbiter.userBrightness() == 80);
    assert(arbiter.effectiveBrightness() == 0);

    // Held/moving touch updates may keep the latest choice, but stay dark.
    for (const int brightness : {85, 90, 95}) {
        arbiter.setUserBrightness(brightness);
        assert(arbiter.effectiveBrightness() == 0);
    }

    // Wake restores the latest user choice, not the pre-screen-off value.
    arbiter.setDisplayState(DisplayProtectionState::Active);
    assert(arbiter.userBrightness() == 95);
    assert(arbiter.effectiveBrightness() == 95);

    arbiter.setDisplayState(DisplayProtectionState::Dimmed);
    assert(arbiter.effectiveBrightness() == 20);
    return 0;
}
