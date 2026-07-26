#include "apps/app_codex_status/trae_status_ui_layout.h"

#include <cassert>


int main()
{
    using namespace trae_status_ui;

    assert(kBrandIconX == 168 && kBrandIconY == 22);
    assert(kBrandX == 218 && kBrandY == 32);
    assert(kHealthX == 118 && kHealthY == 67 && kHealthWidth == 230);
    assert(kRobotX == 96 && kRobotY == 105);
    assert(kRobotWidth == 77 && kRobotHeight == 86);
    assert(kTelemetryX == 214);
    assert(kDividerX == 82 && kDividerY == 223 && kDividerWidth == 302);
    assert(kSummaryY == 240);
    assert(kDeckX == 108 && kDeckY == 268);
    assert(kDeckWidth == 250 && kDeckHeight == 120);
    assert(kCardWidth == 250 && kCardHeight == 98);
    assert(kDeckY + kDeckHeight < kPageIndicatorY);
    assert(kPageIndicatorY == 416 && kBatteryY == 438);
}
