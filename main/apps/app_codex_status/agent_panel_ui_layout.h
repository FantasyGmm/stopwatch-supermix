#pragma once

#include <cstdint>

namespace agent_panel_ui {

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

constexpr int kScreenSize = 466;
constexpr uint32_t kBackground = 0x080B10;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0x788391;
constexpr uint32_t kSelection = 0x49C9EE;
constexpr uint32_t kConnected = 0x53D7A4;
constexpr uint32_t kRequesting = 0xFFB454;
constexpr uint32_t kOffline = 0x667180;
constexpr uint32_t kDestructive = 0xFF6248;
constexpr uint32_t kPairing = 0x7C9DFF;

constexpr Rect kDragHandle{209, 12, 48, 4};
constexpr Rect kRailViewport{32, 78, 402, 300};
constexpr Rect kFocusBand{39, 195, 388, 60};
constexpr Rect kFocusMarker{42, 211, 3, 28};
constexpr Rect kResetRect{139, 390, 188, 44};
constexpr Rect kCloseHint{193, 438, 80, 4};
constexpr int kPanelHeaderBottomY = 78;
constexpr int kRailRowHeight = 58;
constexpr int kMinimumTouchHeight = 44;
constexpr int kDeleteRevealWidth = 76;
constexpr uint32_t kResetHoldMs = 1200;
constexpr uint32_t kNoticeDurationMs = 2000;

}  // namespace agent_panel_ui
