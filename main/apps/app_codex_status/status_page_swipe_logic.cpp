#include "status_page_swipe_logic.h"

#include <algorithm>
#include <cstdlib>


namespace {

constexpr int kFastSwipeMinDistance = 36;
constexpr int kDragSwipeMinDistance = 100;
constexpr int kAxisLockDistance = 12;
constexpr uint32_t kFastSwipeMaxMs = 260;
constexpr int kDeckMinX = 183;
constexpr int kDeckMaxX = 283;
constexpr int kDeckMinY = 423;
constexpr int kDeckMaxY = 465;
constexpr int kViewportHeight = 466;

}  // namespace


void StatusPageSwipeLogic::press(
    int x,
    int y,
    uint32_t now_ms,
    bool trae_task_candidate
)
{
    _active = true;
    _axis = Axis::Undecided;
    _start_x = x;
    _start_y = y;
    _last_x = x;
    _last_y = y;
    _start_ms = now_ms;
    _trae_task_candidate = trae_task_candidate;
}


void StatusPageSwipeLogic::move(int x, int y)
{
    if (!_active) {
        return;
    }

    _last_x = x;
    _last_y = y;
    if (_axis != Axis::Undecided) {
        return;
    }

    const int abs_x = std::abs(_last_x - _start_x);
    const int abs_y = std::abs(_last_y - _start_y);
    if (std::max(abs_x, abs_y) < kAxisLockDistance) {
        return;
    }
    _axis = abs_x > abs_y ? Axis::Horizontal : Axis::Vertical;
}


bool StatusPageSwipeLogic::task_deck_drag_active(int current_page) const
{
    return _active && current_page == 1 && _axis == Axis::Vertical &&
        _start_x >= kDeckMinX && _start_x <= kDeckMaxX &&
        _start_y >= kDeckMinY && _start_y <= kDeckMaxY &&
        _last_y < _start_y;
}


int StatusPageSwipeLogic::task_deck_drag_offset(int current_page) const
{
    if (!task_deck_drag_active(current_page)) {
        return 0;
    }
    return std::clamp(_start_y - _last_y, 0, kViewportHeight);
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
    const bool fast_swipe =
        abs_x >= kFastSwipeMinDistance && elapsed_ms <= kFastSwipeMaxMs;
    const bool long_drag = abs_x >= kDragSwipeMinDistance;

    if (_axis == Axis::Vertical) {
        const bool fast_vertical =
            abs_y >= kFastSwipeMinDistance && elapsed_ms <= kFastSwipeMaxMs;
        const bool long_vertical = abs_y >= kDragSwipeMinDistance;
        if (current_page == 2 && _trae_task_candidate &&
            (fast_vertical || long_vertical)) {
            return dy < 0 ? StatusPageSwipeResult::TraeTasksForward
                          : StatusPageSwipeResult::TraeTasksBackward;
        }
        const int upward_distance = -dy;
        const bool in_deck_zone =
            _start_x >= kDeckMinX && _start_x <= kDeckMaxX &&
            _start_y >= kDeckMinY && _start_y <= kDeckMaxY;
        const bool fast_upward =
            upward_distance >= kFastSwipeMinDistance && elapsed_ms <= kFastSwipeMaxMs;
        const bool long_upward = upward_distance >= kDragSwipeMinDistance;
        if (current_page == 1 && in_deck_zone && (fast_upward || long_upward)) {
            return StatusPageSwipeResult::OpenTaskDeck;
        }
        return StatusPageSwipeResult::Stay;
    }

    if (_axis != Axis::Horizontal || (!fast_swipe && !long_drag)) {
        return StatusPageSwipeResult::Stay;
    }
    if (dx < 0 && current_page == 0) {
        return StatusPageSwipeResult::Codex;
    }
    if (dx < 0 && current_page == 1) {
        return StatusPageSwipeResult::Trae;
    }
    if (dx > 0 && current_page == 2) {
        return StatusPageSwipeResult::Codex;
    }
    if (dx > 0 && current_page == 1) {
        return StatusPageSwipeResult::PinnedMicrophone;
    }
    return StatusPageSwipeResult::Stay;
}


void StatusPageSwipeLogic::cancel()
{
    _active = false;
    _axis = Axis::Undecided;
    _trae_task_candidate = false;
}
