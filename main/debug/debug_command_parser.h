#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

enum class DebugCommandKind : uint8_t {
    None,
    AppList,
    AppOpen,
    AppHome,
    AssistantHome,
    AssistantStatus,
    AssistantTrace,
    AssistantTraceClear,
    AssistantWifiCycle,
    DisplayProtectionStatus,
    VolumeSet,
};

enum class DebugCommandError : uint8_t {
    None,
    InvalidCommand,
    LineTooLong,
};

struct DebugCommand {
    DebugCommandKind kind = DebugCommandKind::None;
    DebugCommandError error = DebugCommandError::None;
    std::array<char, 49> argument{};
    uint8_t value = 0;
};

constexpr std::size_t debug_command_max_line_length = 64;

DebugCommand ParseDebugCommand(std::string_view line);
bool DebugAppNameEquals(std::string_view installed_name, std::string_view normalized_name);
