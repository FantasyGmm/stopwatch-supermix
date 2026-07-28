/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <M5PM1.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <cmath>

static const std::string_view _tag = "HAL-PMIC";
static std::unique_ptr<M5PM1> _pm1;

namespace {

constexpr uint32_t _power_led_pulse_ms = 160;

bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

Hal::PowerLedMode sanitize_power_led_mode(int32_t raw_mode)
{
    switch (raw_mode) {
    case static_cast<int32_t>(Hal::PowerLedMode::On):
        return Hal::PowerLedMode::On;
    case static_cast<int32_t>(Hal::PowerLedMode::Blink):
        return Hal::PowerLedMode::Blink;
    default:
        return Hal::PowerLedMode::Off;
    }
}

uint8_t battery_millivolts_to_percent(uint16_t millivolts)
{
    constexpr uint16_t _battery_mv_empty = 3300;
    constexpr uint16_t _battery_mv_full  = 4200;

    if (millivolts <= _battery_mv_empty) {
        return 0;
    }
    if (millivolts >= _battery_mv_full) {
        return 100;
    }

    const auto scaled =
        static_cast<uint32_t>(millivolts - _battery_mv_empty) * 100U / (_battery_mv_full - _battery_mv_empty);
    return static_cast<uint8_t>(std::min<uint32_t>(scaled, 100U));
}

constexpr uint32_t _power_sample_period_ms = 250;
constexpr uint8_t _battery_sample_iterations = 4;
constexpr uint16_t _bat_filter_weight_old = 7;
constexpr uint16_t _bat_filter_weight_new = 1;
constexpr uint16_t _bat_filter_weight_sum = _bat_filter_weight_old + _bat_filter_weight_new;

uint8_t _bat_level             = 0;
uint32_t _bat_sample_revision  = 0;
uint16_t _bat_filtered_mv      = 0;
std::mutex _bat_level_mutex;
ExternalPowerTracker _external_power_tracker;
std::mutex _external_power_mutex;
std::mutex _pmic_io_mutex;

template <typename Operation>
auto with_pmic_io_lock(Operation&& operation)
{
    std::lock_guard<std::mutex> lock(_pmic_io_mutex);
    return operation();
}

void update_bat_level(uint8_t level)
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    _bat_level = level;
    _bat_sample_revision++;
}

void update_bat_level_from_mv(uint16_t millivolts)
{
    if (_bat_filtered_mv == 0) {
        _bat_filtered_mv = millivolts;
    } else {
        const uint32_t filtered = static_cast<uint32_t>(_bat_filtered_mv) * _bat_filter_weight_old +
                                  static_cast<uint32_t>(millivolts) * _bat_filter_weight_new;
        _bat_filtered_mv = static_cast<uint16_t>((filtered + (_bat_filter_weight_sum / 2)) / _bat_filter_weight_sum);
    }

    update_bat_level(battery_millivolts_to_percent(_bat_filtered_mv));
}

void bat_reading_task(void* param)
{
    mclog::tagInfo(_tag, "start bat reading task");

    uint8_t battery_sample_iteration = 0;
    while (true) {
        if (_pm1) {
            uint16_t vin_mv       = 0;
            const auto vin_result = with_pmic_io_lock([&] {
                return _pm1->readVin(&vin_mv);
            });
            {
                std::lock_guard<std::mutex> lock(_external_power_mutex);
                _external_power_tracker.observe(vin_result == M5PM1_OK, vin_mv > 4000);
            }

            ++battery_sample_iteration;
            if (battery_sample_iteration == _battery_sample_iterations) {
                battery_sample_iteration = 0;
                uint16_t battery_mv       = 0;
                const auto battery_result = with_pmic_io_lock([&] {
                    return _pm1->readVbat(&battery_mv);
                });
                if (battery_result == M5PM1_OK) {
                    update_bat_level_from_mv(battery_mv);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(_power_sample_period_ms));
    }
}

}  // namespace

// PMIC IO
#define PMG0_RTC_IMU_INT M5PM1_GPIO_NUM_0
#define PMG2_CHG_STAT    M5PM1_GPIO_NUM_2
#define PMG4_PORT_INT    M5PM1_GPIO_NUM_4
#define PMG3_CHG_PROG    M5PM1_GPIO_NUM_3
#define PMG1_G12_PY_IRQ  M5PM1_GPIO_NUM_1

void Hal::pmic_init()
{
    mclog::tagInfo(_tag, "pmic init");

    _pm1 = std::make_unique<M5PM1>();
    if (_pm1->begin(i2c_bus_get_internal_bus_handle(_i2c_bus)) != M5PM1_OK) {
        mclog::tagInfo(_tag, "init failed");
        _pm1.reset();
        return;
    }

    _pm1->setI2cSleepTime(0);
    _pm1->setI2cSleepTime(0);

    getPowerLedBlinkIntervalSeconds(true);
    setPowerLedMode(getPowerLedMode(true), false);

    // set button delay click 1s
    _pm1->btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    // disable WDT, default is open
    _pm1->wdtSet(0);
    //  hold LDO power close when power off, keep power for RTC
    _pm1->ldoSetPowerHold(true);

    // set charge enable or disable, this setting will keep working after power off
    _pm1->setChargeEnable(true);

    // drive CHG_PROG low to force active charge programming
    _pm1->gpioSet(PMG3_CHG_PROG, M5PM1_GPIO_MODE_OUTPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);

    _pm1->gpioSetFunc(PMG2_CHG_STAT, M5PM1_GPIO_FUNC_GPIO);
    _pm1->gpioSetMode(PMG2_CHG_STAT, M5PM1_GPIO_MODE_INPUT);
    _pm1->gpioSetPull(PMG2_CHG_STAT, M5PM1_GPIO_PULL_NONE);

    _pm1->setSingleResetDisable(true);

    uint16_t battery_mv = 0;
    if (_pm1->readVbat(&battery_mv) == M5PM1_OK) {
        update_bat_level_from_mv(battery_mv);
    }

    xTaskCreate(bat_reading_task, "bat_reading", 4 * 1024, NULL, 1, NULL);
}

bool Hal::pmic_get_pwr_btn_state()
{
    bool result = false;
    if (_pm1) {
        return with_pmic_io_lock([&] {
            return _pm1->btnGetState(&result);
        });
    }
    return result;
}

uint8_t Hal::getBatteryLevel()
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    return _bat_level;
}

BatterySampleSnapshot Hal::batterySampleSnapshot()
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    return {_bat_level, _bat_sample_revision};
}

uint32_t Hal::batterySampleRevision()
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    return _bat_sample_revision;
}

ExternalPowerSnapshot Hal::getExternalPowerSnapshot()
{
    std::lock_guard<std::mutex> lock(_external_power_mutex);
    return _external_power_tracker.snapshot();
}

bool Hal::isBatteryCharging(bool strict)
{
    const bool external_power = getExternalPowerSnapshot().state == ExternalPowerState::ExternalPower;
    if (!strict || !external_power) {
        return external_power;
    }

    if (!_pm1) {
        return false;
    }

    uint8_t charge_status = 1;
    const auto chg_result = with_pmic_io_lock([&] {
        return _pm1->gpioGetInput(PMG2_CHG_STAT, &charge_status);
    });
    if (chg_result != M5PM1_OK) {
        return false;
    }

    return charge_status == 0;
}

void Hal::applyPowerLedOutput(bool enabled)
{
    if (!_pm1 || (_power_led_output_valid && _power_led_output == enabled)) {
        return;
    }

    const auto led_result = with_pmic_io_lock([&] {
        return _pm1->setLedEnLevel(enabled);
    });
    if (led_result != M5PM1_OK) {
        mclog::tagWarn(_tag, "failed to set power LED to {}", enabled ? "on" : "off");
        return;
    }

    _power_led_output       = enabled;
    _power_led_output_valid = true;
}

void Hal::setPowerLedMode(PowerLedMode mode, bool saveToSettings)
{
    _power_led_mode = sanitize_power_led_mode(static_cast<int32_t>(mode));

    const uint32_t now = millis();
    switch (_power_led_mode) {
    case PowerLedMode::Off:
        _power_led_next_blink_ms = 0;
        _power_led_pulse_end_ms  = 0;
        applyPowerLedOutput(false);
        break;
    case PowerLedMode::On:
        _power_led_next_blink_ms = 0;
        _power_led_pulse_end_ms  = 0;
        applyPowerLedOutput(true);
        break;
    case PowerLedMode::Blink:
        applyPowerLedOutput(true);
        _power_led_pulse_end_ms  = now + _power_led_pulse_ms;
        _power_led_next_blink_ms = now + power_led_blink_interval_ms(_power_led_blink_interval_seconds);
        break;
    }

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("pwr_led_mode", static_cast<int32_t>(_power_led_mode));
    }
}

Hal::PowerLedMode Hal::getPowerLedMode(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _power_led_mode = sanitize_power_led_mode(settings.GetInt("pwr_led_mode", 0));
    }
    return _power_led_mode;
}

void Hal::setPowerLedBlinkIntervalSeconds(int seconds, bool saveToSettings)
{
    const int normalized = normalize_power_led_blink_interval_seconds(seconds);
    const bool changed   = normalized != _power_led_blink_interval_seconds;
    _power_led_blink_interval_seconds = normalized;

    if (changed && _power_led_mode == PowerLedMode::Blink) {
        setPowerLedMode(PowerLedMode::Blink, false);
    }

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("pwr_led_int_s", _power_led_blink_interval_seconds);
    }
}

int Hal::getPowerLedBlinkIntervalSeconds(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _power_led_blink_interval_seconds = normalize_power_led_blink_interval_seconds(
            settings.GetInt("pwr_led_int_s", PowerLedBlinkIntervalDefaultSeconds));
    }
    return _power_led_blink_interval_seconds;
}

void Hal::updatePowerLed()
{
    if (_power_led_mode != PowerLedMode::Blink || !_pm1) {
        return;
    }

    const uint32_t now = millis();
    if (_power_led_output && deadline_reached(now, _power_led_pulse_end_ms)) {
        applyPowerLedOutput(false);
    }

    if (!_power_led_output && deadline_reached(now, _power_led_next_blink_ms)) {
        applyPowerLedOutput(true);
        _power_led_pulse_end_ms  = now + _power_led_pulse_ms;
        _power_led_next_blink_ms = now + power_led_blink_interval_ms(_power_led_blink_interval_seconds);
    }
}
