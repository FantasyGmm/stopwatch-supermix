#pragma once

#include <cstdint>


enum class StatusPageSwipeResult {
    Stay,
    Codex,
    PinnedMicrophone,
    Trae,
    OpenTaskDeck,
};


class StatusPageSwipeLogic {
public:
    void press(int x, int y, uint32_t now_ms);
    void move(int x, int y);
    bool task_deck_drag_active(int current_page) const;
    int task_deck_drag_offset(int current_page) const;
    StatusPageSwipeResult release(uint32_t now_ms, int current_page);
    void cancel();

private:
    enum class Axis {
        Undecided,
        Horizontal,
        Vertical,
    };

    bool _active = false;
    Axis _axis = Axis::Undecided;
    int _start_x = 0;
    int _start_y = 0;
    int _last_x = 0;
    int _last_y = 0;
    uint32_t _start_ms = 0;
};
