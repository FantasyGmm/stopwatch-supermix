#include "apps/app_codex_status/codex_status_ui_layout.h"

#include <cassert>

int main()
{
    using namespace codex_status_ui;

    assert(kScreenSize == 466);
    assert(kInstrumentBlack == 0x000000);
    assert(kPetX == 73);
    assert(kPetY == 88);
    assert(kPetWidth == 110);
    assert(kPetHeight == 120);
    assert(kPetScale == 293);
    assert(kPetX + kPetWidth == 183);

    assert(kBeaconSpine.x == 194);
    assert(kBeaconSpine.y == 96);
    assert(kBeaconSpine.w == 2);
    assert(kBeaconSpine.h == 104);
    assert(kBeaconSpine.x - (kPetX + kPetWidth) == 11);

    assert(kTelemetryX == 214);
    assert(kTelemetryWidth == 200);
    assert(kTelemetryX > kBeaconSpine.x + kBeaconSpine.w);
    assert(kTelemetryX + kTelemetryWidth == 414);
    assert(kModelCaptionY == 98);
    assert(kModelValueY == 120);
    assert(kEffortCaptionY == 154);
    assert(kEffortValueY == 176);
    assert(kModelCaptionY < kModelValueY);
    assert(kModelValueY < kEffortCaptionY);
    assert(kEffortCaptionY < kEffortValueY);

    assert(kQuotaX == 55);
    assert(kQuotaWidth == 356);
    assert(kQuotaLabelWidth == 32);
    assert(kQuotaBarX == 101);
    assert(kQuotaBarWidth == 248);
    assert(kQuotaPercentX == 351);
    assert(kQuotaPercentWidth == 60);
    assert(kQuotaBarX + kQuotaBarWidth < kQuotaPercentX);
    assert(kQuotaPercentX + kQuotaPercentWidth == kQuotaX + kQuotaWidth);
    assert(kWeekY == 220);
    assert(kWeekY < kAlertDividerY);
    assert(kAlertDividerY < kStateLabelY);
    assert(kStateLabelY < kStateCountY);
    assert(kTaskIndexY < kAlertProjectY);
    assert(kAlertProjectY < kAlertDetailY);
    assert(kAlertDetailY < kBatteryY);

    assert(kAlertDividerX == 82);
    assert(kAlertDividerY == 258);
    assert(kAlertDividerWidth == 302);
    assert(kPendingLabelX == 62);
    assert(kRunningLabelX == 308);
    assert(kPendingCountX == 78);
    assert(kRunningCountX == 324);
    assert(kStateLabelY == 278);
    assert(kStateLabelWidth == 96);
    assert(kStateCountY == 294);
    assert(kStateCountWidth == 64);
    assert(kPendingLabelX == kScreenSize - (kRunningLabelX + kStateLabelWidth));
    assert(kPendingCountX == kScreenSize - (kRunningCountX + kStateCountWidth));
    assert(kPendingLabelX + kStateLabelWidth / 2 ==
           kPendingCountX + kStateCountWidth / 2);
    assert(kRunningLabelX + kStateLabelWidth / 2 ==
           kRunningCountX + kStateCountWidth / 2);
    assert(kTaskIndexX == 151);
    assert(kTaskIndexY == 284);
    assert(kTaskIndexWidth == 164);
    assert(kAlertProjectX == 145);
    assert(kAlertProjectY == 307);
    assert(kAlertProjectWidth == 176);
    assert(kAlertDetailX == 93);
    assert(kAlertDetailY == 359);
    assert(kAlertDetailWidth == 280);
    assert(kAlertDetailHeight == 41);
    assert(kPageIndicatorY == 416);
    assert(kBatteryY == 432);

    assert(kStateLabelY - kAlertDividerY == 20);
    assert(kAlertProjectY - kTaskIndexY == 23);
    assert(kAlertDetailY - kAlertProjectY == 52);
    assert(kPageIndicatorY - (kAlertDetailY + kAlertDetailHeight) == 16);
    assert(kBatteryY - kPageIndicatorY == 16);
    assert(kBatteryY - kAlertDetailY == 73);
    assert(kBatteryY < kScreenSize);

    assert(kPanelGraphite == 0x27303E);
    assert(kBeaconViolet == 0x7C9DFF);
    assert(kRunningCyan == 0x49C9EE);
    assert(kQuotaMint == 0x53D7A4);
    assert(kSignalWhite == 0xF2F6F8);
    assert(kEtchedGray == 0x788391);
    assert(kAlertVermilion == 0xFF6248);
}
