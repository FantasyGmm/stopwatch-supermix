#pragma once

#include <cstddef>
#include <cstdint>

struct BatteryCalibrationInput {
    uint32_t now_ms = 0;
    uint32_t revision = 0;
    uint16_t raw_mv = 0;
    uint16_t filtered_mv = 0;
    uint8_t percent = 0;
    bool external_power = false;
};

struct BatteryCalibrationDecision {
    bool append = false;
    bool stop = false;
    uint32_t elapsed_seconds = 0;
};

class BatteryCalibrationSchedule {
public:
    static constexpr uint32_t sampleIntervalMs = 60000;
    static constexpr uint32_t maxSamples = 1440;

    void start(uint32_t now_ms);
    void stop();
    BatteryCalibrationDecision update(const BatteryCalibrationInput& input);

    bool active() const;
    uint32_t sampleCount() const;

private:
    bool _active = false;
    bool _has_revision = false;
    uint32_t _started_ms = 0;
    uint32_t _last_sample_ms = 0;
    uint32_t _last_revision = 0;
    uint32_t _sample_count = 0;
};

bool FormatBatteryCalibrationCsvLine(const BatteryCalibrationInput& input,
                                     uint32_t elapsed_seconds,
                                     char* output,
                                     std::size_t output_size);
