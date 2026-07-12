#include "apps/app_clock/clock_navigation.h"
#include <cassert>

int main()
{
    assert(resolve_clock_navigation(true, true, true, true) == ClockNavigation::Home);
    assert(resolve_clock_navigation(false, true, false, false) == ClockNavigation::FeatureMenu);
    assert(resolve_clock_navigation(false, false, true, false) == ClockNavigation::Previous);
    assert(resolve_clock_navigation(false, false, false, true) == ClockNavigation::Next);
    assert(resolve_clock_navigation(false, false, false, false) == ClockNavigation::None);
    return 0;
}
