#pragma once

#include <cstddef>
#include <cstdint>

namespace agent_panel_ui {

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct LedgerMetadataLayout {
    Rect state;
    Rect id;
    Rect time;
};

constexpr int kScreenSize = 466;
constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0x788391;
constexpr uint32_t kDivider = 0x26313D;
constexpr uint32_t kSelection = 0x49C9EE;
constexpr uint32_t kConnected = 0x53D7A4;
constexpr uint32_t kRequesting = 0xFFB454;
constexpr uint32_t kOffline = 0x667180;
constexpr uint32_t kDestructive = 0xFF6248;
constexpr uint32_t kPairing = 0x7C9DFF;
constexpr uint8_t kHandleOpacity = 184;
constexpr uint8_t kConnectedWashOpacity = 24;
constexpr uint8_t kDividerOpacity = 158;
constexpr uint8_t kRequestHaloOpacity = 33;

constexpr Rect kDragHandle{209, 12, 48, 4};
constexpr Rect kManageRect{153, 20, 160, 54};
constexpr Rect kRailViewport{32, 78, 402, 300};
constexpr Rect kFocusBand{39, 195, 388, 60};
constexpr Rect kFocusMarker{42, 211, 3, 28};
constexpr Rect kResetRect{139, 390, 188, 44};
constexpr Rect kCloseHint{193, 438, 80, 4};
constexpr Rect kPullHint{193, 456, 80, 4};
constexpr int kCollapsedVisibleHeight = 14;
constexpr int kPanelHiddenY = -(kScreenSize - kCollapsedVisibleHeight);
constexpr int kPanelHeaderBottomY = 74;
constexpr int kRailRowHeight = 58;
constexpr int kMinimumTouchHeight = 44;
constexpr int kLedgerTop = 74;
constexpr int kLedgerHeight = 352;
constexpr int kLedgerMaximumRowHeight = 66;
constexpr int kLedgerSafeRadius = 220;
constexpr int kLedgerHorizontalInset = 18;
constexpr int kDeleteRevealWidth = 76;
constexpr uint32_t kResetHoldMs = 1200;
constexpr uint32_t kNoticeDurationMs = 2000;

constexpr int panel_hint_y(int panel_y)
{
    const int clamped_y = panel_y < kPanelHiddenY
        ? kPanelHiddenY
        : (panel_y > 0 ? 0 : panel_y);
    const int hint_travel = kPullHint.y - kCloseHint.y;
    return kCloseHint.y +
        (-clamped_y * hint_travel) / (-kPanelHiddenY);
}

constexpr int integer_square_root(int value)
{
    if (value <= 0) {
        return 0;
    }
    int root = 0;
    while (root + 1 <= value / (root + 1)) {
        ++root;
    }
    return root;
}

constexpr Rect ledger_row_layout(size_t count, size_t index)
{
    if (count == 0 || count > 8 || index >= count) {
        return {0, 0, 0, 0};
    }
    int row_height = kLedgerHeight / static_cast<int>(count);
    if (row_height < kMinimumTouchHeight) {
        row_height = kMinimumTouchHeight;
    } else if (row_height > kLedgerMaximumRowHeight) {
        row_height = kLedgerMaximumRowHeight;
    }
    const int used_height = row_height * static_cast<int>(count);
    const int first_y = kLedgerTop + (kLedgerHeight - used_height) / 2;
    const int y = first_y + static_cast<int>(index) * row_height;
    const int center_y = y + row_height / 2;
    const int delta_y = center_y >= kScreenSize / 2
        ? center_y - kScreenSize / 2
        : kScreenSize / 2 - center_y;
    const int chord_squared =
        kLedgerSafeRadius * kLedgerSafeRadius - delta_y * delta_y;
    const int width =
        2 * integer_square_root(chord_squared) - kLedgerHorizontalInset;
    return {(kScreenSize - width) / 2, y, width, row_height};
}

constexpr LedgerMetadataLayout ledger_metadata_layout(int row_width)
{
    constexpr int kTextStart = 30;
    constexpr int kRightInset = 8;
    // LVGL's Montserrat 14 glyph advance, kerning, rounding, and -1 px letter
    // spacing measure 98 px for "Switch request", 51 px for "ID F62B", and
    // 66 px for "Unknown". Keep every field complete in the narrowest row.
    constexpr int kNarrowStateWidth = 98;
    constexpr int kIdWidth = 54;
    constexpr int kTimeWidth = 68;
    const int available = row_width - kTextStart - kRightInset;
    if (available < kNarrowStateWidth + kIdWidth + kTimeWidth) {
        return {};
    }
    const int extra = available -
        (kNarrowStateWidth + kIdWidth + kTimeWidth);
    const int state_width = kNarrowStateWidth + extra;
    const int id_x = kTextStart + state_width;
    const int time_x = id_x + kIdWidth;
    return {
        {kTextStart, 0, state_width, 0},
        {id_x, 0, kIdWidth, 0},
        {time_x, 0, kTimeWidth, 0},
    };
}

}  // namespace agent_panel_ui
