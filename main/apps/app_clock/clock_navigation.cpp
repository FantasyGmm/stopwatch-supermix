/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "clock_navigation.h"

ClockNavigation resolve_clock_navigation(bool home_chord, bool left_hold, bool left_click, bool right_click)
{
    if (home_chord) {
        return ClockNavigation::Home;
    }
    if (left_hold) {
        return ClockNavigation::FeatureMenu;
    }
    if (left_click) {
        return ClockNavigation::Previous;
    }
    if (right_click) {
        return ClockNavigation::Next;
    }
    return ClockNavigation::None;
}
