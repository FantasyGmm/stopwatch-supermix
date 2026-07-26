#include "debug_command_service.h"

#include "debug_app_control.h"
#include "debug_assistant_control.h"

#include <hal/hal.h>
#include <runtime/assistant/assistant_runtime.h>
#include <runtime/assistant/assistant_trace.h>
#include <runtime/runtime_supervisor.h>
#include <runtime/display/global_display_protection_service.h>
#include <runtime/services/shared_wifi_service.h>

#include <cinttypes>
#include <string_view>

#include <esp_log.h>
#include <hal/usb_serial_jtag_ll.h>

namespace {
constexpr const char* tag = "DebugCommand";
DebugCommandService service;

const char* result_name(DebugAppOpenResult result)
{
    switch (result) {
        case DebugAppOpenResult::Ok:
            return "OK";
        case DebugAppOpenResult::Busy:
            return "BUSY";
        case DebugAppOpenResult::NotFound:
            return "NOT_FOUND";
    }
    return "BUSY";
}

void log_assistant_status()
{
    const auto snapshot = GetRuntimeSupervisor().diagnostics();
    ESP_LOGI(tag,
             "ASTATUS sid=%" PRIu64 " state=%s bindings=%u subscriptions=%u wifi=%u capture=%u "
             "playback=%u ap=%u blocked=%u internal=%u psram=%u tasks=%u",
             GetAssistantRuntime().sessionId(), AssistantTraceName(snapshot.assistant_state),
             static_cast<unsigned>(snapshot.assistant_bindings),
             static_cast<unsigned>(snapshot.assistant_subscriptions),
             static_cast<unsigned>(snapshot.resources.wifi_lease_count),
             static_cast<unsigned>(snapshot.resources.capture_owner),
             static_cast<unsigned>(snapshot.resources.playback_owner),
             static_cast<unsigned>(snapshot.resources.wifi_ap_active),
             static_cast<unsigned>(snapshot.resources.blocked),
             static_cast<unsigned>(snapshot.internal_free_heap),
             static_cast<unsigned>(snapshot.psram_free),
             static_cast<unsigned>(snapshot.task_count));
}

const char* display_protection_state_name(DisplayProtectionState state)
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

const char* external_power_state_name(ExternalPowerState state)
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

const char* display_wake_reason_name(DisplayWakeReason reason)
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

void log_display_protection_status()
{
    const auto snapshot = GetGlobalDisplayProtectionService().snapshot();
    ESP_LOGI(tag,
             "DISPLAY PROTECTION state=%s effective=%d user=%d automatic=%u "
             "power=%s power_valid=%u dim_choice=%u off_choice=%u "
             "dim_ms=%" PRIu32 " off_ms=%" PRIu32 " idle_ms=%" PRIu32 " "
             "dim_remaining_ms=%" PRIu32 " off_remaining_ms=%" PRIu32 " "
             "charging_guard=%u reasons=0x%02x last_wake=%s",
             display_protection_state_name(snapshot.state),
             snapshot.effective_brightness, snapshot.restore_brightness,
             static_cast<unsigned>(snapshot.automatic_enabled),
             external_power_state_name(snapshot.external_power.state),
             static_cast<unsigned>(
                 snapshot.external_power.latest_sample_valid),
             static_cast<unsigned>(snapshot.config.dim_choice),
             static_cast<unsigned>(snapshot.config.screen_off_choice),
             snapshot.timing.dim_after_ms,
             snapshot.timing.screen_off_after_ms,
             snapshot.timing.inactivity_ms,
             snapshot.timing.dim_remaining_ms,
             snapshot.timing.screen_off_remaining_ms,
             static_cast<unsigned>(snapshot.config.protect_while_charging),
             static_cast<unsigned>(snapshot.reason_mask),
             display_wake_reason_name(snapshot.last_wake_reason));
}
} // namespace

DebugCommandService& GetDebugCommandService()
{
    return service;
}

void DebugCommandService::update()
{
    std::array<uint8_t, 32> input{};
    const uint32_t count =
        usb_serial_jtag_ll_read_rxfifo(input.data(), input.size());
    for (uint32_t index = 0; index < count; ++index) {
        consume(static_cast<char>(input[index]));
    }
}

void DebugCommandService::consume(char value)
{
    if (value == '\n') {
        dispatchLine();
        resetLine();
        return;
    }
    if (_overflow) {
        return;
    }
    if (_length >= debug_command_max_line_length) {
        _overflow = true;
        return;
    }
    _line[_length++] = value;
}

void DebugCommandService::dispatchLine()
{
    if (_overflow) {
        ESP_LOGE(tag, "DEBUG ERROR LINE_TOO_LONG");
        return;
    }

    const DebugCommand command =
        ParseDebugCommand(std::string_view(_line.data(), _length));
    if (command.error == DebugCommandError::LineTooLong) {
        ESP_LOGE(tag, "DEBUG ERROR LINE_TOO_LONG");
        return;
    }
    if (command.error != DebugCommandError::None) {
        ESP_LOGE(tag, "DEBUG ERROR INVALID_COMMAND");
        return;
    }
    if (command.kind == DebugCommandKind::AppList) {
        if (!RequestDebugAppList()) {
            ESP_LOGW(tag, "DEBUG APP LIST BUSY");
        }
        return;
    }
    if (command.kind == DebugCommandKind::AppOpen) {
        const auto result = RequestDebugAppOpen(command.argument.data());
        ESP_LOGI(tag, "DEBUG APP OPEN %s %s", command.argument.data(), result_name(result));
        return;
    }
    if (command.kind == DebugCommandKind::AppHome) {
        auto& assistant = GetAssistantRuntime();
        GetAssistantTrace().record({
            .session_id = assistant.sessionId(),
            .timestamp_ms = GetHAL().millis(),
            .component = AssistantTraceComponent::Debug,
            .event = AssistantTraceEvent::ForceHome,
            .state = assistant.state(),
            .detail = AssistantTraceDetail::ForceHome,
            .result = AssistantTraceResult::Accepted,
        });
        ESP_LOGI(tag, "DEBUG APP HOME %s", RequestDebugAppHome() ? "OK" : "BUSY");
        return;
    }
    if (command.kind == DebugCommandKind::AssistantHome) {
        ESP_LOGI(tag, "DEBUG ASSISTANT HOME %s",
                 RequestDebugAssistantHome() ? "OK" : "BUSY");
        return;
    }
    if (command.kind == DebugCommandKind::AssistantStatus) {
        log_assistant_status();
        ESP_LOGI(tag, "DEBUG ASSISTANT STATUS OK");
        return;
    }
    if (command.kind == DebugCommandKind::AssistantTrace) {
        GetAssistantTrace().dump();
        ESP_LOGI(tag, "DEBUG ASSISTANT TRACE OK");
        return;
    }
    if (command.kind == DebugCommandKind::AssistantTraceClear) {
        GetAssistantTrace().clear();
        ESP_LOGI(tag, "DEBUG ASSISTANT TRACE CLEAR OK");
        return;
    }
    if (command.kind == DebugCommandKind::AssistantWifiCycle) {
        auto& assistant = GetAssistantRuntime();
        const bool active = assistant.state() != AssistantRuntimeState::Stopped;
        const bool cycled = active && GetSharedWifiService().cycleStaForValidation();
        GetAssistantTrace().record({
            .session_id = assistant.sessionId(),
            .timestamp_ms = GetHAL().millis(),
            .component = AssistantTraceComponent::Debug,
            .event = AssistantTraceEvent::ResourceDecision,
            .state = assistant.state(),
            .detail = AssistantTraceDetail::Wifi,
            .result = cycled ? AssistantTraceResult::Accepted
                             : AssistantTraceResult::Rejected,
        });
        ESP_LOGI(tag, "DEBUG ASSISTANT WIFI CYCLE %s", cycled ? "OK" : "BUSY");
        return;
    }
    if (command.kind == DebugCommandKind::DisplayProtectionStatus) {
        log_display_protection_status();
        return;
    }
    if (command.kind == DebugCommandKind::VolumeSet) {
        GetHAL().setSpeakerVolume(command.value, false);
        ESP_LOGI(tag, "DEBUG VOLUME %u OK", static_cast<unsigned>(command.value));
        return;
    }
    ESP_LOGE(tag, "DEBUG ERROR INVALID_COMMAND");
}

void DebugCommandService::resetLine()
{
    _line.fill('\0');
    _length = 0;
    _overflow = false;
}
