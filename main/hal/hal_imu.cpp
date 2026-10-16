/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <bmi270_bmm150.h>

#ifndef BMI2_I2C_PRIM_ADDR
#define BMI2_I2C_PRIM_ADDR 0x68
#endif

static const std::string_view _tag        = "HAL-IMU";
static bmi270_bmm150_handle_t _imu_sensor = NULL;

void Hal::imu_init()
{
    mclog::tagInfo(_tag, "init");

    bmi270_bmm150_config_t sensor_conf = {
        .i2c_addr        = BMI2_I2C_PRIM_ADDR,
        .config_file_ptr = NULL,
        // .mode            = BOSCH_ACCEL_AND_MAGN,
        .mode = BOSCH_ACCELEROMETER_ONLY,
    };

    if (bmi270_bmm150_sensor_create(_i2c_bus, &_imu_sensor, &sensor_conf) != ESP_OK) {
        mclog::tagError(_tag, "init failed");
        _imu_sensor = NULL;
        _imu_monitoring = false;
        return;
    }

    // Sensor creation enables the accelerometer and gyroscope. Keep both asleep
    // until the IMU app owns the screen and explicitly starts its sampling.
    _imu_monitoring = true;
    setImuMonitoring(false);
}

void Hal::updateImuData()
{
    if (_imu_sensor != NULL && _imu_monitoring) {
        int available = 0;
        if (bmi270_bmm150_sensor_acceleration_available(_imu_sensor, &available) == ESP_OK && available > 0) {
            bmi270_bmm150_sensor_read_acceleration(_imu_sensor, &_imu_data.accelY, &_imu_data.accelX,
                                                   &_imu_data.accelZ);
        }
        if (bmi270_bmm150_sensor_gyroscope_available(_imu_sensor, &available) == ESP_OK && available > 0) {
            bmi270_bmm150_sensor_read_gyroscope(_imu_sensor, &_imu_data.gyroY, &_imu_data.gyroX, &_imu_data.gyroZ);
        }
    } else if (_imu_sensor == NULL) {
        mclog::tagError(_tag, "imu invalid");
    }
}

bool Hal::setImuMonitoring(bool active)
{
    if (_imu_sensor == nullptr) {
        if (active) {
            mclog::tagWarn(_tag, "cannot start IMU sampling: sensor unavailable");
        }
        return false;
    }

    // Stop software sampling even if the sensor command fails; the warning
    // still exposes that the physical device may remain powered.
    if (!active) {
        _imu_monitoring = false;
    }
    const uint8_t sensors[] = {BMI2_ACCEL, BMI2_GYRO};
    const int8_t result = active
                              ? bmi2_sensor_enable(sensors, 2, &_imu_sensor->bmi2)
                              : bmi2_sensor_disable(sensors, 2, &_imu_sensor->bmi2);
    if (result != BMI2_OK) {
        mclog::tagWarn(_tag, "failed to {} IMU sampling: {}", active ? "start" : "stop",
                       static_cast<int>(result));
        return false;
    }

    _imu_monitoring = active;
    return true;
}
