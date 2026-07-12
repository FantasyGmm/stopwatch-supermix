#include "apps/app_codex_status/codex_status_ui_layout.h"

#include <cassert>

int main()
{
    using namespace codex_status_ui;

    assert(kScreenSize == 466);
    assert(kModelInstrument.x == 54);
    assert(kEffortInstrument.x == 296);
    assert(kModelInstrument.w == kEffortInstrument.w);
    assert(kModelInstrument.h == kEffortInstrument.h);
    assert(kModelInstrument.x + kModelInstrument.w +
               kEffortInstrument.x ==
           kScreenSize);

    assert(kQuotaX >= 42);
    assert(kQuotaX + kQuotaWidth <= kScreenSize - 42);
    assert(kFiveHourY < kWeekY);
    assert(kWeekY < kAlertDividerY);
    assert(kAlertDividerY < kAlertHeadingY);
    assert(kAlertHeadingY < kAlertProjectY);
    assert(kAlertProjectY < kAlertDetailY);
    assert(kAlertDetailY < kBatteryY);

    assert(kAlertHeadingWidth <= 310);
    assert(kAlertProjectWidth <= 340);
    assert(kAlertDetailWidth <= 320);
    assert(kBatteryY < kScreenSize);
}
