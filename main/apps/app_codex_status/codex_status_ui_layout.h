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
constexpr uint32_t kPanelGraphite   = 0x121823;
constexpr uint32_t kCalibrationBlue = 0x54C7EC;
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

constexpr Rect kModelInstrument{54, 112, 116, 92};
constexpr Rect kEffortInstrument{296, 112, 116, 92};
constexpr int kPetCenterX = 233;
constexpr int kPetCenterY = 158;
constexpr int kCalibrationY = 158;

constexpr int kQuotaX = 49;
constexpr int kQuotaWidth = 368;
constexpr int kFiveHourY = 225;
constexpr int kWeekY = 268;

constexpr int kAlertDividerX = 73;
constexpr int kAlertDividerY = 310;
constexpr int kAlertDividerWidth = 320;
constexpr int kAlertDividerGap = 28;
constexpr int kAlertHeadingY = 324;
constexpr int kAlertHeadingWidth = 310;
constexpr int kAlertProjectY = 354;
constexpr int kAlertProjectWidth = 340;
constexpr int kAlertDetailY = 382;
constexpr int kAlertDetailWidth = 320;
constexpr int kBatteryY = 435;

}  // namespace codex_status_ui
