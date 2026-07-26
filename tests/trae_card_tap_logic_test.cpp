#include "apps/app_codex_status/trae_card_tap_logic.h"
#include "apps/app_codex_status/status_page_swipe_logic.h"

#include <cassert>

int main()
{
    TraeCardTapLogic tap;
    tap.press(200, 300, 1000, true);
    tap.move(205, 304);
    assert(tap.release(1250));

    tap.press(200, 300, 2000, true);
    tap.move(245, 302);
    assert(!tap.release(2200));

    tap.press(80, 120, 3000, false);
    assert(!tap.release(3100));

    tap.press(200, 300, 4000, true);
    tap.cancel();
    assert(!tap.release(4100));

    StatusPageTouchWakeGate gate;
    TraeCardTapLogic wake_tap;
    gate.suppressUntilRelease();
    assert(gate.consume(true));
    assert(gate.consume(true));
    assert(gate.consume(false));
    assert(!wake_tap.release(5100));

    assert(!gate.consume(true));
    wake_tap.press(200, 300, 6000, true);
    assert(!gate.consume(false));
    assert(wake_tap.release(6100));
}
