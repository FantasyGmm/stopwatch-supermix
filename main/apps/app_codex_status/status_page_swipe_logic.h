#pragma once

#include <cstdint>


enum class StatusPageSwipeResult {
    Stay,
    Codex,
    PinnedMicrophone,
    Trae,
};


class StatusPageSwipeLogic {
public:
    void press(int x, int y, uint32_t now_ms);
    void move(int x, int y);
    StatusPageSwipeResult release(uint32_t now_ms, int current_page);
    void cancel();

private:
    bool _active = false;
    int _start_x = 0;
    int _start_y = 0;
    int _last_x = 0;
    int _last_y = 0;
    uint32_t _start_ms = 0;
};
