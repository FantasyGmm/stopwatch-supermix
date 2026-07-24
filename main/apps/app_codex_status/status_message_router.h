#pragma once

#include <cstddef>
#include <cstdint>


enum class StatusMessageSource : uint8_t {
    LegacyCodex,
    Codex,
    Trae,
    Unknown,
};


StatusMessageSource route_status_message(const char* data, size_t length);
