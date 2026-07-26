#pragma once

#include <cstdint>


namespace trae_status_ui {

constexpr int kScreenSize = 466;
constexpr int kPageIndicatorY = 416;
constexpr int kBatteryY = 438;

constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kCardBackground = 0x151B24;
constexpr uint32_t kAccent = 0x7C9DFF;
constexpr uint32_t kRunning = 0x49C9EE;
constexpr uint32_t kPending = 0xFF6248;
constexpr uint32_t kProcessLive = 0x53D7A4;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0xB8C2CC;
constexpr uint32_t kDimText = 0x788391;

constexpr int kBrandIconX = 168;
constexpr int kBrandIconY = 22;
constexpr int kBrandIconScaleX = 77;
constexpr int kBrandIconScaleY = 112;
constexpr int kBrandX = 218;
constexpr int kBrandY = 32;

constexpr int kHealthX = 118;
constexpr int kHealthY = 67;
constexpr int kHealthWidth = 230;
constexpr int kBluetoothWidth = 72;
constexpr int kHealthDotX = 197;
constexpr int kProcessX = 213;

constexpr int kRobotX = 96;
constexpr int kRobotY = 105;
constexpr int kRobotWidth = 77;
constexpr int kRobotHeight = 86;
constexpr int kRobotScale = 183;
constexpr int kTelemetryX = 214;
constexpr int kTelemetryWidth = 200;
constexpr int kModelCaptionY = 98;
constexpr int kModelValueY = 120;
constexpr int kEffortCaptionY = 154;
constexpr int kEffortValueY = 176;

constexpr int kDividerX = 82;
constexpr int kDividerY = 223;
constexpr int kDividerWidth = 302;

constexpr int kSummaryY = 240;
constexpr int kPendingSummaryX = 108;
constexpr int kPendingCountX = 169;
constexpr int kRunningSummaryX = 238;
constexpr int kRunningCountX = 299;

constexpr int kDeckX = 108;
constexpr int kDeckY = 268;
constexpr int kDeckWidth = 250;
constexpr int kDeckHeight = 120;
constexpr int kCardWidth = 250;
constexpr int kCardHeight = 98;
constexpr int kBackCardOffset = 10;
constexpr int kDeepCardOffset = 18;

}  // namespace trae_status_ui
