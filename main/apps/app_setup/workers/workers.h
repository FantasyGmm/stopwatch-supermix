/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include "wifi_settings_model.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

class WifiSettingsController;

namespace setup_workers {

class PercentageAdjustView;

/**
 * @brief
 *
 */
class WorkerBase {
public:
    virtual ~WorkerBase() = default;

    virtual void update()
    {
    }

    virtual void updateRuntime()
    {
    }

    virtual void releaseRuntime()
    {
    }

    bool isDone() const
    {
        return _is_done;
    }

protected:
    bool _is_done = false;
};

/**
 * @brief
 *
 */
class BrightnessWorker : public WorkerBase {
public:
    BrightnessWorker();
    ~BrightnessWorker();
    void update() override;

private:
    std::unique_ptr<PercentageAdjustView> _view;
    int _applied_brightness = 0;
    uint32_t _global_revision = 0;
    bool _save_requested    = false;
};

/**
 * @brief
 *
 */
class VolumeWorker : public WorkerBase {
public:
    VolumeWorker();
    ~VolumeWorker();
    void update() override;

private:
    std::unique_ptr<PercentageAdjustView> _view;
    int _applied_volume  = 0;
    uint32_t _global_revision = 0;
    bool _save_requested = false;
};

/**
 * @brief
 *
 */
class ButtonWorker : public WorkerBase {
public:
    ButtonWorker();
    ~ButtonWorker();
    void update() override;

private:
    class ButtonConfigView;

    std::unique_ptr<ButtonConfigView> _view;
    Hal::ButtonConfig _applied_config;
};

class PowerLedWorker : public WorkerBase {
public:
    PowerLedWorker();
    ~PowerLedWorker();
    void update() override;

private:
    class PowerLedConfigView;

    std::unique_ptr<PowerLedConfigView> _view;
    Hal::PowerLedMode _original_mode = Hal::PowerLedMode::Off;
    Hal::PowerLedMode _applied_mode  = Hal::PowerLedMode::Off;
    int _original_interval_seconds   = PowerLedBlinkIntervalDefaultSeconds;
    int _applied_interval_seconds    = PowerLedBlinkIntervalDefaultSeconds;
    bool _committed                  = false;
};

/**
 * @brief
 *
 */
class SetTimeWorker : public WorkerBase {
public:
    SetTimeWorker();
    ~SetTimeWorker();
    void update() override;

private:
    class TimeAdjustView;

    std::unique_ptr<TimeAdjustView> _view;
    TimeHms _applied_time;
};

/**
 * @brief
 *
 */
class SetDateWorker : public WorkerBase {
public:
    SetDateWorker();
    ~SetDateWorker();
    void update() override;

private:
    class DateAdjustView;

    std::unique_ptr<DateAdjustView> _view;
    DateYmd _applied_date;
};

class WifiSettingsWorker : public WorkerBase {
public:
    WifiSettingsWorker();
    ~WifiSettingsWorker();
    void updateRuntime() override;
    void update() override;
    void releaseRuntime() override;

private:
    class WifiSettingsView;

    std::unique_ptr<WifiSettingsView> _view;
    std::unique_ptr<WifiSettingsController> _controller;
    std::optional<std::size_t> _pending_selected_network;
    std::optional<WifiSettingsSnapshot> _pending_snapshot;
    uint32_t _next_poll_tick = 0;
    bool _pending_pairing_action = false;
    bool _has_snapshot       = false;
    WifiSettingsSnapshot _last_snapshot;
};

/**
 * @brief
 *
 */
class AboutWorker : public WorkerBase {
public:
    AboutWorker();
    ~AboutWorker();
    void update() override;

private:
    class AboutView;

    std::unique_ptr<AboutView> _view;
    int _progress                = 0;
    uint32_t _next_progress_tick = 0;
    int _pending_burst_steps     = 0;
};

}  // namespace setup_workers
