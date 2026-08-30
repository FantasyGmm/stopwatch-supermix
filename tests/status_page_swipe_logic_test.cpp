#include "apps/app_codex_status/status_page_swipe_logic.h"

#include <cassert>


static bool right_to_left_fast_swipe_moves_codex_to_trae()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(260, 218);
    return swipe.release(1200, 1) == StatusPageSwipeResult::Trae;
}


static bool right_to_left_fast_swipe_moves_pinned_microphone_to_codex()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(260, 218);
    return swipe.release(1200, 0) == StatusPageSwipeResult::Codex;
}


static bool left_to_right_fast_swipe_moves_trae_to_codex()
{
    StatusPageSwipeLogic swipe;
    swipe.press(160, 220, 1000);
    swipe.move(200, 222);
    return swipe.release(1260, 2) == StatusPageSwipeResult::Codex;
}


static bool left_to_right_fast_swipe_moves_codex_to_pinned_microphone()
{
    StatusPageSwipeLogic swipe;
    swipe.press(160, 220, 1000);
    swipe.move(200, 222);
    return swipe.release(1260, 1) == StatusPageSwipeResult::PinnedMicrophone;
}


static bool long_horizontal_drag_moves_page()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(199, 240);
    return swipe.release(1600, 1) == StatusPageSwipeResult::Trae;
}


static bool vertical_drag_does_not_move_page()
{
    StatusPageSwipeLogic swipe;
    swipe.press(220, 100, 1000);
    swipe.move(180, 220);
    return swipe.release(1200, 0) == StatusPageSwipeResult::Stay;
}


static void assert_trae_task_vertical_swipes_scroll_one_row()
{
    StatusPageSwipeLogic upward;
    upward.press(220, 260, 1000, true);
    upward.move(222, 220);
    assert(
        upward.release(1200, 2) ==
        StatusPageSwipeResult::TraeTasksForward
    );

    StatusPageSwipeLogic downward;
    downward.press(220, 220, 1000, true);
    downward.move(218, 260);
    assert(
        downward.release(1200, 2) ==
        StatusPageSwipeResult::TraeTasksBackward
    );
}


static void assert_trae_vertical_swipe_requires_task_region_ownership()
{
    StatusPageSwipeLogic outside;
    outside.press(80, 260, 1000, false);
    outside.move(82, 220);
    assert(outside.release(1200, 2) == StatusPageSwipeResult::Stay);

    StatusPageSwipeLogic short_drag;
    short_drag.press(220, 260, 1000, true);
    short_drag.move(220, 230);
    assert(short_drag.release(1400, 2) == StatusPageSwipeResult::Stay);
}


static bool short_slow_drag_snaps_back()
{
    StatusPageSwipeLogic swipe;
    swipe.press(220, 220, 1000);
    swipe.move(170, 218);
    return swipe.release(1400, 0) == StatusPageSwipeResult::Stay;
}


static void assert_pinned_page_does_not_wrap_right()
{
    StatusPageSwipeLogic swipe;
    swipe.press(160, 220, 1000);
    swipe.move(210, 220);
    assert(swipe.release(1200, 0) == StatusPageSwipeResult::Stay);
}


static void assert_bottom_center_upward_swipe_opens_codex_deck()
{
    StatusPageSwipeLogic fast;
    fast.press(233, 440, 1000);
    fast.move(235, 403);
    assert(fast.release(1260, 1) == StatusPageSwipeResult::OpenTaskDeck);

    StatusPageSwipeLogic long_drag;
    long_drag.press(183, 465, 1000);
    long_drag.move(185, 365);
    assert(long_drag.release(1600, 1) == StatusPageSwipeResult::OpenTaskDeck);
}


static void assert_codex_deck_drag_reports_continuous_finger_progress()
{
    StatusPageSwipeLogic swipe;
    swipe.press(233, 440, 1000);
    assert(!swipe.task_deck_drag_active(1));
    assert(swipe.task_deck_drag_offset(1) == 0);

    swipe.move(234, 427);
    assert(swipe.task_deck_drag_active(1));
    assert(swipe.task_deck_drag_offset(1) == 13);

    swipe.move(235, 390);
    assert(swipe.task_deck_drag_active(1));
    assert(swipe.task_deck_drag_offset(1) == 50);

    swipe.move(235, -100);
    assert(swipe.task_deck_drag_offset(1) == 466);
    assert(!swipe.task_deck_drag_active(0));
    assert(swipe.task_deck_drag_offset(2) == 0);
}


static void assert_non_entry_gestures_never_report_deck_progress()
{
    StatusPageSwipeLogic horizontal;
    horizontal.press(233, 440, 1000);
    horizontal.move(190, 438);
    assert(!horizontal.task_deck_drag_active(1));
    assert(horizontal.task_deck_drag_offset(1) == 0);

    StatusPageSwipeLogic outside;
    outside.press(150, 440, 1000);
    outside.move(150, 380);
    assert(!outside.task_deck_drag_active(1));
    assert(outside.task_deck_drag_offset(1) == 0);
}


static void assert_deck_entry_safe_zone_is_inclusive_and_codex_only()
{
    StatusPageSwipeLogic right_boundary;
    right_boundary.press(283, 423, 1000);
    right_boundary.move(283, 380);
    assert(right_boundary.release(1200, 1) == StatusPageSwipeResult::OpenTaskDeck);

    StatusPageSwipeLogic outside_x;
    outside_x.press(182, 440, 1000);
    outside_x.move(182, 390);
    assert(outside_x.release(1200, 1) == StatusPageSwipeResult::Stay);

    StatusPageSwipeLogic outside_y;
    outside_y.press(233, 422, 1000);
    outside_y.move(233, 370);
    assert(outside_y.release(1200, 1) == StatusPageSwipeResult::Stay);

    StatusPageSwipeLogic trae;
    trae.press(233, 440, 1000);
    trae.move(233, 390);
    assert(trae.release(1200, 2) == StatusPageSwipeResult::Stay);
}


static void assert_vertical_owner_never_becomes_horizontal_page_swipe()
{
    StatusPageSwipeLogic swipe;
    swipe.press(233, 440, 1000);
    swipe.move(235, 425);  // Vertical ownership after the 12 px dead zone.
    swipe.move(100, 420);
    assert(swipe.release(1200, 1) == StatusPageSwipeResult::Stay);
}


static void assert_horizontal_owner_preserves_existing_paging()
{
    StatusPageSwipeLogic swipe;
    swipe.press(233, 440, 1000);
    swipe.move(218, 438);  // Horizontal ownership after the 12 px dead zone.
    swipe.move(180, 360);
    assert(swipe.release(1200, 1) == StatusPageSwipeResult::Trae);
}


static void assert_downward_safe_zone_and_edge_vertical_gestures_stay()
{
    StatusPageSwipeLogic downward;
    downward.press(233, 423, 1000);
    downward.move(233, 465);
    assert(downward.release(1200, 1) == StatusPageSwipeResult::Stay);

    StatusPageSwipeLogic edge;
    edge.press(10, 440, 1000);
    edge.move(10, 380);
    assert(edge.release(1200, 1) == StatusPageSwipeResult::Stay);
}


static void assert_trae_page_does_not_wrap_left()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(250, 220);
    assert(swipe.release(1200, 2) == StatusPageSwipeResult::Stay);
}


static void assert_cancel_clears_active_gesture()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(200, 220);
    swipe.cancel();
    assert(swipe.release(1200, 0) == StatusPageSwipeResult::Stay);
}


int main()
{
    assert(right_to_left_fast_swipe_moves_codex_to_trae());
    assert(right_to_left_fast_swipe_moves_pinned_microphone_to_codex());
    assert(left_to_right_fast_swipe_moves_trae_to_codex());
    assert(left_to_right_fast_swipe_moves_codex_to_pinned_microphone());
    assert(long_horizontal_drag_moves_page());
    assert(vertical_drag_does_not_move_page());
    assert_trae_task_vertical_swipes_scroll_one_row();
    assert_trae_vertical_swipe_requires_task_region_ownership();
    assert(short_slow_drag_snaps_back());
    assert_pinned_page_does_not_wrap_right();
    assert_trae_page_does_not_wrap_left();
    assert_bottom_center_upward_swipe_opens_codex_deck();
    assert_codex_deck_drag_reports_continuous_finger_progress();
    assert_non_entry_gestures_never_report_deck_progress();
    assert_deck_entry_safe_zone_is_inclusive_and_codex_only();
    assert_vertical_owner_never_becomes_horizontal_page_swipe();
    assert_horizontal_owner_preserves_existing_paging();
    assert_downward_safe_zone_and_edge_vertical_gestures_stay();
    assert_cancel_clears_active_gesture();
}
