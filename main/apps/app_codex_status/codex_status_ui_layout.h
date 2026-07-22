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

constexpr uint32_t kInstrumentBlack = 0x070A0F;
constexpr uint32_t kPanelGraphite   = 0x27303E;
constexpr uint32_t kBeaconViolet    = 0x7C9DFF;
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
constexpr int kHostY = 84;

constexpr Rect kPetDock{52, 112, 104, 104};
constexpr int kPetX = 56;
constexpr int kPetY = 112;
constexpr int kPetWidth = 96;
constexpr int kPetHeight = 104;
constexpr Rect kBeaconSpine{170, 112, 2, 104};

constexpr int kTelemetryX = 190;
constexpr int kTelemetryWidth = 224;
constexpr int kModelCaptionY = 114;
constexpr int kModelValueY = 136;
constexpr int kEffortCaptionY = 170;
constexpr int kEffortValueY = 192;

constexpr int kQuotaX = 55;
constexpr int kQuotaWidth = 356;
constexpr int kQuotaLabelWidth = 32;
constexpr int kQuotaPercentWidth = 70;
constexpr int kQuotaBarX = 180;
constexpr int kQuotaBarWidth = 231;
constexpr int kFiveHourY = 236;
constexpr int kWeekY = 276;

constexpr int kAlertDividerX = 82;
constexpr int kAlertDividerY = 320;
constexpr int kAlertDividerWidth = 302;
constexpr int kAlertHeadingY = 337;
constexpr int kAlertHeadingWidth = 310;
constexpr int kAlertProjectY = 367;
constexpr int kAlertProjectWidth = 340;
constexpr int kAlertDetailY = 394;
constexpr int kAlertDetailWidth = 320;
constexpr int kBatteryY = 435;

}  // namespace codex_status_ui
