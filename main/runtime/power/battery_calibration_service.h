#pragma once

#include "battery_calibration_schedule.h"

#include <cstdint>

class BatteryCalibrationService {
public:
    bool start();
    void update();
    bool exportToLog() const;

    bool active() const;
    uint32_t sampleCount() const;

private:
    BatteryCalibrationSchedule _schedule;
};

BatteryCalibrationService& GetBatteryCalibrationService();
