#pragma once

#include <cstdlib>


namespace codex_task_focus_gesture {

enum class TaskFocusGestureAxis {
    Undecided,
    Horizontal,
    Vertical,
};


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

}  // namespace codex_task_focus_gesture
