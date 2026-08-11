#pragma once

#include "voice_input_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace input_action {

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kReadyControlSize = 20;
constexpr size_t kWireActionSize = 20;

enum class Kind : uint8_t { Submit = 1, ForceSteer = 2 };

struct ReadyControl {
    voice_input::crypto::ExchangeNonce connection_nonce = {};
};

struct Action {
    Kind kind = Kind::Submit;
    uint32_t sequence = 0;
    uint32_t watch_ms = 0;
};

struct WireAction {
    Action logical = {};
    std::array<uint8_t, kWireActionSize> bytes = {};
};

size_t encode_ready(
    const ReadyControl& ready,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity);
bool decode_ready(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    ReadyControl& ready);
size_t encode_action(
    const Action& action,
    const voice_input::crypto::ExchangeNonce& connection_nonce,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity);
bool decode_action(
    const uint8_t* bytes,
    size_t length,
    const voice_input::crypto::ExchangeNonce& connection_nonce,
    voice_input::crypto::CcmContext& crypto,
    Action& action);

}  // namespace input_action
