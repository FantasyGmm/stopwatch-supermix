#include "battery_percent_estimator.h"

#include <array>
#include <cstddef>

namespace {

struct BatteryCurvePoint {
    uint16_t millivolts;
    uint8_t percent;
};

// Reduced from the 2026-08-22 full-discharge calibration. Keeping a reserve
// below 3300 mV avoids reporting usable time after the voltage knee.
constexpr std::array<BatteryCurvePoint, 9> kDischargeCurve{{
    {3300, 0},
    {3372, 5},
    {3456, 10},
    {3536, 20},
    {3629, 40},
    {3670, 50},
    {3737, 60},
    {3922, 80},
    {4150, 100},
}};

}  // namespace

uint8_t BatteryPercentFromMillivolts(uint16_t millivolts)
{
    if (millivolts <= kDischargeCurve.front().millivolts) {
        return kDischargeCurve.front().percent;
    }
    if (millivolts >= kDischargeCurve.back().millivolts) {
        return kDischargeCurve.back().percent;
    }

    for (std::size_t index = 1; index < kDischargeCurve.size(); ++index) {
        const auto& upper = kDischargeCurve[index];
        if (millivolts > upper.millivolts) {
            continue;
        }

        const auto& lower           = kDischargeCurve[index - 1];
        const uint32_t voltage_span = upper.millivolts - lower.millivolts;
        const uint32_t voltage_step = millivolts - lower.millivolts;
        const uint32_t percent_span = upper.percent - lower.percent;
        const uint32_t interpolated =
            static_cast<uint32_t>(lower.percent) +
            (voltage_step * percent_span + voltage_span / 2U) / voltage_span;
        return static_cast<uint8_t>(interpolated);
    }

    return kDischargeCurve.back().percent;
}

uint8_t BatteryPercentEstimator::update(uint16_t filtered_millivolts, bool allow_increase)
{
    const uint8_t candidate = BatteryPercentFromMillivolts(filtered_millivolts);
    if (!_initialized || allow_increase || candidate <= _percent) {
        _percent     = candidate;
        _initialized = true;
    }
    return _percent;
}

void BatteryPercentEstimator::reset()
{
    _initialized = false;
    _percent     = 0;
}
