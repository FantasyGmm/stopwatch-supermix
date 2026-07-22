#include "apps/app_codex_status/codex_status_ui_layout.h"

#include <cassert>

int main()
{
    using namespace codex_status_ui;

    assert(kScreenSize == 466);
    assert(kPetDock.x == 52);
    assert(kPetDock.y == 112);
    assert(kPetDock.w == 104);
    assert(kPetDock.h == 104);
    assert(kPetX == 56);
    assert(kPetY == 112);
    assert(kPetWidth == 96);
    assert(kPetHeight == 104);

    assert(kBeaconSpine.x == 170);
    assert(kBeaconSpine.y == kPetDock.y);
    assert(kBeaconSpine.w == 2);
    assert(kBeaconSpine.h == kPetDock.h);
    assert(kPetDock.x + kPetDock.w < kBeaconSpine.x);

    assert(kTelemetryX == 190);
    assert(kTelemetryWidth == 224);
    assert(kTelemetryX > kBeaconSpine.x + kBeaconSpine.w);
    assert(kTelemetryX + kTelemetryWidth <= kScreenSize - 52);
    assert(kModelCaptionY < kModelValueY);
    assert(kModelValueY < kEffortCaptionY);
    assert(kEffortCaptionY < kEffortValueY);

    assert(kQuotaX == 55);
    assert(kQuotaWidth == 356);
    assert(kQuotaLabelWidth == 32);
    assert(kQuotaPercentWidth == 70);
    assert(kQuotaBarX == 180);
    assert(kQuotaBarWidth == 231);
    assert(kQuotaBarX + kQuotaBarWidth == kQuotaX + kQuotaWidth);
    assert(kFiveHourY < kWeekY);
    assert(kWeekY < kAlertDividerY);
    assert(kAlertDividerY < kAlertHeadingY);
    assert(kAlertHeadingY < kAlertProjectY);
    assert(kAlertProjectY < kAlertDetailY);
    assert(kAlertDetailY < kBatteryY);

    assert(kAlertDividerX == 82);
    assert(kAlertDividerWidth == 302);
    assert(kAlertHeadingWidth == 310);
    assert(kAlertProjectWidth == 340);
    assert(kAlertDetailWidth == 320);
    assert(kBatteryY < kScreenSize);

    assert(kInstrumentBlack == 0x070A0F);
    assert(kPanelGraphite == 0x27303E);
    assert(kBeaconViolet == 0x7C9DFF);
    assert(kQuotaMint == 0x53D7A4);
    assert(kSignalWhite == 0xF2F6F8);
    assert(kEtchedGray == 0x788391);
    assert(kAlertVermilion == 0xFF6248);
}
