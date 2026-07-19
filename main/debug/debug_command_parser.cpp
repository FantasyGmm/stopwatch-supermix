#include "debug_command_parser.h"

#include <cctype>

namespace {
constexpr std::string_view list_command = "debug app list";
constexpr std::string_view home_command = "debug app home";
constexpr std::string_view open_prefix = "debug app open ";

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
