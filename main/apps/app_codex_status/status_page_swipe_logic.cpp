#include "status_page_swipe_logic.h"

#include <cstdlib>


namespace {

constexpr int kFastSwipeMinDistance = 36;
constexpr int kDragSwipeMinDistance = 100;
constexpr uint32_t kFastSwipeMaxMs = 260;

}  // namespace


void StatusPageSwipeLogic::press(int x, int y, uint32_t now_ms)
{
    _active = true;
    _start_x = x;
    _start_y = y;
    _last_x = x;
    _last_y = y;
    _start_ms = now_ms;
}


void StatusPageSwipeLogic::move(int x, int y)
{
    if (!_active) {
        return;
    }

    _last_x = x;
    _last_y = y;
}


StatusPageSwipeResult StatusPageSwipeLogic::release(uint32_t now_ms, int current_page)
{
    if (!_active) {
        return StatusPageSwipeResult::Stay;
    }

    _active = false;
    const int dx = _last_x - _start_x;
    const int dy = _last_y - _start_y;
    const int abs_x = std::abs(dx);
    const int abs_y = std::abs(dy);
    const uint32_t elapsed_ms = now_ms - _start_ms;
    const bool horizontal = abs_x > abs_y;
    const bool fast_swipe =
        horizontal && abs_x >= kFastSwipeMinDistance && elapsed_ms <= kFastSwipeMaxMs;
    const bool long_drag = horizontal && abs_x >= kDragSwipeMinDistance;

    if (!fast_swipe && !long_drag) {
        return StatusPageSwipeResult::Stay;
    }
    if (dx < 0 && current_page == 0) {
        return StatusPageSwipeResult::Trae;
    }
    if (dx > 0 && current_page == 1) {
        return StatusPageSwipeResult::Codex;
    }
    return StatusPageSwipeResult::Stay;
}


void StatusPageSwipeLogic::cancel()
{
    _active = false;
}
