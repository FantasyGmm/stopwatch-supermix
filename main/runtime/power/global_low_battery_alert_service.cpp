#include "runtime/power/global_low_battery_alert_service.h"

#include "apps/common/audio/audio.h"
#include "hal/hal.h"
#include "runtime/resource_supervisor.h"

namespace {
GlobalLowBatteryAlertService global_low_battery_alert_service;
}

GlobalLowBatteryAlertService& GetGlobalLowBatteryAlertService()
{
    return global_low_battery_alert_service;
}

bool GlobalLowBatteryAlertService::init()
{
    shutdown();

    _overlay_available = _overlay.init(lv_layer_top());
    _initialized = true;
    return _overlay_available;
}

void GlobalLowBatteryAlertService::update()
{
    if (!_initialized) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    const auto resources = GetResourceSupervisor().snapshot();
    const BatterySampleSnapshot battery_sample = GetHAL().batterySampleSnapshot();
    const bool sample_due =
        battery_sample.revision != 0 &&
        battery_sample.revision != _last_battery_revision;
    if (sample_due) {
        _last_battery_revision = battery_sample.revision;
    }
    const bool display_visible = GetHAL().isDisplayVisible();
    const bool display_woke = display_visible && !_last_display_visible;
    _last_display_visible = display_visible;
    const bool charging_refresh_due =
        !_charging_observed || sample_due || display_woke ||
        static_cast<uint32_t>(now_ms - _last_charging_poll_ms) >=
            chargingPollIntervalMs;
    if (charging_refresh_due) {
        _external_power = GetHAL().isBatteryCharging(false);
        _last_charging_poll_ms = now_ms;
        _charging_observed = true;
    }
    const bool playback_available =
        !_system_playback_owned && !resources.blocked &&
        resources.playback_owner == RuntimeOwner::None &&
        !GetHAL().isAudioPlaying();

    const LowBatteryAlertInput input{
        .now_ms = now_ms,
        .battery_sample_due = sample_due,
        .battery_sample_valid = sample_due,
        .battery_percent = sample_due ? battery_sample.level : 0,
        .external_power = _external_power,
        .display_visible = display_visible,
        .playback_available = playback_available,
        .speaker_audible = GetHAL().getSpeakerVolume() > 0,
    };
    const LowBatteryAlertActions actions = _logic.update(input);

    if (actions.hide_text) {
        _hide_pending = true;
    }
    if (actions.show_text) {
        _show_pending = true;
        _show_percent = actions.battery_percent;
    }
    if (actions.play_sound) {
        _audio_start_pending = true;
    }
}

void GlobalLowBatteryAlertService::updateOverlay()
{
    if (!_initialized || !_overlay_available) {
        return;
    }

    if (_hide_pending) {
        _overlay.hide();
        _hide_pending = false;
    }
    if (_show_pending) {
        _overlay.show(_show_percent);
        _show_pending = false;
    }
}

void GlobalLowBatteryAlertService::applyPendingAudio()
{
    if (!_initialized) {
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    if (_system_playback_owned) {
        if (!GetHAL().isAudioPlaying() ||
            static_cast<uint32_t>(now_ms - _sound_started_ms) >=
                soundOwnershipTimeoutMs) {
            GetResourceSupervisor().releasePlayback(RuntimeOwner::System);
            _system_playback_owned = false;
        }
        return;
    }

    if (!_audio_start_pending) {
        return;
    }
    _audio_start_pending = false;

    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }
    const auto resources = GetResourceSupervisor().snapshot();
    if (resources.blocked || resources.playback_owner != RuntimeOwner::None ||
        GetHAL().isAudioPlaying()) {
        _logic.deferSound();
        return;
    }
    if (GetResourceSupervisor().acquirePlayback(RuntimeOwner::System) !=
        AcquireResult::Granted) {
        _logic.deferSound();
        return;
    }

    _system_playback_owned = true;
    _sound_started_ms = GetHAL().millis();
    audio::play_melody({76, 72}, 0.09f, 0.8f);
}

void GlobalLowBatteryAlertService::shutdown()
{
    if (_system_playback_owned) {
        GetResourceSupervisor().releasePlayback(RuntimeOwner::System);
    }

    _overlay.shutdown();
    _logic = LowBatteryAlertLogic{};
    _initialized = false;
    _overlay_available = false;
    _last_battery_revision = 0;
    _charging_observed = false;
    _external_power = false;
    _last_display_visible = false;
    _last_charging_poll_ms = 0;
    _show_pending = false;
    _hide_pending = false;
    _show_percent = 0;
    _audio_start_pending = false;
    _system_playback_owned = false;
    _sound_started_ms = 0;
}
