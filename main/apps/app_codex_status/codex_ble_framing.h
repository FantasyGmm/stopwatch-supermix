#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

constexpr uint8_t kCodexBleStatusMessage = 1;
constexpr uint8_t kCodexBleEventMessage = 2;


struct CodexBleMessage {
    uint8_t type = 0;
    std::string payload;
};


class CodexBleReassembler {
public:
    std::optional<CodexBleMessage> push(const uint8_t* data, size_t length, uint32_t now_ms);
    void reset();

private:
    static constexpr size_t kHeaderSize = 6;
    static constexpr size_t kMaxPayloadSize = 2048;
    static constexpr uint32_t kTimeoutMs = 3000;

    bool _active = false;
    uint8_t _type = 0;
    uint8_t _fragment_count = 0;
    uint8_t _next_fragment = 0;
    uint16_t _sequence = 0;
    uint32_t _last_fragment_ms = 0;
    std::string _payload;
};
