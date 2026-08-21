#include "main/apps/app_codex_status/agent_panel_ui_layout.h"

#include <cassert>
#include <cstddef>

int main()
{
    using namespace agent_panel_ui;
    static_assert(kScreenSize == 466);
    static_assert(kBackground == 0x000000);
    static_assert(kCollapsedVisibleHeight == 14);
    static_assert(kPanelHiddenY == -452);
    static_assert(kDivider == 0x26313D);
    static_assert(kHandleOpacity == 184);
    static_assert(kConnectedWashOpacity == 24);
    static_assert(kDividerOpacity == 158);
    static_assert(kRequestHaloOpacity == 33);
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
    static_assert(kPanelHeaderBottomY == 74);
    static_assert(kDeleteRevealWidth == 76);
    static_assert(kResetHoldMs == 1200);
    assert(kResetRect.x >= 0);
    assert(kResetRect.x + kResetRect.w <= kScreenSize);
    assert(kCloseHint.y + kCloseHint.h <= kScreenSize);
    assert(kPullHint.y + kPullHint.h <= kScreenSize);
    assert(kPanelHiddenY + kPullHint.y == 4);
    assert(kPanelHiddenY + kPullHint.y + kPullHint.h <=
           kCollapsedVisibleHeight);
    assert(panel_hint_y(kPanelHiddenY) == kPullHint.y);
    assert(panel_hint_y(0) == kCloseHint.y);
    assert(panel_hint_y(kPanelHiddenY / 2) > kCloseHint.y);
    assert(panel_hint_y(kPanelHiddenY / 2) < kPullHint.y);

    static_assert(kManageRect.x == 153);
    static_assert(kManageRect.y == 20);
    static_assert(kManageRect.w == 160);
    static_assert(kManageRect.h == 54);
    static_assert(kLedgerTop == 74);
    static_assert(kLedgerHeight == 352);
    static_assert(kLedgerTop + kLedgerHeight == 426);
    static_assert(kManageRect.y + kManageRect.h <= kLedgerTop);
    static_assert(kPanelHeaderBottomY <= kLedgerTop);

    const Rect one = ledger_row_layout(1, 0);
    assert(one.x == 23);
    assert(one.y == 217);
    assert(one.w == 420);
    assert(one.h == 66);

    const Rect five_first = ledger_row_layout(5, 0);
    const Rect five_last = ledger_row_layout(5, 4);
    assert(five_first.x == 55);
    assert(five_first.y == 85);
    assert(five_first.w == 356);
    assert(five_first.h == 66);
    assert(five_last.x == 81);
    assert(five_last.y == 349);
    assert(five_last.w == 304);
    assert(five_last.h == 66);

    const Rect eight_first = ledger_row_layout(8, 0);
    const Rect eight_last = ledger_row_layout(8, 7);
    assert(eight_first.x == 70);
    assert(eight_first.y == 74);
    assert(eight_first.w == 326);
    assert(eight_first.h == 44);
    assert(eight_last.x == 104);
    assert(eight_last.y == 382);
    assert(eight_last.w == 258);
    assert(eight_last.h == 44);
    assert(eight_last.y + eight_last.h == kLedgerTop + kLedgerHeight);

    const LedgerMetadataLayout narrow_metadata =
        ledger_metadata_layout(eight_last.w);
    assert(narrow_metadata.state.x == 30);
    assert(narrow_metadata.state.w >= 98);
    assert(narrow_metadata.id.w >= 54);
    assert(narrow_metadata.time.w >= 68);
    assert(
        narrow_metadata.state.x + narrow_metadata.state.w <=
        narrow_metadata.id.x);
    assert(
        narrow_metadata.id.x + narrow_metadata.id.w <=
        narrow_metadata.time.x);
    assert(
        narrow_metadata.time.x + narrow_metadata.time.w <=
        eight_last.w - 8);

    for (size_t count = 1; count <= 8; ++count) {
        for (size_t index = 0; index < count; ++index) {
            const Rect row = ledger_row_layout(count, index);
            assert(row.w > 0);
            assert(row.h >= kMinimumTouchHeight);
            assert(row.x >= 0);
            assert(row.x + row.w <= kScreenSize);
            assert(row.y >= kLedgerTop);
            assert(row.y + row.h <= kLedgerTop + kLedgerHeight);
        }
    }
    const Rect invalid_count = ledger_row_layout(0, 0);
    const Rect invalid_index = ledger_row_layout(8, 8);
    assert(invalid_count.w == 0 && invalid_count.h == 0);
    assert(invalid_index.w == 0 && invalid_index.h == 0);
}
