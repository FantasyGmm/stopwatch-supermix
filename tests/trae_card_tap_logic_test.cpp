#include "apps/app_codex_status/trae_card_tap_logic.h"

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
}
