#include "battery_calibration_schedule.h"

#include <cstdio>

void BatteryCalibrationSchedule::start(uint32_t now_ms)
{
    _active = true;
    _has_revision = false;
    _started_ms = now_ms;
    _last_sample_ms = now_ms;
    _last_revision = 0;
    _sample_count = 0;
}

void BatteryCalibrationSchedule::stop()
{
    _active = false;
}

BatteryCalibrationDecision BatteryCalibrationSchedule::update(
    const BatteryCalibrationInput& input)
{
    BatteryCalibrationDecision decision;
    if (!_active || (_has_revision && input.revision == _last_revision)) {
        return decision;
    }

    if (_has_revision && static_cast<uint32_t>(input.now_ms - _last_sample_ms) < sampleIntervalMs) {
        return decision;
    }

    decision.append = true;
    decision.elapsed_seconds = static_cast<uint32_t>(input.now_ms - _started_ms) / 1000U;
    _has_revision = true;
    _last_revision = input.revision;
    _last_sample_ms = input.now_ms;
    ++_sample_count;
    if (_sample_count >= maxSamples) {
        _active = false;
        decision.stop = true;
    }
    return decision;
}

bool BatteryCalibrationSchedule::active() const
{
    return _active;
}

uint32_t BatteryCalibrationSchedule::sampleCount() const
{
    return _sample_count;
}

bool FormatBatteryCalibrationCsvLine(const BatteryCalibrationInput& input,
                                     uint32_t elapsed_seconds,
                                     char* output,
                                     std::size_t output_size)
{
    if (output == nullptr || output_size == 0) {
        return false;
    }
    const int written = std::snprintf(output,
                                      output_size,
                                      "%lu,%u,%u,%u,%u\n",
                                      static_cast<unsigned long>(elapsed_seconds),
                                      static_cast<unsigned>(input.raw_mv),
                                      static_cast<unsigned>(input.filtered_mv),
                                      static_cast<unsigned>(input.percent),
                                      input.external_power ? 1U : 0U);
    return written >= 0 && static_cast<std::size_t>(written) < output_size;
}
