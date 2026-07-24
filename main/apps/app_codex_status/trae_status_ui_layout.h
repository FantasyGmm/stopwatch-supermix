#pragma once

#include <cstdint>


namespace trae_status_ui {

constexpr int kScreenSize = 466;
constexpr int kPageIndicatorY = 416;
constexpr int kBatteryY = 438;

constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kAccent = 0x7C9DFF;
constexpr uint32_t kRunning = 0x49C9EE;
constexpr uint32_t kPending = 0xFF6248;
constexpr uint32_t kCompleted = 0x3CB371;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0xB8C2CC;
constexpr uint32_t kDimText = 0x788391;

constexpr int kBrandX = 218;
constexpr int kBrandY = 32;
constexpr int kStatusY = 63;

constexpr int kSignalX = 72;
constexpr int kSignalY = 105;
constexpr int kSignalSize = 86;

constexpr int kTelemetryX = 214;
constexpr int kTelemetryWidth = 200;
constexpr int kModelCaptionY = 98;
constexpr int kModelValueY = 120;
constexpr int kEffortCaptionY = 154;
constexpr int kEffortValueY = 176;

constexpr int kDividerX = 82;
constexpr int kDividerY = 223;
constexpr int kDividerWidth = 302;

constexpr int kLowerPanelX = 69;
constexpr int kLowerPanelY = 246;
constexpr int kLowerPanelWidth = 328;
constexpr int kLowerPanelHeight = 118;
constexpr int kColumnDividerX = 210;
constexpr int kColumnDividerY = kLowerPanelY;
constexpr int kColumnDividerHeight = 98;

constexpr int kLeftColumnX = 69;
constexpr int kLeftColumnWidth = 126;
constexpr int kRightColumnX = 228;
constexpr int kRightColumnWidth = 169;

constexpr int kCaptionY = 250;
constexpr int kPrimaryValueY = 274;
constexpr int kSecondaryCaptionY = 311;
constexpr int kSecondaryValueY = 333;

}  // namespace trae_status_ui
