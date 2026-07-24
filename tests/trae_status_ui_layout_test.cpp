#include "apps/app_codex_status/trae_status_ui_layout.h"

#include <cassert>


int main()
{
    using namespace trae_status_ui;

    assert(kDividerX == 82);
    assert(kDividerY == 223);
    assert(kDividerWidth == 302);
    assert(kLowerPanelX == 69);
    assert(kLowerPanelY == 246);
    assert(kLowerPanelWidth == 328);
    assert(kLowerPanelHeight == 118);
    assert(kColumnDividerX == 210);
    assert(kColumnDividerY == kLowerPanelY);
    assert(kColumnDividerHeight == 98);
    assert(kLeftColumnX == 69);
    assert(kLeftColumnWidth == 126);
    assert(kRightColumnX == 228);
    assert(kRightColumnWidth == 169);
    assert(kLeftColumnX + kLeftColumnWidth < kColumnDividerX);
    assert(kColumnDividerX < kRightColumnX);
    assert(kRightColumnX + kRightColumnWidth <=
           kLowerPanelX + kLowerPanelWidth);
    assert(kLowerPanelY + kLowerPanelHeight < kPageIndicatorY);
    assert(kPageIndicatorY == 416);
    assert(kBatteryY == 438);
}
