#include "apps/app_codex_status/codex_status_ui_layout.h"

#include <cassert>

int main()
{
    using namespace codex_status_ui;

    assert(kScreenSize == 466);
    assert(kInstrumentBlack == 0x000000);
    assert(kPetX == 55);
    assert(kPetY == 88);
    assert(kPetWidth == 144);
    assert(kPetHeight == 156);
    assert(kPetScale == 384);
    assert(kPetX + kPetWidth == 199);

    assert(kBeaconSpine.x == 210);
    assert(kBeaconSpine.y == 96);
    assert(kBeaconSpine.w == 2);
    assert(kBeaconSpine.h == 120);
    assert(kBeaconSpine.x - (kPetX + kPetWidth) == 11);

    assert(kTelemetryX == 231);
    assert(kTelemetryWidth == 200);
    assert(kTelemetryX > kBeaconSpine.x + kBeaconSpine.w);
    assert(kTelemetryX + kTelemetryWidth == 431);
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
    assert(kWeekY == 246);
    assert(kWeekY < kAlertDividerY);
    assert(kAlertDividerY < kStateLabelY);
    assert(kStateLabelY < kStateCountY);
    assert(kTaskIndexY < kAlertProjectY);
    assert(kAlertProjectY < kAlertDetailY);
    assert(kAlertDetailY < kBatteryY);

    assert(kAlertDividerX == 82);
    assert(kAlertDividerY == 282);
    assert(kAlertDividerWidth == 302);
    assert(kPendingLabelX == 54);
    assert(kRunningLabelX == 300);
    assert(kPendingCountX == 78);
    assert(kRunningCountX == 324);
    assert(kStateLabelY == 294);
    assert(kStateLabelWidth == 112);
    assert(kStateCountY == 320);
    assert(kStateCountWidth == 64);
    assert(kPendingLabelX == kScreenSize - (kRunningLabelX + kStateLabelWidth));
    assert(kPendingCountX == kScreenSize - (kRunningCountX + kStateCountWidth));
    assert(kPendingLabelX + kStateLabelWidth / 2 ==
           kPendingCountX + kStateCountWidth / 2);
    assert(kRunningLabelX + kStateLabelWidth / 2 ==
           kRunningCountX + kStateCountWidth / 2);
    assert(kTaskIndexX == 151);
    assert(kTaskIndexY == 306);
    assert(kTaskIndexWidth == 164);
    assert(kAlertProjectX == 145);
    assert(kAlertProjectY == 332);
    assert(kAlertProjectWidth == 176);
    assert(kAlertDetailX == 93);
    assert(kAlertDetailY == 363);
    assert(kAlertDetailWidth == 280);
    assert(kAlertDetailHeight == 41);
    assert(kPageIndicatorY == 416);
    assert(kBatteryY == 432);

    assert(kStateLabelY - kAlertDividerY == 12);
    assert(kAlertProjectY - kTaskIndexY == 26);
    assert(kAlertDetailY - kAlertProjectY == 31);
    assert(kPageIndicatorY - (kAlertDetailY + kAlertDetailHeight) == 12);
    assert(kBatteryY - kPageIndicatorY == 16);
    assert(kBatteryY - kAlertDetailY == 69);
    assert(kBatteryY < kScreenSize);

    assert(kPanelGraphite == 0x27303E);
    assert(kBeaconViolet == 0x7C9DFF);
    assert(kRunningCyan == 0x49C9EE);
    assert(kQuotaMint == 0x53D7A4);
    assert(kSignalWhite == 0xF2F6F8);
    assert(kEtchedGray == 0x788391);
    assert(kAlertVermilion == 0xFF6248);
}
