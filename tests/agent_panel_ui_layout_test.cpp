#include "main/apps/app_codex_status/agent_panel_ui_layout.h"

#include <cassert>

int main()
{
    using namespace agent_panel_ui;
    static_assert(kScreenSize == 466);
    static_assert(kRailViewport.x == 32);
    static_assert(kRailViewport.y == 78);
    static_assert(kRailViewport.w == 402);
    static_assert(kRailViewport.h == 300);
    static_assert(kFocusBand.x == 39);
    static_assert(kFocusBand.y == 195);
    static_assert(kFocusBand.w == 388);
    static_assert(kFocusBand.h == 60);
    static_assert(kRailRowHeight == 58);
    static_assert(kMinimumTouchHeight >= 44);
    static_assert(kPanelHeaderBottomY == 78);
    static_assert(kDeleteRevealWidth == 76);
    static_assert(kResetHoldMs == 1200);
    assert(kResetRect.x >= 0);
    assert(kResetRect.x + kResetRect.w <= kScreenSize);
    assert(kCloseHint.y + kCloseHint.h <= kScreenSize);
}
