#include "main/apps/common/edge_control/edge_control_logic.h"

#include <cassert>

int main()
{
    EdgeControlLogic logic;

    logic.press(80, 300, 50, 60, 1000);
    const EdgeControlUpdate update = logic.move(82, 250, 1040);
    assert(update.side == EdgeControlSide::Brightness);
    assert(update.activated);
    assert(update.value > 50);
    assert(update.apply);
    assert(!update.persist);
    assert(update.consumed);
    assert(logic.active());
    assert(logic.current_value() == update.value);
    assert(logic.visible_side() == EdgeControlSide::Brightness);

    const EdgeControlUpdate released = logic.release(1100);
    assert(released.side == EdgeControlSide::Brightness);
    assert(released.value == update.value);
    assert(released.apply);
    assert(released.persist);
    assert(released.consumed);
    assert(!logic.active());
    assert(logic.visible_side() == EdgeControlSide::Brightness);
    assert(!logic.tick(1799).hide);
    assert(logic.tick(1800).hide);
    assert(logic.visible_side() == EdgeControlSide::None);
    assert(!logic.tick(1801).hide);

    logic.reset();
    logic.press(380, 300, 50, 60, 2000);
    const EdgeControlUpdate volume = logic.move(382, 350, 2040);
    assert(volume.side == EdgeControlSide::Volume);
    assert(volume.activated);
    assert(volume.value == 35);
    assert(volume.value % 5 == 0);
    assert(volume.apply);
    assert(volume.consumed);

    logic.reset();
    logic.press(80, 300, 50, 60, 2000);
    const EdgeControlUpdate cancelled = logic.move(130, 302, 2040);
    assert(cancelled.cancelled);
    assert(!cancelled.consumed);
    assert(!logic.release(2060).consumed);
    assert(!logic.active());
    assert(logic.visible_side() == EdgeControlSide::None);

    logic.reset();
    logic.press(80, 30, 50, 60, 3000);
    const EdgeControlUpdate excluded = logic.move(80, 100, 3040);
    assert(!excluded.activated);
    assert(!excluded.consumed);
    assert(!logic.active());

    logic.reset();
    logic.press(80, 300, 10, 100, 4000);
    assert(logic.move(80, 466, 4040).value == 10);

    logic.reset();
    logic.press(80, 300, 10, 100, 5000);
    assert(logic.move(80, 90, 5040).value == 100);

    logic.reset();
    logic.press(233, 300, 10, 10, 6000);
    const EdgeControlUpdate volume_min = logic.move(233, 466, 6040);
    assert(volume_min.side == EdgeControlSide::Volume);
    assert(volume_min.value == 0);

    logic.reset();
    logic.press(380, 300, 10, 100, 7000);
    assert(logic.move(380, 0, 7040).value == 100);

    logic.reset();
    logic.press(80, 300, 50, 60, 8000);
    const EdgeControlUpdate before_period = logic.move(80, 250, 8039);
    assert(before_period.activated);
    assert(!before_period.apply);
    const EdgeControlUpdate first_period = logic.move(80, 250, 8040);
    assert(first_period.apply);
    const EdgeControlUpdate before_next_period = logic.move(80, 230, 8079);
    assert(!before_next_period.apply);
    const EdgeControlUpdate next_period = logic.move(80, 230, 8080);
    assert(next_period.apply);
    const EdgeControlUpdate unchanged = logic.move(80, 230, 8120);
    assert(!unchanged.apply);
}
