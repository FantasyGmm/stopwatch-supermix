#pragma once

#include <cstdint>

constexpr uint32_t kScreenOffMainLoopDelayMs = 10U;
constexpr uint32_t kScreenOffBackgroundIntervalMs = 50U;
constexpr uint32_t kActiveLvglRuntimePeriodMs = 10U;
constexpr uint32_t kScreenOffLvglRuntimePeriodMs = 50U;

constexpr uint32_t screen_off_main_loop_delay_ms(bool screen_off)
{
    return screen_off ? kScreenOffMainLoopDelayMs : 0U;
}

constexpr uint32_t lvgl_runtime_period_ms(bool screen_off)
{
    return screen_off ? kScreenOffLvglRuntimePeriodMs
                      : kActiveLvglRuntimePeriodMs;
}

class ScreenOffRuntimeCadence {
public:
    bool background_work_due(bool screen_off, uint32_t now_ms)
    {
        if (!screen_off) {
            _screen_off = false;
            _last_background_work_ms = now_ms;
            return true;
        }
        if (!_screen_off) {
            _screen_off = true;
            _last_background_work_ms = now_ms;
            return true;
        }
        if (now_ms - _last_background_work_ms <
            kScreenOffBackgroundIntervalMs) {
            return false;
        }
        _last_background_work_ms = now_ms;
        return true;
    }

private:
    uint32_t _last_background_work_ms = 0;
    bool _screen_off = false;
};
