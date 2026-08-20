#include "debug_command_parser.h"

#include <cctype>

namespace {
constexpr std::string_view list_command = "debug app list";
constexpr std::string_view home_command = "debug app home";
constexpr std::string_view open_prefix = "debug app open ";
constexpr std::string_view assistant_home_command = "debug assistant home";
constexpr std::string_view assistant_status_command = "debug assistant status";
constexpr std::string_view assistant_trace_command = "debug assistant trace";
constexpr std::string_view assistant_trace_clear_command = "debug assistant trace clear";
constexpr std::string_view assistant_wifi_cycle_command = "debug assistant wifi-cycle";
constexpr std::string_view display_protection_command = "debug display protection";
constexpr std::string_view battery_start_command = "debug battery start";
constexpr std::string_view battery_status_command = "debug battery status";
constexpr std::string_view battery_export_command = "debug battery export";
constexpr std::string_view volume_prefix = "debug volume ";

char normalized_character(char value)
{
    const auto byte = static_cast<unsigned char>(value);
    if (!std::isalnum(byte)) {
        return '-';
    }
    return static_cast<char>(std::tolower(byte));
}
} // namespace

DebugCommand ParseDebugCommand(std::string_view line)
{
    DebugCommand command;
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.size() > debug_command_max_line_length) {
        command.error = DebugCommandError::LineTooLong;
        return command;
    }
    if (line == list_command) {
        command.kind = DebugCommandKind::AppList;
        return command;
    }
    if (line == home_command) {
        command.kind = DebugCommandKind::AppHome;
        return command;
    }
    if (line == assistant_home_command) {
        command.kind = DebugCommandKind::AssistantHome;
        return command;
    }
    if (line == assistant_status_command) {
        command.kind = DebugCommandKind::AssistantStatus;
        return command;
    }
    if (line == assistant_trace_command) {
        command.kind = DebugCommandKind::AssistantTrace;
        return command;
    }
    if (line == assistant_trace_clear_command) {
        command.kind = DebugCommandKind::AssistantTraceClear;
        return command;
    }
    if (line == assistant_wifi_cycle_command) {
        command.kind = DebugCommandKind::AssistantWifiCycle;
        return command;
    }
    if (line == display_protection_command) {
        command.kind = DebugCommandKind::DisplayProtectionStatus;
        return command;
    }
    if (line == battery_start_command) {
        command.kind = DebugCommandKind::BatteryStart;
        return command;
    }
    if (line == battery_status_command) {
        command.kind = DebugCommandKind::BatteryStatus;
        return command;
    }
    if (line == battery_export_command) {
        command.kind = DebugCommandKind::BatteryExport;
        return command;
    }
    if (line.size() > volume_prefix.size() &&
        line.substr(0, volume_prefix.size()) == volume_prefix) {
        unsigned value = 0;
        for (char character : line.substr(volume_prefix.size())) {
            if (!std::isdigit(static_cast<unsigned char>(character))) {
                command.error = DebugCommandError::InvalidCommand;
                return command;
            }
            value = value * 10U + static_cast<unsigned>(character - '0');
            if (value > 100U) {
                command.error = DebugCommandError::InvalidCommand;
                return command;
            }
        }
        command.kind = DebugCommandKind::VolumeSet;
        command.value = static_cast<uint8_t>(value);
        return command;
    }
    if (line.size() <= open_prefix.size() || line.substr(0, open_prefix.size()) != open_prefix) {
        command.error = DebugCommandError::InvalidCommand;
        return command;
    }

    const std::string_view name = line.substr(open_prefix.size());
    if (name.size() >= command.argument.size()) {
        command.error = DebugCommandError::InvalidCommand;
        return command;
    }
    for (std::size_t index = 0; index < name.size(); ++index) {
        const auto byte = static_cast<unsigned char>(name[index]);
        if (!std::isalnum(byte) && name[index] != '-' && name[index] != '_') {
            command.error = DebugCommandError::InvalidCommand;
            command.argument.fill('\0');
            return command;
        }
        command.argument[index] = normalized_character(name[index]);
    }
    command.kind = DebugCommandKind::AppOpen;
    return command;
}

bool DebugAppNameEquals(std::string_view installed_name, std::string_view normalized_name)
{
    std::size_t installed_index = 0;
    std::size_t normalized_index = 0;
    while (installed_index < installed_name.size() && normalized_index < normalized_name.size()) {
        const char installed = normalized_character(installed_name[installed_index++]);
        const char expected = normalized_character(normalized_name[normalized_index++]);
        if (installed != expected) {
            return false;
        }
    }
    return installed_index == installed_name.size() && normalized_index == normalized_name.size();
}
