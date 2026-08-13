#pragma once

#include <cstdint>

enum class CodexButtonAction : uint8_t {
    None,
    Submit,
    ForceSteer,
};

class CodexButtonGesture {
public:
    static constexpr uint32_t kDoubleClickMs = 300;

    void reset();
    CodexButtonAction update(bool short_click, bool right_pressed,
                             bool hold_started, uint32_t now_ms,
                             bool suppressed = false);

private:
    bool _single_pending = false;
    bool _second_press_latched = false;
    uint32_t _first_click_ms = 0;
};
