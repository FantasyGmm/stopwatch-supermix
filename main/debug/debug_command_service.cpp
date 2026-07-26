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
    return "active";
}

void log_display_protection_status()
{
    const auto snapshot = GetGlobalDisplayProtectionService().snapshot();
    ESP_LOGI(tag,
             "DISPLAY PROTECTION state=%s brightness=%d restore=%d automatic=%u reasons=0x%02x",
             display_protection_state_name(snapshot.state),
             snapshot.effective_brightness, snapshot.restore_brightness,
             static_cast<unsigned>(snapshot.automatic_enabled),
             static_cast<unsigned>(snapshot.reason_mask));
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
