#pragma once

#include <cstdint>

// A failed read immediately invalidates the displayed value. Each sensor has an
// independent state so an unplugged pressure sensor cannot hide valid SHT30 data.
enum class EnvSensorState : uint8_t {
    NotDetected,
    Initializing,
    Ready,
    ReadError,
    Unavailable,
};

struct EnvSensorStatus {
    EnvSensorState state = EnvSensorState::NotDetected;
    bool has_sample = false;
    uint32_t sampled_at_ms = 0;
};

struct EnvSnapshot {
    static constexpr uint32_t stale_after_ms = 5000;

    EnvSensorStatus climate;
    EnvSensorStatus pressure;
    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;
    float pressure_hpa = 0.0f;
    bool monitoring = false;
};
