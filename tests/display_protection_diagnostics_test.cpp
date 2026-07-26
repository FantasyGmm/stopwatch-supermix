#include "runtime/display/display_protection_diagnostics.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

DisplayProtectionDiagnostics make_diagnostics(
    const DisplayProtectionTimingSnapshot& timing)
{
    return {
        .state = DisplayProtectionState::Dimmed,
        .effective_brightness = 20,
        .user_brightness = 73,
        .automatic_enabled = true,
        .external_power = {ExternalPowerState::ExternalPower, 9, true},
        .config = {2, 4, true},
        .timing = timing,
        .reason_mask = static_cast<uint8_t>(
            static_cast<uint8_t>(DisplayKeepAwakeReason::AssistantInteraction) |
            static_cast<uint8_t>(DisplayKeepAwakeReason::AlarmRinging)),
        .last_wake_reason = DisplayWakeReason::PowerInserted,
    };
}

} // namespace

int main()
{
    DisplayProtectionLogic logic;
    logic.reset(UINT32_MAX - 5000U);
    logic.setTimeouts(10000U, 30000U, UINT32_MAX - 5000U);

    const auto wrapped_timing = logic.timingSnapshot(1999U);
    assert(wrapped_timing.inactivity_ms == 7000U);
    assert(wrapped_timing.dim_remaining_ms == 3000U);
    assert(wrapped_timing.screen_off_remaining_ms == 23000U);

    char line[512]{};
    const auto diagnostics = make_diagnostics(wrapped_timing);
    const size_t count =
        FormatDisplayProtectionDiagnostics(diagnostics, line, sizeof(line));
    assert(count == std::strlen(line));
    assert(std::strcmp(
               line,
               "DISPLAY PROTECTION state=dimmed effective=20 user=73 "
               "automatic=1 power=external power_valid=1 dim_choice=2 "
               "off_choice=4 dim_ms=10000 off_ms=30000 idle_ms=7000 "
               "dim_remaining_ms=3000 off_remaining_ms=23000 "
               "charging_guard=1 reasons=0x05 last_wake=power_inserted") ==
           0);
    assert(std::strchr(line, '\n') == nullptr);

    const auto expired_timing = logic.timingSnapshot(24999U);
    assert(expired_timing.inactivity_ms == 30000U);
    assert(expired_timing.dim_remaining_ms == 0U);
    assert(expired_timing.screen_off_remaining_ms == 0U);
    const auto expired = make_diagnostics(expired_timing);
    FormatDisplayProtectionDiagnostics(expired, line, sizeof(line));
    assert(std::strstr(line, "idle_ms=30000") != nullptr);
    assert(std::strstr(line, "dim_remaining_ms=0") != nullptr);
    assert(std::strstr(line, "off_remaining_ms=0") != nullptr);

    assert(std::strcmp(DisplayProtectionStateName(DisplayProtectionState::Active),
                       "active") == 0);
    assert(std::strcmp(DisplayProtectionStateName(DisplayProtectionState::Dimmed),
                       "dimmed") == 0);
    assert(std::strcmp(
               DisplayProtectionStateName(DisplayProtectionState::ScreenOff),
               "off") == 0);
    assert(std::strcmp(DisplayProtectionStateName(
                           static_cast<DisplayProtectionState>(UINT8_MAX)),
                       "unknown") == 0);

    assert(std::strcmp(ExternalPowerStateName(ExternalPowerState::Unknown),
                       "unknown") == 0);
    assert(std::strcmp(ExternalPowerStateName(ExternalPowerState::Battery),
                       "battery") == 0);
    assert(std::strcmp(
               ExternalPowerStateName(ExternalPowerState::ExternalPower),
               "external") == 0);
    assert(std::strcmp(ExternalPowerStateName(
                           static_cast<ExternalPowerState>(UINT8_MAX)),
                       "unknown") == 0);

    assert(std::strcmp(DisplayWakeReasonName(DisplayWakeReason::Initialization),
                       "initialization") == 0);
    assert(std::strcmp(DisplayWakeReasonName(DisplayWakeReason::Button),
                       "button") == 0);
    assert(std::strcmp(DisplayWakeReasonName(DisplayWakeReason::Touch),
                       "touch") == 0);
    assert(std::strcmp(DisplayWakeReasonName(DisplayWakeReason::PowerInserted),
                       "power_inserted") == 0);
    assert(std::strcmp(DisplayWakeReasonName(DisplayWakeReason::PowerRemoved),
                       "power_removed") == 0);
    assert(std::strcmp(
               DisplayWakeReasonName(DisplayWakeReason::ConfigurationChanged),
               "configuration_changed") == 0);
    assert(std::strcmp(
               DisplayWakeReasonName(DisplayWakeReason::KeepAwakeActivated),
               "keep_awake_activated") == 0);
    assert(std::strcmp(DisplayWakeReasonName(
                           static_cast<DisplayWakeReason>(UINT8_MAX)),
                       "unknown") == 0);

    ExternalPowerHealthLogPolicy health;
    health.reset(true, UINT32_MAX - 10000U);
    assert(health.update(false, UINT32_MAX - 9000U) ==
           ExternalPowerHealthLogEvent::Unhealthy);
    assert(health.update(false, UINT32_MAX - 8999U) ==
           ExternalPowerHealthLogEvent::None);
    assert(health.update(false, 20998U) ==
           ExternalPowerHealthLogEvent::None);
    assert(health.update(false, 20999U) ==
           ExternalPowerHealthLogEvent::Unhealthy);
    assert(health.update(false, 21000U) ==
           ExternalPowerHealthLogEvent::None);
    assert(health.update(true, 21001U) ==
           ExternalPowerHealthLogEvent::Healthy);
    assert(health.update(true, 21002U) ==
           ExternalPowerHealthLogEvent::None);

    return 0;
}
