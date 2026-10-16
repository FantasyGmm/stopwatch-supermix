#pragma once

#include <cstdint>

namespace codex_status_ui {

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct AgentNameFit {
    int scale;
    bool elide;
};

inline AgentNameFit fit_agent_name(int unscaled_width, int available_width)
{
    constexpr int scales[] = {256, 236, 217, 197};
    for (const int scale : scales) {
        const int scaled_width =
            (unscaled_width * scale + 255) / 256;
        if (scaled_width <= available_width) {
            return {scale, false};
        }
    }
    return {scales[3], true};
}

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

constexpr int kPetX = 55;
constexpr int kPetY = 88;
constexpr int kPetWidth = 144;
constexpr int kPetHeight = 156;
constexpr int kPetScale = 384;
constexpr Rect kBeaconSpine{210, 96, 2, 120};

constexpr int kTelemetryX = 231;
constexpr int kTelemetryWidth = 200;
constexpr int kStatusCaptionY = 61;
constexpr int kAgentStatusWidth = 107;
constexpr int kAgentStatusX =
    kTelemetryX + (kTelemetryWidth - kAgentStatusWidth) / 2;
constexpr int kVoiceCaptionX = kPetX;
constexpr int kVoiceColumnWidth = kPetWidth;
constexpr int kVoiceStatusWidth = 107;
constexpr int kVoiceDotSize = 6;
constexpr int kVoiceDotGap = 6;
constexpr int kStatusValueY = 73;
constexpr int kModelCaptionY = 100;
constexpr int kModelValueY = 121;
constexpr int kEffortCaptionY = 160;
constexpr int kEffortValueY = 181;
constexpr int kStatusValueHeight = 20;
constexpr int kLargeValueHeight = 36;
constexpr uint32_t kLongLabelScrollDurationMs = 10000;

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
constexpr int kProjectNameX = 72;
constexpr int kProjectNameY = 366;
constexpr int kProjectNameWidth = 322;
constexpr int kProjectNameHeight = 34;
constexpr int kPageIndicatorY = 416;
constexpr int kBatteryY = 432;

}  // namespace codex_status_ui
