#include "input_action_protocol.h"

#include <algorithm>

namespace input_action {
namespace {

constexpr uint8_t kReadyKind = 1;
constexpr size_t kReadyAadSize = 12;
constexpr size_t kActionHeaderSize = 12;

uint16_t read_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U);
}

uint32_t read_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

void write_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

bool nonzero_nonce(const voice_input::crypto::ExchangeNonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t value) {
        return value != 0;
    });
}

bool valid_kind(Kind kind)
{
    return kind == Kind::Submit || kind == Kind::ForceSteer;
}

voice_input::crypto::Domain action_domain(Kind kind)
{
    return kind == Kind::Submit
               ? voice_input::crypto::Domain::InputSubmit
               : voice_input::crypto::Domain::InputForceSteer;
}

}  // namespace

size_t encode_ready(
    const ReadyControl& ready,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity)
{
    if (output == nullptr || capacity < kReadyControlSize ||
        !nonzero_nonce(ready.connection_nonce)) {
        return 0;
    }
    output[0] = kProtocolVersion;
    output[1] = kReadyKind;
    output[2] = 0;
    output[3] = 0;
    std::copy(
        ready.connection_nonce.begin(), ready.connection_nonce.end(), output + 4);
    size_t encrypted_size = 0;
    if (!crypto.encrypt(
            voice_input::crypto::Domain::InputReady,
            ready.connection_nonce,
            0,
            output,
            kReadyAadSize,
            nullptr,
            0,
            output + kReadyAadSize,
            capacity - kReadyAadSize,
            encrypted_size) ||
        encrypted_size != voice_input::crypto::kTagSize) {
        return 0;
    }
    return kReadyControlSize;
}

bool decode_ready(
    const uint8_t* bytes,
    size_t length,
    voice_input::crypto::CcmContext& crypto,
    ReadyControl& ready)
{
    if (bytes == nullptr || length != kReadyControlSize ||
        bytes[0] != kProtocolVersion || bytes[1] != kReadyKind ||
        read_u16(bytes + 2) != 0) {
        return false;
    }
    ReadyControl candidate = {};
    std::copy_n(
        bytes + 4,
        candidate.connection_nonce.size(),
        candidate.connection_nonce.begin());
    if (!nonzero_nonce(candidate.connection_nonce)) {
        return false;
    }
    uint8_t empty_output = 0;
    size_t output_size = 0;
    if (!crypto.decrypt(
            voice_input::crypto::Domain::InputReady,
            candidate.connection_nonce,
            0,
            bytes,
            kReadyAadSize,
            bytes + kReadyAadSize,
            voice_input::crypto::kTagSize,
            &empty_output,
            0,
            output_size) ||
        output_size != 0) {
        return false;
    }
    ready = candidate;
    return true;
}

size_t encode_action(
    const Action& action,
    const voice_input::crypto::ExchangeNonce& connection_nonce,
    voice_input::crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity)
{
    if (output == nullptr || capacity < kWireActionSize ||
        !valid_kind(action.kind) || action.sequence == 0 ||
        !nonzero_nonce(connection_nonce)) {
        return 0;
    }
    output[0] = kProtocolVersion;
    output[1] = static_cast<uint8_t>(action.kind);
    write_u32(output + 2, action.sequence);
    write_u32(output + 6, action.watch_ms);
    write_u16(output + 10, voice_input::crypto::kTagSize);
    size_t encrypted_size = 0;
    if (!crypto.encrypt(
            action_domain(action.kind),
            connection_nonce,
            action.sequence,
            output,
            kActionHeaderSize,
            nullptr,
            0,
            output + kActionHeaderSize,
            capacity - kActionHeaderSize,
            encrypted_size) ||
        encrypted_size != voice_input::crypto::kTagSize) {
        return 0;
    }
    return kWireActionSize;
}

bool decode_action(
    const uint8_t* bytes,
    size_t length,
    const voice_input::crypto::ExchangeNonce& connection_nonce,
    voice_input::crypto::CcmContext& crypto,
    Action& action)
{
    if (bytes == nullptr || length != kWireActionSize ||
        bytes[0] != kProtocolVersion ||
        read_u16(bytes + 10) != voice_input::crypto::kTagSize ||
        !nonzero_nonce(connection_nonce)) {
        return false;
    }
    Action candidate = {};
    candidate.kind = static_cast<Kind>(bytes[1]);
    candidate.sequence = read_u32(bytes + 2);
    candidate.watch_ms = read_u32(bytes + 6);
    if (!valid_kind(candidate.kind) || candidate.sequence == 0) {
        return false;
    }
    uint8_t empty_output = 0;
    size_t output_size = 0;
    if (!crypto.decrypt(
            action_domain(candidate.kind),
            connection_nonce,
            candidate.sequence,
            bytes,
            kActionHeaderSize,
            bytes + kActionHeaderSize,
            voice_input::crypto::kTagSize,
            &empty_output,
            0,
            output_size) ||
        output_size != 0) {
        return false;
    }
    action = candidate;
    return true;
}

}  // namespace input_action
