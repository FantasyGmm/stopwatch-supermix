#include "main/apps/common/edge_control/edge_control_logic.h"

#include <cassert>


static void assert_touch_exclusion_is_inclusive_and_scoped()
{
    EdgeControlLogic logic;
    const EdgeControlRect exclusion = {183, 423, 283, 465};

    const int boundary_points[][2] = {
        {183, 423}, {283, 423}, {183, 465}, {283, 465},
    };
    for (const auto& point : boundary_points) {
        logic.press(point[0], point[1], 50, 60, 1000, &exclusion);
        const EdgeControlUpdate update = logic.move(point[0], point[1] - 50, 1040);
        assert(!update.activated);
        assert(!update.consumed);
        assert(!logic.release(1080).consumed);
    }

    logic.press(182, 440, 50, 60, 2000, &exclusion);
    assert(logic.move(182, 390, 2040).activated);
    assert(logic.release(2080).consumed);

    logic.press(284, 440, 50, 60, 3000, &exclusion);
    assert(logic.move(284, 390, 3040).activated);
    assert(logic.release(3080).consumed);

    logic.press(233, 422, 50, 60, 4000, &exclusion);
    assert(logic.move(233, 370, 4040).activated);
    assert(logic.release(4080).consumed);

    logic.press(233, 466, 50, 60, 5000, &exclusion);
    assert(logic.move(233, 410, 5040).activated);
    assert(logic.release(5080).consumed);

    // Clearing the app-specific exclusion restores the normal global gesture.
    logic.press(233, 440, 50, 60, 6000, nullptr);
    assert(logic.move(233, 390, 6040).activated);
    assert(logic.release(6080).consumed);
}


static void assert_exclusion_changes_cancel_only_unowned_candidates()
{
    EdgeControlLogic logic;
    logic.press(80, 300, 50, 60, 1000);
    assert(logic.candidate());
    logic.cancelCandidate();
    assert(!logic.candidate());
    assert(!logic.move(80, 240, 1040).activated);
    assert(!logic.release(1080).consumed);

    logic.press(80, 300, 50, 60, 2000);
    assert(logic.move(80, 250, 2040).activated);
    assert(logic.active());
    logic.cancelCandidate();
    assert(logic.active());
    assert(logic.release(2080).consumed);
}

int main()
{
    assert_touch_exclusion_is_inclusive_and_scoped();
    assert_exclusion_changes_cancel_only_unowned_candidates();
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
    assert(volume.value == 37);
    assert(volume.hardware_value == 35);
    assert(volume.apply);
    assert(volume.consumed);

    const EdgeControlUpdate volume_visual_only = logic.move(382, 344, 2050);
    assert(volume_visual_only.value == 40);
    assert(volume_visual_only.hardware_value == 40);
    assert(!volume_visual_only.apply);

    EdgeControlApplyQueue apply_queue;
    apply_queue.push(volume);
    assert(apply_queue.pending());
    const EdgeControlHardwareUpdate hardware_update = apply_queue.take();
    assert(hardware_update.side == EdgeControlSide::Volume);
    assert(hardware_update.value == 35);
    assert(!hardware_update.persist);
    assert(!apply_queue.pending());

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
