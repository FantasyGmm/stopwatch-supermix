/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

enum class ClockNavigation {
    None,
    Home,
    FeatureMenu,
    Previous,
    Next,
};

ClockNavigation resolve_clock_navigation(bool home_chord, bool left_hold, bool left_click, bool right_click);
