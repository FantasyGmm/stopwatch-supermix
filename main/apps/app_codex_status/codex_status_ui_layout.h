#pragma once

#include <cstdint>

namespace codex_status_ui {

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

constexpr int kScreenSize = 466;

constexpr uint32_t kInstrumentBlack = 0x000000;
constexpr uint32_t kPanelGraphite   = 0x27303E;
constexpr uint32_t kBeaconViolet    = 0x7C9DFF;
constexpr uint32_t kRunningCyan     = 0x49C9EE;
constexpr uint32_t kQuotaMint       = 0x53D7A4;
constexpr uint32_t kSignalWhite     = 0xF2F6F8;
constexpr uint32_t kEtchedGray      = 0x788391;
constexpr uint32_t kAlertVermilion  = 0xFF6248;

constexpr int kLogoX = 168;
constexpr int kLogoY = 22;
constexpr int kLogoScale = 170;
constexpr int kBrandX = 218;
constexpr int kBrandY = 32;
constexpr int kStatusY = 63;

constexpr int kPetX = 55;
constexpr int kPetY = 88;
constexpr int kPetWidth = 144;
constexpr int kPetHeight = 156;
constexpr int kPetScale = 384;
constexpr Rect kBeaconSpine{210, 96, 2, 120};

constexpr int kTelemetryX = 231;
constexpr int kTelemetryWidth = 200;
constexpr int kModelCaptionY = 98;
constexpr int kModelValueY = 120;
constexpr int kEffortCaptionY = 154;
constexpr int kEffortValueY = 176;

constexpr int kQuotaX = 55;
constexpr int kQuotaWidth = 356;
constexpr int kQuotaLabelWidth = 32;
constexpr int kQuotaBarX = 101;
constexpr int kQuotaBarWidth = 248;
constexpr int kQuotaPercentX = 351;
constexpr int kQuotaPercentWidth = 60;
constexpr int kWeekY = 246;

constexpr int kAlertDividerX = 82;
constexpr int kAlertDividerY = 282;
constexpr int kAlertDividerWidth = 302;

constexpr int kPendingLabelX = 54;
constexpr int kRunningLabelX = 300;
constexpr int kPendingCountX = 78;
constexpr int kRunningCountX = 324;
constexpr int kStateLabelY = 294;
constexpr int kStateLabelWidth = 112;
constexpr int kStateCountY = 320;
constexpr int kStateCountWidth = 64;

constexpr int kTaskIndexX = 151;
constexpr int kTaskIndexY = 306;
constexpr int kTaskIndexWidth = 164;
constexpr int kAlertProjectX = 145;
constexpr int kAlertProjectY = 332;
constexpr int kAlertProjectWidth = 176;
constexpr int kAlertDetailX = 93;
constexpr int kAlertDetailY = 363;
constexpr int kAlertDetailWidth = 280;
constexpr int kAlertDetailHeight = 41;
constexpr int kPageIndicatorY = 416;
constexpr int kBatteryY = 432;

}  // namespace codex_status_ui
