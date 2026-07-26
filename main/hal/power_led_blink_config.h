#pragma once

#include <cstdint>

constexpr int PowerLedBlinkIntervalMinSeconds     = 1;
constexpr int PowerLedBlinkIntervalMaxSeconds     = 10;
constexpr int PowerLedBlinkIntervalDefaultSeconds = 2;

constexpr int normalize_power_led_blink_interval_seconds(int seconds)
{
    return seconds < PowerLedBlinkIntervalMinSeconds
               ? PowerLedBlinkIntervalMinSeconds
               : (seconds > PowerLedBlinkIntervalMaxSeconds ? PowerLedBlinkIntervalMaxSeconds : seconds);
}

constexpr uint32_t power_led_blink_interval_ms(int seconds)
{
    return static_cast<uint32_t>(normalize_power_led_blink_interval_seconds(seconds)) * 1000U;
}
