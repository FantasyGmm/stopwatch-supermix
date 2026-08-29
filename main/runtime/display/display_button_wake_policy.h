#pragma once

constexpr bool shouldDeferRightButtonWake(
    bool display_was_off,
    bool left_pressed,
    bool right_pressed_edge,
    bool defer_right_button_wake)
{
    return display_was_off &&
           !left_pressed &&
           right_pressed_edge &&
           defer_right_button_wake;
}
