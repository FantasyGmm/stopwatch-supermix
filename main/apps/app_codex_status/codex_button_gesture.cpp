#include "codex_button_gesture.h"

void CodexButtonGesture::reset() {
    _single_pending = false;
    _second_press_latched = false;
    _first_click_ms = 0;
}

CodexButtonAction CodexButtonGesture::update(bool short_click,
                                              bool right_pressed,
                                              bool hold_started,
                                              uint32_t now_ms,
                                              bool suppressed) {
    if (suppressed || hold_started) {
        reset();
        return CodexButtonAction::None;
    }

    if (!_single_pending) {
        if (short_click) {
            _single_pending = true;
            _first_click_ms = now_ms;
        }
        return CodexButtonAction::None;
    }

    if (_second_press_latched) {
        if (short_click) {
            reset();
            return CodexButtonAction::ForceSteer;
        }
        return CodexButtonAction::None;
    }

    const uint32_t elapsed_ms = now_ms - _first_click_ms;
    if (right_pressed && elapsed_ms < kDoubleClickMs) {
        _second_press_latched = true;
        return CodexButtonAction::None;
    }

    if (elapsed_ms >= kDoubleClickMs) {
        reset();
        return CodexButtonAction::Submit;
    }

    return CodexButtonAction::None;
}
