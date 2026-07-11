#include "codex_ble_framing.h"


std::optional<CodexBleMessage> CodexBleReassembler::push(const uint8_t* data, size_t length, uint32_t now_ms)
{
    if (_active && now_ms - _last_fragment_ms > kTimeoutMs) {
        reset();
    }
    if (data == nullptr || length <= kHeaderSize) {
        reset();
        return std::nullopt;
    }

    const uint8_t version = data[0];
    const uint8_t type = data[1];
    const uint16_t sequence = static_cast<uint16_t>(data[2]) |
                              (static_cast<uint16_t>(data[3]) << 8);
    const uint8_t index = data[4];
    const uint8_t count = data[5];
    const size_t payload_length = length - kHeaderSize;

    if (version != 1 ||
        (type != kCodexBleStatusMessage && type != kCodexBleEventMessage) ||
        count == 0 || index >= count) {
        reset();
        return std::nullopt;
    }

    if (index == 0) {
        reset();
        _active = true;
        _type = type;
        _sequence = sequence;
        _fragment_count = count;
    } else if (!_active || type != _type || sequence != _sequence ||
               count != _fragment_count || index != _next_fragment) {
        reset();
        return std::nullopt;
    }

    if (_payload.size() + payload_length > kMaxPayloadSize) {
        reset();
        return std::nullopt;
    }

    _payload.append(reinterpret_cast<const char*>(data + kHeaderSize), payload_length);
    _last_fragment_ms = now_ms;
    _next_fragment = static_cast<uint8_t>(index + 1);

    if (_next_fragment != _fragment_count) {
        return std::nullopt;
    }

    CodexBleMessage message;
    message.type = _type;
    message.payload = std::move(_payload);
    reset();
    return message;
}


void CodexBleReassembler::reset()
{
    _active = false;
    _type = 0;
    _fragment_count = 0;
    _next_fragment = 0;
    _sequence = 0;
    _last_fragment_ms = 0;
    _payload.clear();
}
