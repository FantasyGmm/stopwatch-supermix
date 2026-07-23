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
    assert(kFiveHourY == 220);
    assert(kWeekY == 260);
    assert(kFiveHourY < kWeekY);
    assert(kWeekY < kAlertDividerY);
    assert(kAlertDividerY < kAlertHeadingY);
    assert(kAlertHeadingY < kAlertProjectY);
    assert(kAlertProjectY < kAlertDetailY);
    assert(kAlertDetailY < kBatteryY);

    assert(kAlertDividerX == 82);
    assert(kAlertDividerY == 304);
    assert(kAlertDividerWidth == 302);
    assert(kAlertHeadingY == 328);
    assert(kAlertHeadingWidth == 310);
    assert(kAlertProjectY == 357);
    assert(kAlertProjectWidth == 340);
    assert(kAlertDetailY == 389);
    assert(kAlertDetailWidth == 320);
    assert(kBatteryY == 438);

    assert(kAlertHeadingY - kAlertDividerY == 24);
    assert(kAlertProjectY - kAlertHeadingY == 29);
    assert(kAlertDetailY - kAlertProjectY == 32);
    assert(kBatteryY - kAlertDetailY == 49);
    assert(kBatteryY < kScreenSize);

    assert(kPanelGraphite == 0x27303E);
    assert(kBeaconViolet == 0x7C9DFF);
    assert(kRunningCyan == 0x49C9EE);
    assert(kQuotaMint == 0x53D7A4);
    assert(kSignalWhite == 0xF2F6F8);
    assert(kEtchedGray == 0x788391);
    assert(kAlertVermilion == 0xFF6248);
    assert(kSecondaryText == 0xB8C2CC);
}
