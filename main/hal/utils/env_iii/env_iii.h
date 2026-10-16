// SPDX-License-Identifier: MIT
// Includes M5Stack M5Unit-ENV QMP6988 calibration layout adaptation.
// Source commit: 737e5180ea7b84d8f22fe49ae4fc62cf06cbc3a2.
// See third_party_notices/m5unit-env.md.
#pragma once

#include <cstdint>

#include <driver/i2c_master.h>
#include <hal/env_sensor_snapshot.h>

class EnvIIIDriver {
public:
    explicit EnvIIIDriver(i2c_master_bus_handle_t bus);
    ~EnvIIIDriver();

    bool begin();
    void sample(EnvSnapshot& snapshot, uint32_t now_ms);
    void reset();

private:
    struct QmpCalibration {
        int32_t b00 = 0;
        int32_t bt1 = 0;
        int32_t bp1 = 0;
        int64_t bt2 = 0;
        int32_t b11 = 0;
        int32_t bp2 = 0;
        int32_t b12 = 0;
        int32_t b21 = 0;
        int32_t bp3 = 0;
        int32_t a0 = 0;
        int32_t a1 = 0;
        int32_t a2 = 0;
    };

    bool add_device(uint8_t address, i2c_master_dev_handle_t& device);
    bool can_attempt(uint32_t now_ms, bool& waiting, uint32_t retry_at_ms);
    void fail_climate(EnvSnapshot& snapshot, EnvSensorState state, uint32_t now_ms);
    void fail_pressure(EnvSnapshot& snapshot, EnvSensorState state, uint32_t now_ms);
    void sample_climate(EnvSnapshot& snapshot, uint32_t now_ms);
    void sample_pressure(EnvSnapshot& snapshot, uint32_t now_ms);
    bool probe_climate();
    bool read_climate(float& temperature_c, float& humidity_percent);
    bool initialize_pressure();
    bool read_pressure(float& temperature_c, float& pressure_hpa);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t sht30_device_ = nullptr;
    i2c_master_dev_handle_t qmp6988_device_ = nullptr;

    bool begun_ = false;
    bool reset_pending_ = true;
    bool climate_initialized_ = false;
    bool pressure_initialized_ = false;
    bool climate_ever_identified_ = false;
    bool pressure_ever_identified_ = false;
    bool climate_waiting_retry_ = false;
    bool pressure_waiting_retry_ = false;
    uint32_t climate_retry_at_ms_ = 0;
    uint32_t pressure_retry_at_ms_ = 0;
    EnvSensorState climate_state_ = EnvSensorState::NotDetected;
    EnvSensorState pressure_state_ = EnvSensorState::NotDetected;
    QmpCalibration qmp_calibration_{};
};
