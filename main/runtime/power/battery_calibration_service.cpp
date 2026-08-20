#include "battery_calibration_service.h"

#include <hal/hal.h>

#include <array>
#include <cstdio>
#include <cstring>

#include <esp_log.h>

namespace {
constexpr const char* tag = "BatteryCalibration";
constexpr const char* calibrationPath = "/spiflash/battery-calibration.csv";
constexpr const char* csvHeader = "elapsed_s,raw_mv,filtered_mv,display_pct,external_power\n";
BatteryCalibrationService service;
}  // namespace

BatteryCalibrationService& GetBatteryCalibrationService()
{
    return service;
}

bool BatteryCalibrationService::start()
{
    FILE* file = std::fopen(calibrationPath, "wb");
    if (file == nullptr) {
        _schedule.stop();
        return false;
    }
    const bool wrote_header = std::fputs(csvHeader, file) >= 0;
    const bool closed = std::fclose(file) == 0;
    if (!wrote_header || !closed) {
        _schedule.stop();
        return false;
    }
    _schedule.start(GetHAL().millis());
    return true;
}

void BatteryCalibrationService::update()
{
    if (!_schedule.active()) {
        return;
    }

    const BatterySampleSnapshot snapshot = GetHAL().batterySampleSnapshot();
    if (snapshot.revision == 0 || snapshot.raw_mv == 0 || snapshot.filtered_mv == 0) {
        return;
    }

    const BatteryCalibrationInput input{
        .now_ms = GetHAL().millis(),
        .revision = snapshot.revision,
        .raw_mv = snapshot.raw_mv,
        .filtered_mv = snapshot.filtered_mv,
        .percent = snapshot.level,
        .external_power = GetHAL().isBatteryCharging(false),
    };
    const BatteryCalibrationDecision decision = _schedule.update(input);
    if (!decision.append) {
        return;
    }

    std::array<char, 64> line{};
    if (!FormatBatteryCalibrationCsvLine(input, decision.elapsed_seconds, line.data(), line.size())) {
        _schedule.stop();
        ESP_LOGE(tag, "calibration line formatting failed");
        return;
    }

    FILE* file = std::fopen(calibrationPath, "ab");
    if (file == nullptr) {
        _schedule.stop();
        ESP_LOGE(tag, "calibration file open failed");
        return;
    }
    const bool wrote_line = std::fputs(line.data(), file) >= 0;
    const bool closed = std::fclose(file) == 0;
    if (!wrote_line || !closed) {
        _schedule.stop();
        ESP_LOGE(tag, "calibration file write failed");
    }
}

bool BatteryCalibrationService::exportToLog() const
{
    FILE* file = std::fopen(calibrationPath, "rb");
    if (file == nullptr) {
        return false;
    }

    std::array<char, 128> line{};
    while (std::fgets(line.data(), line.size(), file) != nullptr) {
        line[std::strcspn(line.data(), "\r\n")] = '\0';
        ESP_LOGI(tag, "DEBUG BATTERY CSV %s", line.data());
    }
    const bool read_ok = std::ferror(file) == 0;
    const bool closed = std::fclose(file) == 0;
    return read_ok && closed;
}

bool BatteryCalibrationService::active() const
{
    return _schedule.active();
}

uint32_t BatteryCalibrationService::sampleCount() const
{
    return _schedule.sampleCount();
}
