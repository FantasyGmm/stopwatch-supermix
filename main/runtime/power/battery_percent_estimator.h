#pragma once

#include <cstdint>

uint8_t BatteryPercentFromMillivolts(uint16_t millivolts);

class BatteryPercentEstimator {
public:
    uint8_t update(uint16_t filtered_millivolts, bool allow_increase);
    void reset();

private:
    bool _initialized = false;
    uint8_t _percent  = 0;
};
