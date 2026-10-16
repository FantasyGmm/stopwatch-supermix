/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/env_iii/env_iii.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <mooncake_log.h>
#include <new>

namespace {

static const std::string_view _tag = "HAL-ENV";

constexpr gpio_num_t _port_a_sda = GPIO_NUM_10;
constexpr gpio_num_t _port_a_scl = GPIO_NUM_11;
constexpr uint32_t _port_a_power_startup_ms = 20;
constexpr uint32_t _env_sample_period_ms = 1000;
constexpr uint32_t _env_retry_period_ms = 5000;
constexpr uint32_t _env_task_stack_bytes = 4 * 1024;
constexpr UBaseType_t _env_task_priority = tskIDLE_PRIORITY + 1;

void initialize_snapshot(EnvSnapshot& snapshot, EnvSensorState state, bool monitoring)
{
    snapshot = {};
    snapshot.monitoring = monitoring;
    snapshot.climate.state = state;
    snapshot.pressure.state = state;
}

esp_err_t create_port_a_bus(i2c_master_bus_handle_t* bus)
{
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = _port_a_sda,
        .scl_io_num = _port_a_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = false},
    };
    return i2c_new_master_bus(&config, bus);
}

}  // namespace

void Hal::EnvIIIDriverDeleter::operator()(EnvIIIDriver* driver) const
{
    delete driver;
}

bool Hal::setEnvMonitoring(bool active)
{
    std::lock_guard<std::mutex> control_lock(_env_control_mutex);

    const bool already_requested = _env_monitoring.load(std::memory_order_acquire);
    if (already_requested == active && (!active || _env_task_handle != nullptr)) {
        return true;
    }

    if (active && _env_task_handle == nullptr) {
        TaskHandle_t task_handle = nullptr;
        if (xTaskCreate(&Hal::envMonitoringTaskEntry, "env_monitor", _env_task_stack_bytes, this,
                        _env_task_priority, &task_handle) != pdPASS) {
            if (!already_requested) {
                _env_monitoring_epoch.fetch_add(1, std::memory_order_acq_rel);
                _env_monitoring.store(true, std::memory_order_release);
            }
            {
                std::lock_guard<std::mutex> snapshot_lock(_env_snapshot_mutex);
                initialize_snapshot(_env_snapshot, EnvSensorState::Unavailable, true);
            }
            mclog::tagWarn(_tag, "failed to create ENV monitor task");
            return false;
        }
        _env_task_handle = task_handle;
    }

    if (active) {
        {
            std::lock_guard<std::mutex> snapshot_lock(_env_snapshot_mutex);
            initialize_snapshot(_env_snapshot, EnvSensorState::Initializing, true);
        }
        if (!already_requested) {
            _env_monitoring_epoch.fetch_add(1, std::memory_order_acq_rel);
        }
        _env_monitoring.store(true, std::memory_order_release);
    } else {
        _env_monitoring.store(false, std::memory_order_release);
        _env_monitoring_epoch.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> snapshot_lock(_env_snapshot_mutex);
            initialize_snapshot(_env_snapshot, EnvSensorState::NotDetected, false);
        }
    }

    if (_env_task_handle != nullptr) {
        xTaskNotifyGive(_env_task_handle);
    }
    return true;
}

EnvSnapshot Hal::getEnvSnapshot()
{
    EnvSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(_env_snapshot_mutex);
        snapshot = _env_snapshot;
    }

    snapshot.monitoring = _env_monitoring.load(std::memory_order_acquire);
    if (!snapshot.monitoring) {
        initialize_snapshot(snapshot, EnvSensorState::NotDetected, false);
        return snapshot;
    }

    return snapshot;
}

void Hal::envMonitoringTaskEntry(void* param)
{
    static_cast<Hal*>(param)->envMonitoringTask();
}

void Hal::envMonitoringTask()
{
    bool session_seen = false;
    uint32_t session_epoch = 0;
    bool power_lease_owned = false;
    bool power_ready = false;
    bool driver_ready = false;

    auto publish_unavailable = [this](uint32_t epoch) {
        if (!_env_monitoring.load(std::memory_order_acquire) ||
            _env_monitoring_epoch.load(std::memory_order_acquire) != epoch) {
            return;
        }

        std::lock_guard<std::mutex> lock(_env_snapshot_mutex);
        if (!_env_monitoring.load(std::memory_order_acquire) ||
            _env_monitoring_epoch.load(std::memory_order_acquire) != epoch) {
            return;
        }
        initialize_snapshot(_env_snapshot, EnvSensorState::Unavailable, true);
    };

    while (true) {
        const uint32_t requested_epoch = _env_monitoring_epoch.load(std::memory_order_acquire);
        const bool monitoring = _env_monitoring.load(std::memory_order_acquire);

        if (!session_seen || requested_epoch != session_epoch) {
            if (session_seen) {
                if (_env_driver != nullptr) {
                    _env_driver->reset();
                }
                if (power_lease_owned) {
                    if (restoreEnvPortPower()) {
                        power_lease_owned = false;
                    } else {
                        mclog::tagWarn(_tag, "failed to restore Port A 5V power state");
                    }
                }
                power_ready = false;
            }
            session_seen = true;
            session_epoch = requested_epoch;
        }

        if (!monitoring) {
            if (power_lease_owned) {
                if (restoreEnvPortPower()) {
                    power_lease_owned = false;
                } else {
                    mclog::tagWarn(_tag, "retry failed to restore Port A 5V power state");
                }
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_env_retry_period_ms));
            } else {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            }
            continue;
        }

        if (!_env_monitoring.load(std::memory_order_acquire) ||
            _env_monitoring_epoch.load(std::memory_order_acquire) != session_epoch) {
            continue;
        }

        if (!power_ready) {
            if (!acquireEnvPortPower()) {
                mclog::tagWarn(_tag, "failed to enable Port A 5V power");
                publish_unavailable(session_epoch);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_env_retry_period_ms));
                continue;
            }
            power_lease_owned = true;
            vTaskDelay(pdMS_TO_TICKS(_port_a_power_startup_ms) + 1);
            power_ready = true;
        }

        if (!_env_monitoring.load(std::memory_order_acquire) ||
            _env_monitoring_epoch.load(std::memory_order_acquire) != session_epoch) {
            continue;
        }

        if (!_env_i2c_bus) {
            const esp_err_t result = create_port_a_bus(&_env_i2c_bus);
            if (result != ESP_OK) {
                mclog::tagWarn(_tag, "failed to create Port A I2C bus: {}", esp_err_to_name(result));
                publish_unavailable(session_epoch);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_env_retry_period_ms));
                continue;
            }
        }

        if (_env_driver == nullptr) {
            _env_driver.reset(new (std::nothrow) EnvIIIDriver(_env_i2c_bus));
            if (_env_driver == nullptr) {
                mclog::tagWarn(_tag, "failed to allocate ENV-III driver");
                publish_unavailable(session_epoch);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_env_retry_period_ms));
                continue;
            }
        }

        if (!driver_ready) {
            if (!_env_driver->begin()) {
                mclog::tagWarn(_tag, "failed to initialize ENV-III I2C devices");
                publish_unavailable(session_epoch);
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_env_retry_period_ms));
                continue;
            }
            driver_ready = true;
        }

        if (!_env_monitoring.load(std::memory_order_acquire) ||
            _env_monitoring_epoch.load(std::memory_order_acquire) != session_epoch) {
            continue;
        }

        EnvSnapshot sample;
        initialize_snapshot(sample, EnvSensorState::Initializing, true);
        _env_driver->sample(sample, millis());

        if (_env_monitoring.load(std::memory_order_acquire) &&
            _env_monitoring_epoch.load(std::memory_order_acquire) == session_epoch) {
            sample.monitoring = true;
            std::lock_guard<std::mutex> lock(_env_snapshot_mutex);
            if (_env_monitoring.load(std::memory_order_acquire) &&
                _env_monitoring_epoch.load(std::memory_order_acquire) == session_epoch) {
                _env_snapshot = sample;
            }
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_env_sample_period_ms));
    }
}
