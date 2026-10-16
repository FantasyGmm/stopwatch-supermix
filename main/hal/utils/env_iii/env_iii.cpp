// SPDX-License-Identifier: MIT
// Adapts M5Stack M5Unit-ENV QMP6988 compensation/OTP and SHT30 probe/CRC logic.
// Source commit: 737e5180ea7b84d8f22fe49ae4fc62cf06cbc3a2.
// See third_party_notices/m5unit-env.md.
#include "env_iii.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr uint8_t kSht30Address = 0x44;
constexpr uint8_t kQmp6988Address = 0x70;
constexpr int kI2cTimeoutMs = 50;
constexpr uint32_t kRetryIntervalMs = 5000;

constexpr uint8_t kQmpChipIdRegister = 0xD1;
constexpr uint8_t kQmpChipId = 0x5C;
constexpr uint8_t kQmpSoftResetRegister = 0xE0;
constexpr uint8_t kQmpStatusRegister = 0xF3;
constexpr uint8_t kQmpControlMeasurementRegister = 0xF4;
constexpr uint8_t kQmpPressureRegister = 0xF7;
constexpr uint8_t kQmpCalibrationRegister = 0xA0;
constexpr uint8_t kQmpSoftResetValue = 0xE6;
constexpr uint8_t kQmpForcedOsrPressure8Temperature1 = 0x31;
constexpr uint8_t kQmpMeasuringBit = 0x08;
constexpr uint32_t kQmpRawOffset = 8388608U;
constexpr size_t kQmpCalibrationLength = 25;

constexpr float kSht30MinTemperatureC = -40.0f;
constexpr float kSht30MaxTemperatureC = 125.0f;
constexpr float kQmpMinTemperatureC = -40.0f;
constexpr float kQmpMaxTemperatureC = 85.0f;
constexpr float kMinPressureHpa = 300.0f;
constexpr float kMaxPressureHpa = 1100.0f;

void delay_ms(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        return;
    }
    const uint64_t ticks =
        (static_cast<uint64_t>(configTICK_RATE_HZ) * duration_ms + 999U) / 1000U + 1U;
    vTaskDelay(static_cast<TickType_t>(ticks));
}

bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return static_cast<uint32_t>(now_ms - deadline_ms) < 0x80000000U;
}

uint8_t sht_crc8(const uint8_t* data, size_t length)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0 ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                                    : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

uint16_t read_u16_be(const uint8_t* data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

uint32_t read_u24_be(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 16U) | (static_cast<uint32_t>(data[1]) << 8U) |
           static_cast<uint32_t>(data[2]);
}

int32_t sign_extend_16(uint16_t value)
{
    return (value & 0x8000U) != 0 ? static_cast<int32_t>(value) - 0x10000
                                  : static_cast<int32_t>(value);
}

int32_t sign_extend_20(uint32_t value)
{
    value &= 0xFFFFFU;
    return (value & 0x80000U) != 0 ? static_cast<int32_t>(value) - 0x100000
                                   : static_cast<int32_t>(value);
}

// Reproduce an arithmetic right shift without relying on implementation-defined
// right shifts of negative signed values.
int64_t arithmetic_shift_right(int64_t value, uint8_t shift)
{
    const int64_t divisor = static_cast<int64_t>(1) << shift;
    const int64_t quotient = value / divisor;
    const int64_t remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

bool read_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t* data, size_t length)
{
    return device != nullptr &&
           i2c_master_transmit_receive(device, &reg, sizeof(reg), data, length, kI2cTimeoutMs) == ESP_OK;
}

bool write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return device != nullptr &&
           i2c_master_transmit(device, bytes, sizeof(bytes), kI2cTimeoutMs) == ESP_OK;
}

template <typename Calibration>
bool decode_qmp_calibration(const uint8_t* raw, Calibration& calibration)
{
    bool all_zero = true;
    bool all_ones = true;
    for (size_t i = 0; i < kQmpCalibrationLength; ++i) {
        all_zero = all_zero && raw[i] == 0;
        all_ones = all_ones && raw[i] == 0xFF;
    }
    if (all_zero || all_ones) {
        return false;
    }

    const auto s16 = [raw](size_t offset) {
        return sign_extend_16(read_u16_be(raw + offset));
    };

    const uint32_t b00_raw = (static_cast<uint32_t>(read_u16_be(raw)) << 4U) | ((raw[24] >> 4U) & 0x0FU);
    const uint32_t a0_raw = (static_cast<uint32_t>(read_u16_be(raw + 18)) << 4U) | (raw[24] & 0x0FU);

    calibration.b00 = sign_extend_20(b00_raw);
    calibration.bt1 = static_cast<int32_t>(2982LL * s16(2) + 107370906LL);
    calibration.bt2 = 329854LL * s16(4) + 108083093LL;
    calibration.bp1 = static_cast<int32_t>(19923LL * s16(6) + 1133836764LL);
    calibration.b11 = static_cast<int32_t>(2406LL * s16(8) + 118215883LL);
    calibration.bp2 = static_cast<int32_t>(3079LL * s16(10) - 181579595LL);
    calibration.b12 = static_cast<int32_t>(6846LL * s16(12) + 85590281LL);
    calibration.b21 = static_cast<int32_t>(13836LL * s16(14) + 79333336LL);
    calibration.bp3 = static_cast<int32_t>(2915LL * s16(16) + 157155561LL);
    calibration.a0 = sign_extend_20(a0_raw);
    calibration.a1 = static_cast<int32_t>(3608LL * s16(20) - 1731677965LL);
    calibration.a2 = static_cast<int32_t>(16889LL * s16(22) - 87619360LL);
    return true;
}

template <typename Calibration>
bool convert_temperature256(int32_t raw_temperature, const Calibration& calibration, int16_t& temperature256)
{
    int64_t wk1 = static_cast<int64_t>(calibration.a1) * raw_temperature;
    int64_t wk2 = arithmetic_shift_right(static_cast<int64_t>(calibration.a2) * raw_temperature, 14);
    wk2 = arithmetic_shift_right(wk2 * raw_temperature, 10);
    wk2 = arithmetic_shift_right((wk1 + wk2) / 32767, 19);
    const int64_t result = arithmetic_shift_right(static_cast<int64_t>(calibration.a0) + wk2, 4);
    if (result < std::numeric_limits<int16_t>::min() || result > std::numeric_limits<int16_t>::max()) {
        return false;
    }
    temperature256 = static_cast<int16_t>(result);
    return true;
}

template <typename Calibration>
bool convert_pressure16(int32_t raw_pressure, int16_t temperature256, const Calibration& calibration,
                       int32_t& pressure16)
{
    int64_t wk1 = static_cast<int64_t>(calibration.bt1) * temperature256;
    int64_t wk2 = arithmetic_shift_right(static_cast<int64_t>(calibration.bp1) * raw_pressure, 5);
    wk1 += wk2;
    wk2 = arithmetic_shift_right(calibration.bt2 * temperature256, 1);
    wk2 = arithmetic_shift_right(wk2 * temperature256, 8);
    int64_t wk3 = wk2;
    wk2 = arithmetic_shift_right(static_cast<int64_t>(calibration.b11) * temperature256, 4);
    wk2 = arithmetic_shift_right(wk2 * raw_pressure, 1);
    wk3 += wk2;
    wk2 = arithmetic_shift_right(static_cast<int64_t>(calibration.bp2) * raw_pressure, 13);
    wk2 = arithmetic_shift_right(wk2 * raw_pressure, 1);
    wk3 += wk2;
    wk1 += arithmetic_shift_right(wk3, 14);
    wk2 = static_cast<int64_t>(calibration.b12) * temperature256;
    wk2 = arithmetic_shift_right(wk2 * temperature256, 22);
    wk2 = arithmetic_shift_right(wk2 * raw_pressure, 1);
    wk3 = wk2;
    wk2 = arithmetic_shift_right(static_cast<int64_t>(calibration.b21) * temperature256, 6);
    wk2 = arithmetic_shift_right(wk2 * raw_pressure, 23);
    wk2 = arithmetic_shift_right(wk2 * raw_pressure, 1);
    wk3 += wk2;
    wk2 = arithmetic_shift_right(static_cast<int64_t>(calibration.bp3) * raw_pressure, 12);
    wk2 = arithmetic_shift_right(wk2 * raw_pressure, 23);
    wk2 *= raw_pressure;
    wk3 += wk2;
    wk1 += arithmetic_shift_right(wk3, 15);
    wk1 /= 32767;
    wk1 = arithmetic_shift_right(wk1, 11);
    wk1 += calibration.b00;

    if (wk1 < std::numeric_limits<int32_t>::min() || wk1 > std::numeric_limits<int32_t>::max()) {
        return false;
    }
    pressure16 = static_cast<int32_t>(wk1);
    return true;
}

}  // namespace

EnvIIIDriver::EnvIIIDriver(i2c_master_bus_handle_t bus) : bus_(bus)
{
}

EnvIIIDriver::~EnvIIIDriver()
{
    if (sht30_device_ != nullptr) {
        (void)i2c_master_bus_rm_device(sht30_device_);
    }
    if (qmp6988_device_ != nullptr) {
        (void)i2c_master_bus_rm_device(qmp6988_device_);
    }
}

bool EnvIIIDriver::add_device(uint8_t address, i2c_master_dev_handle_t& device)
{
    if (device != nullptr) {
        return true;
    }
    if (bus_ == nullptr) {
        return false;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus_, &config, &device) == ESP_OK;
}

bool EnvIIIDriver::begin()
{
    if (bus_ == nullptr) {
        begun_ = false;
        climate_state_ = EnvSensorState::Unavailable;
        pressure_state_ = EnvSensorState::Unavailable;
        return false;
    }

    begun_ = true;
    if (add_device(kSht30Address, sht30_device_)) {
        if (climate_state_ == EnvSensorState::Unavailable) {
            climate_state_ = EnvSensorState::NotDetected;
        }
    } else {
        climate_state_ = EnvSensorState::Unavailable;
    }
    if (add_device(kQmp6988Address, qmp6988_device_)) {
        if (pressure_state_ == EnvSensorState::Unavailable) {
            pressure_state_ = EnvSensorState::NotDetected;
        }
    } else {
        pressure_state_ = EnvSensorState::Unavailable;
    }
    return true;
}

bool EnvIIIDriver::can_attempt(uint32_t now_ms, bool& waiting, uint32_t retry_at_ms)
{
    if (waiting && !time_reached(now_ms, retry_at_ms)) {
        return false;
    }
    waiting = false;
    return true;
}

void EnvIIIDriver::fail_climate(EnvSnapshot& snapshot, EnvSensorState state, uint32_t now_ms)
{
    climate_initialized_ = false;
    climate_state_ = state;
    climate_waiting_retry_ = true;
    climate_retry_at_ms_ = now_ms + kRetryIntervalMs;
    snapshot.climate.state = state;
    snapshot.climate.has_sample = false;
    snapshot.climate.sampled_at_ms = 0;
    snapshot.temperature_c = 0.0f;
    snapshot.humidity_percent = 0.0f;
}

void EnvIIIDriver::fail_pressure(EnvSnapshot& snapshot, EnvSensorState state, uint32_t now_ms)
{
    pressure_initialized_ = false;
    qmp_calibration_ = {};
    pressure_state_ = state;
    pressure_waiting_retry_ = true;
    pressure_retry_at_ms_ = now_ms + kRetryIntervalMs;
    snapshot.pressure.state = state;
    snapshot.pressure.has_sample = false;
    snapshot.pressure.sampled_at_ms = 0;
    snapshot.pressure_hpa = 0.0f;
}

void EnvIIIDriver::sample(EnvSnapshot& snapshot, uint32_t now_ms)
{
    snapshot.climate.has_sample = false;
    snapshot.pressure.has_sample = false;
    snapshot.climate.state = climate_state_;
    snapshot.pressure.state = pressure_state_;

    if (reset_pending_) {
        snapshot.climate.sampled_at_ms = 0;
        snapshot.pressure.sampled_at_ms = 0;
        snapshot.temperature_c = 0.0f;
        snapshot.humidity_percent = 0.0f;
        snapshot.pressure_hpa = 0.0f;
        reset_pending_ = false;
    }
    if (climate_state_ != EnvSensorState::Ready) {
        snapshot.climate.sampled_at_ms = 0;
        snapshot.temperature_c = 0.0f;
        snapshot.humidity_percent = 0.0f;
    }
    if (pressure_state_ != EnvSensorState::Ready) {
        snapshot.pressure.sampled_at_ms = 0;
        snapshot.pressure_hpa = 0.0f;
    }

    if (!begun_ || bus_ == nullptr) {
        climate_state_ = EnvSensorState::Unavailable;
        pressure_state_ = EnvSensorState::Unavailable;
        snapshot.climate.state = climate_state_;
        snapshot.pressure.state = pressure_state_;
        snapshot.climate.has_sample = false;
        snapshot.pressure.has_sample = false;
        snapshot.climate.sampled_at_ms = 0;
        snapshot.pressure.sampled_at_ms = 0;
        snapshot.temperature_c = 0.0f;
        snapshot.humidity_percent = 0.0f;
        snapshot.pressure_hpa = 0.0f;
        return;
    }

    sample_climate(snapshot, now_ms);
    sample_pressure(snapshot, now_ms);
}

void EnvIIIDriver::sample_climate(EnvSnapshot& snapshot, uint32_t now_ms)
{
    if (!can_attempt(now_ms, climate_waiting_retry_, climate_retry_at_ms_)) {
        return;
    }
    climate_state_ = EnvSensorState::Initializing;
    snapshot.climate.state = climate_state_;

    if (!add_device(kSht30Address, sht30_device_)) {
        fail_climate(snapshot, EnvSensorState::Unavailable, now_ms);
        return;
    }
    if (!climate_initialized_) {
        if (!probe_climate()) {
            fail_climate(snapshot, climate_ever_identified_ ? EnvSensorState::ReadError
                                                           : EnvSensorState::NotDetected,
                         now_ms);
            return;
        }
        climate_ever_identified_ = true;
        climate_initialized_ = true;
    }

    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;
    if (!read_climate(temperature_c, humidity_percent) || !std::isfinite(temperature_c) ||
        !std::isfinite(humidity_percent) || temperature_c < kSht30MinTemperatureC ||
        temperature_c > kSht30MaxTemperatureC || humidity_percent < 0.0f || humidity_percent > 100.0f) {
        fail_climate(snapshot, EnvSensorState::ReadError, now_ms);
        return;
    }

    climate_waiting_retry_ = false;
    climate_state_ = EnvSensorState::Ready;
    snapshot.climate.state = climate_state_;
    snapshot.climate.has_sample = true;
    snapshot.climate.sampled_at_ms = now_ms;
    snapshot.temperature_c = temperature_c;
    snapshot.humidity_percent = humidity_percent;
}

void EnvIIIDriver::sample_pressure(EnvSnapshot& snapshot, uint32_t now_ms)
{
    if (!can_attempt(now_ms, pressure_waiting_retry_, pressure_retry_at_ms_)) {
        return;
    }
    pressure_state_ = EnvSensorState::Initializing;
    snapshot.pressure.state = pressure_state_;

    if (!add_device(kQmp6988Address, qmp6988_device_)) {
        fail_pressure(snapshot, EnvSensorState::Unavailable, now_ms);
        return;
    }
    if (!pressure_initialized_) {
        if (!initialize_pressure()) {
            fail_pressure(snapshot, pressure_ever_identified_ ? EnvSensorState::ReadError
                                                             : EnvSensorState::NotDetected,
                          now_ms);
            return;
        }
        pressure_initialized_ = true;
    }

    float temperature_c = 0.0f;
    float pressure_hpa = 0.0f;
    if (!read_pressure(temperature_c, pressure_hpa) || !std::isfinite(temperature_c) ||
        !std::isfinite(pressure_hpa) || temperature_c < kQmpMinTemperatureC ||
        temperature_c > kQmpMaxTemperatureC || pressure_hpa < kMinPressureHpa ||
        pressure_hpa > kMaxPressureHpa) {
        fail_pressure(snapshot, EnvSensorState::ReadError, now_ms);
        return;
    }

    pressure_waiting_retry_ = false;
    pressure_state_ = EnvSensorState::Ready;
    snapshot.pressure.state = pressure_state_;
    snapshot.pressure.has_sample = true;
    snapshot.pressure.sampled_at_ms = now_ms;
    snapshot.pressure_hpa = pressure_hpa;
}

bool EnvIIIDriver::probe_climate()
{
    constexpr uint8_t command[] = {0x36, 0x82};
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (i2c_master_transmit(sht30_device_, command, sizeof(command), kI2cTimeoutMs) == ESP_OK) {
            // SHT30 needs at least 1 ms between the command and its no-clock-stretch response.
            delay_ms(1);
            uint8_t response[6]{};
            if (i2c_master_receive(sht30_device_, response, sizeof(response), kI2cTimeoutMs) == ESP_OK &&
                sht_crc8(response, 2) == response[2] && sht_crc8(response + 3, 2) == response[5]) {
                return true;
            }
        }
        if (attempt + 1 < 3) {
            delay_ms(1);
        }
    }
    return false;
}

bool EnvIIIDriver::read_climate(float& temperature_c, float& humidity_percent)
{
    constexpr uint8_t command[] = {0x24, 0x00};
    if (i2c_master_transmit(sht30_device_, command, sizeof(command), kI2cTimeoutMs) != ESP_OK) {
        return false;
    }

    // High-repeatability single shot without clock stretching requires at least 15 ms.
    delay_ms(16);
    uint8_t response[6]{};
    if (i2c_master_receive(sht30_device_, response, sizeof(response), kI2cTimeoutMs) != ESP_OK ||
        sht_crc8(response, 2) != response[2] || sht_crc8(response + 3, 2) != response[5]) {
        return false;
    }

    const uint16_t raw_temperature = read_u16_be(response);
    const uint16_t raw_humidity = read_u16_be(response + 3);
    temperature_c = -45.0f + 175.0f * static_cast<float>(raw_temperature) / 65535.0f;
    humidity_percent = 100.0f * static_cast<float>(raw_humidity) / 65535.0f;
    return true;
}

bool EnvIIIDriver::initialize_pressure()
{
    uint8_t chip_id = 0;
    if (!read_register(qmp6988_device_, kQmpChipIdRegister, &chip_id, sizeof(chip_id)) ||
        chip_id != kQmpChipId) {
        return false;
    }
    pressure_ever_identified_ = true;

    // The device may NACK while reset is taking effect; verify it by polling its ID.
    (void)write_register(qmp6988_device_, kQmpSoftResetRegister, kQmpSoftResetValue);
    delay_ms(10);
    const int64_t deadline_us = esp_timer_get_time() + 200000;
    bool ready = false;
    while (esp_timer_get_time() < deadline_us) {
        chip_id = 0;
        if (read_register(qmp6988_device_, kQmpChipIdRegister, &chip_id, sizeof(chip_id)) &&
            chip_id == kQmpChipId) {
            ready = true;
            break;
        }
        delay_ms(5);
    }
    if (!ready) {
        return false;
    }

    uint8_t raw[kQmpCalibrationLength]{};
    if (!read_register(qmp6988_device_, kQmpCalibrationRegister, raw, sizeof(raw))) {
        return false;
    }
    return decode_qmp_calibration(raw, qmp_calibration_);
}

bool EnvIIIDriver::read_pressure(float& temperature_c, float& pressure_hpa)
{
    if (!write_register(qmp6988_device_, kQmpControlMeasurementRegister,
                        kQmpForcedOsrPressure8Temperature1)) {
        return false;
    }

    // Always wait beyond the expected conversion time before checking measuring.
    delay_ms(50);
    const int64_t deadline_us = esp_timer_get_time() + 200000;
    for (;;) {
        uint8_t status = 0;
        if (!read_register(qmp6988_device_, kQmpStatusRegister, &status, sizeof(status))) {
            return false;
        }
        if ((status & kQmpMeasuringBit) == 0) {
            break;
        }
        if (esp_timer_get_time() >= deadline_us) {
            return false;
        }
        delay_ms(5);
    }

    uint8_t raw[6]{};
    if (!read_register(qmp6988_device_, kQmpPressureRegister, raw, sizeof(raw))) {
        return false;
    }

    const int32_t raw_pressure = static_cast<int32_t>(read_u24_be(raw)) - static_cast<int32_t>(kQmpRawOffset);
    const int32_t raw_temperature =
        static_cast<int32_t>(read_u24_be(raw + 3)) - static_cast<int32_t>(kQmpRawOffset);
    int16_t temperature256 = 0;
    int32_t pressure16 = 0;
    if (!convert_temperature256(raw_temperature, qmp_calibration_, temperature256) ||
        !convert_pressure16(raw_pressure, temperature256, qmp_calibration_, pressure16)) {
        return false;
    }

    temperature_c = static_cast<float>(temperature256) / 256.0f;
    const float pressure_pa = static_cast<float>(pressure16) / 16.0f;
    pressure_hpa = pressure_pa / 100.0f;
    return true;
}

void EnvIIIDriver::reset()
{
    climate_initialized_ = false;
    pressure_initialized_ = false;
    climate_ever_identified_ = false;
    pressure_ever_identified_ = false;
    climate_waiting_retry_ = false;
    pressure_waiting_retry_ = false;
    climate_retry_at_ms_ = 0;
    pressure_retry_at_ms_ = 0;
    climate_state_ = bus_ == nullptr ? EnvSensorState::Unavailable : EnvSensorState::NotDetected;
    pressure_state_ = bus_ == nullptr ? EnvSensorState::Unavailable : EnvSensorState::NotDetected;
    qmp_calibration_ = {};
    reset_pending_ = true;
}
