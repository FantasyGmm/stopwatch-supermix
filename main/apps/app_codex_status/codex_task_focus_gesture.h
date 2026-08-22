#pragma once

#include <algorithm>
#include <cstdlib>


namespace codex_task_focus_gesture {

enum class TaskFocusGestureAxis {
    Undecided,
    Horizontal,
    Vertical,
};


constexpr int kDeckHandleTouchLeft = 183;
constexpr int kDeckHandleTouchTop = 0;
constexpr int kDeckHandleTouchRight = 283;
constexpr int kDeckHandleTouchBottom = 48;


inline bool is_deck_handle_start(int x, int y)
{
    return x >= kDeckHandleTouchLeft &&
        x <= kDeckHandleTouchRight &&
        y >= kDeckHandleTouchTop &&
        y <= kDeckHandleTouchBottom;
}


inline TaskFocusGestureAxis update_axis(
    TaskFocusGestureAxis current,
    int total_dx,
    int total_dy,
    int dead_zone
)
{
    if (current != TaskFocusGestureAxis::Undecided) {
        return current;
    }
    const int abs_dx = std::abs(total_dx);
    const int abs_dy = std::abs(total_dy);
    if (abs_dx < dead_zone && abs_dy < dead_zone) {
        return current;
    }
    return abs_dx > abs_dy
        ? TaskFocusGestureAxis::Horizontal
        : TaskFocusGestureAxis::Vertical;
}


inline bool is_detail_return_gesture(
    int total_dx,
    int total_dy,
    TaskFocusGestureAxis axis,
    bool started_at_top,
    int threshold
)
{
    if (axis == TaskFocusGestureAxis::Horizontal) {
        return total_dx >= threshold &&
            std::abs(total_dx) > std::abs(total_dy);
    }
    return axis == TaskFocusGestureAxis::Vertical &&
        started_at_top && total_dy >= threshold &&
        std::abs(total_dy) >= std::abs(total_dx);
}


inline int task_stream_drag_offset(
    int total_dy,
    bool has_previous,
    bool has_next,
    int limit
)
{
    int offset = std::clamp(total_dy, -limit, limit);
    if (offset > 0 && !has_previous) {
        offset /= 4;
    } else if (offset < 0 && !has_next) {
        offset /= 4;
    }
    return offset;
}


inline int task_deck_handle_drag_offset(int total_dy, int limit)
{
    return std::clamp(total_dy, 0, limit);
}


inline bool should_auto_dismiss_task_deck(
    int total_dx,
    int total_dy,
    TaskFocusGestureAxis axis,
    bool started_on_handle,
    int drag_offset,
    int dismiss_offset
)
{
    return started_on_handle &&
        axis == TaskFocusGestureAxis::Vertical &&
        total_dy > 0 &&
        drag_offset >= dismiss_offset &&
        std::abs(total_dy) >= std::abs(total_dx);
}

}  // namespace codex_task_focus_gesture
