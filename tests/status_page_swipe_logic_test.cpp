#include "apps/app_codex_status/status_page_swipe_logic.h"

#include <cassert>


static bool right_to_left_fast_swipe_moves_codex_to_trae()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(260, 218);
    return swipe.release(1200, 0) == StatusPageSwipeResult::Trae;
}


static bool left_to_right_fast_swipe_moves_trae_to_codex()
{
    StatusPageSwipeLogic swipe;
    swipe.press(160, 220, 1000);
    swipe.move(200, 222);
    return swipe.release(1260, 1) == StatusPageSwipeResult::Codex;
}


static bool long_horizontal_drag_moves_page()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(199, 240);
    return swipe.release(1600, 0) == StatusPageSwipeResult::Trae;
}


static bool vertical_drag_does_not_move_page()
{
    StatusPageSwipeLogic swipe;
    swipe.press(220, 100, 1000);
    swipe.move(180, 220);
    return swipe.release(1200, 0) == StatusPageSwipeResult::Stay;
}


static bool short_slow_drag_snaps_back()
{
    StatusPageSwipeLogic swipe;
    swipe.press(220, 220, 1000);
    swipe.move(170, 218);
    return swipe.release(1400, 0) == StatusPageSwipeResult::Stay;
}


static void assert_codex_page_does_not_wrap_left()
{
    StatusPageSwipeLogic swipe;
    swipe.press(160, 220, 1000);
    swipe.move(210, 220);
    assert(swipe.release(1200, 0) == StatusPageSwipeResult::Stay);
}


static void assert_trae_page_does_not_wrap_right()
{
    StatusPageSwipeLogic swipe;
    swipe.press(300, 220, 1000);
    swipe.move(250, 220);
    assert(swipe.release(1200, 1) == StatusPageSwipeResult::Stay);
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
    assert(left_to_right_fast_swipe_moves_trae_to_codex());
    assert(long_horizontal_drag_moves_page());
    assert(vertical_drag_does_not_move_page());
    assert(short_slow_drag_snaps_back());
    assert_codex_page_does_not_wrap_left();
    assert_trae_page_does_not_wrap_right();
    assert_cancel_clears_active_gesture();
}
