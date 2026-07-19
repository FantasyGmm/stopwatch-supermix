#include "debug_command_service.h"

#include "debug_app_control.h"
#include "debug_assistant_control.h"

#include <hal/hal.h>
#include <runtime/assistant/assistant_runtime.h>
#include <runtime/assistant/assistant_trace.h>

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
        ESP_LOGI(tag, "DEBUG ASSISTANT STATUS %s",
                 RequestDebugAssistantStatus() ? "OK" : "BUSY");
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
    ESP_LOGE(tag, "DEBUG ERROR INVALID_COMMAND");
}

void DebugCommandService::resetLine()
{
    _line.fill('\0');
    _length = 0;
    _overflow = false;
}
