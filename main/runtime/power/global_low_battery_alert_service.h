#pragma once

#include "apps/common/low_battery/low_battery_alert_overlay.h"
#include "runtime/power/low_battery_alert_logic.h"

#include <cstdint>

class GlobalLowBatteryAlertService {
public:
    bool init();
    void update();
    void updateOverlay();
    void applyPendingAudio();
    void shutdown();

private:
    static constexpr uint32_t soundOwnershipTimeoutMs = 600;
    static constexpr uint32_t chargingPollIntervalMs = 1000;
    LowBatteryAlertLogic _logic;
    LowBatteryAlertOverlay _overlay;
    bool _initialized = false;
    bool _overlay_available = false;
    uint32_t _last_battery_revision = 0;
    bool _charging_observed = false;
    bool _external_power = false;
    bool _last_display_visible = false;
    uint32_t _last_charging_poll_ms = 0;
    bool _show_pending = false;
    bool _hide_pending = false;
    uint8_t _show_percent = 0;
    bool _audio_start_pending = false;
    bool _system_playback_owned = false;
    uint32_t _sound_started_ms = 0;
};

GlobalLowBatteryAlertService& GetGlobalLowBatteryAlertService();
