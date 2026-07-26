#include "display_protection_diagnostics.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

const char* DisplayProtectionStateName(DisplayProtectionState state)
{
    switch (state) {
        case DisplayProtectionState::Active:
            return "active";
        case DisplayProtectionState::Dimmed:
            return "dimmed";
        case DisplayProtectionState::ScreenOff:
            return "off";
    }
    return "unknown";
}

const char* ExternalPowerStateName(ExternalPowerState state)
{
    switch (state) {
        case ExternalPowerState::Unknown:
            return "unknown";
        case ExternalPowerState::Battery:
            return "battery";
        case ExternalPowerState::ExternalPower:
            return "external";
    }
    return "unknown";
}

const char* DisplayWakeReasonName(DisplayWakeReason reason)
{
    switch (reason) {
        case DisplayWakeReason::Initialization:
            return "initialization";
        case DisplayWakeReason::Button:
            return "button";
        case DisplayWakeReason::Touch:
            return "touch";
        case DisplayWakeReason::PowerInserted:
            return "power_inserted";
        case DisplayWakeReason::PowerRemoved:
            return "power_removed";
        case DisplayWakeReason::ConfigurationChanged:
            return "configuration_changed";
        case DisplayWakeReason::KeepAwakeActivated:
            return "keep_awake_activated";
    }
    return "unknown";
}

size_t FormatDisplayProtectionDiagnostics(
    const DisplayProtectionDiagnostics& diagnostics, char* output,
    size_t output_size)
{
    if (output == nullptr || output_size == 0) {
        return 0;
    }

    const int written = std::snprintf(
        output, output_size,
        "DISPLAY PROTECTION state=%s effective=%d user=%d automatic=%u "
        "power=%s power_valid=%u dim_choice=%u off_choice=%u "
        "dim_ms=%" PRIu32 " off_ms=%" PRIu32 " idle_ms=%" PRIu32 " "
        "dim_remaining_ms=%" PRIu32 " off_remaining_ms=%" PRIu32 " "
        "charging_guard=%u reasons=0x%02x last_wake=%s",
        DisplayProtectionStateName(diagnostics.state),
        diagnostics.effective_brightness, diagnostics.user_brightness,
        static_cast<unsigned>(diagnostics.automatic_enabled),
        ExternalPowerStateName(diagnostics.external_power.state),
        static_cast<unsigned>(
            diagnostics.external_power.latest_sample_valid),
        static_cast<unsigned>(diagnostics.config.dim_choice),
        static_cast<unsigned>(diagnostics.config.screen_off_choice),
        diagnostics.timing.dim_after_ms,
        diagnostics.timing.screen_off_after_ms,
        diagnostics.timing.inactivity_ms,
        diagnostics.timing.dim_remaining_ms,
        diagnostics.timing.screen_off_remaining_ms,
        static_cast<unsigned>(diagnostics.config.protect_while_charging),
        static_cast<unsigned>(diagnostics.reason_mask),
        DisplayWakeReasonName(diagnostics.last_wake_reason));
    if (written < 0) {
        output[0] = '\0';
        return 0;
    }
    return std::min(static_cast<size_t>(written), output_size - 1U);
}

void ExternalPowerHealthLogPolicy::reset(bool sample_valid, uint32_t now_ms)
{
    _last_sample_valid = sample_valid;
    _last_log_ms = now_ms;
    _initialized = true;
}

ExternalPowerHealthLogEvent ExternalPowerHealthLogPolicy::update(
    bool sample_valid, uint32_t now_ms)
{
    if (!_initialized) {
        reset(sample_valid, now_ms);
        return ExternalPowerHealthLogEvent::None;
    }

    if (sample_valid != _last_sample_valid) {
        _last_sample_valid = sample_valid;
        _last_log_ms = now_ms;
        return sample_valid ? ExternalPowerHealthLogEvent::Healthy
                            : ExternalPowerHealthLogEvent::Unhealthy;
    }
    if (!sample_valid && now_ms - _last_log_ms >= RetryMs) {
        _last_log_ms = now_ms;
        return ExternalPowerHealthLogEvent::Unhealthy;
    }
    return ExternalPowerHealthLogEvent::None;
}
