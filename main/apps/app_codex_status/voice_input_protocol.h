#pragma once

#include "voice_input_crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace voice_input {

constexpr uint8_t kProtocolVersion = 3;
constexpr uint32_t kTargetBitrate = 8192;
constexpr uint8_t kImaAdpcmCodec = 1;
constexpr uint16_t kSampleRate = 16000;
constexpr uint16_t kBlockSamples = 320;
constexpr size_t kControlAadSize = 12;
constexpr size_t kControlBodySize = 4;
constexpr size_t kControlSize = kControlAadSize + kControlBodySize + crypto::kTagSize;
constexpr size_t kNotificationHeaderSize = 14;
constexpr size_t kBeginBodySize = 5;
constexpr size_t kAudioPlaintextSize = 164;
constexpr size_t kEndBodySize = 26;
constexpr size_t kErrorBodySize = 4;
constexpr size_t kAudioCharacteristicValueSize =
    kNotificationHeaderSize + kAudioPlaintextSize + crypto::kTagSize;
constexpr size_t kMaxNotificationSize = 253;

enum class ControlKind : uint8_t { Ready = 1, Unavailable = 2 };
enum class NotificationKind : uint8_t {
    PttBegin = 1,
    Audio = 2,
    PttEnd = 3,
    VoiceError = 4,
};
enum class UnavailableReason : uint8_t {
    DriverRequired = 1,
    RestartRequired = 2,
    AccessibilityRequired = 3,
    TypelessNotRunning = 4,
    InitializationFailed = 5,
};

struct VoiceControl {
    ControlKind kind = ControlKind::Unavailable;
    uint8_t reason = 0;
    crypto::ExchangeNonce exchange_nonce = {};
    uint32_t target_bps = 0;
};

struct BeginBody {
    uint8_t codec = kImaAdpcmCodec;
    uint16_t sample_rate = kSampleRate;
    uint16_t block_samples = kBlockSamples;
};

struct EndSummary {
    uint32_t generated_packets = 0;
    uint32_t accepted_packets = 0;
    uint32_t queue_overflows = 0;
    uint32_t encode_failures = 0;
    uint32_t captured_samples = 0;
    uint32_t elapsed_ms = 0;
    uint16_t negotiated_mtu = 23;
};

struct ErrorBody {
    uint16_t code = 0;
    uint16_t detail = 0;
};

struct VoiceNotification {
    NotificationKind kind = NotificationKind::VoiceError;
    uint16_t session = 0;
    uint32_t sequence = 0;
    uint32_t watch_ms = 0;
    crypto::ExchangeNonce session_nonce = {};
    size_t payload_size = 0;
    std::array<uint8_t, kAudioPlaintextSize> payload = {};
};

size_t encode_control(
    const VoiceControl& control,
    crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity);
bool decode_control(
    const uint8_t* bytes,
    size_t length,
    crypto::CcmContext& crypto,
    VoiceControl& control);

size_t encode_notification(
    const VoiceNotification& notification,
    crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity);
bool decode_notification(
    const uint8_t* bytes,
    size_t length,
    crypto::CcmContext& crypto,
    const crypto::ExchangeNonce* session_nonce,
    VoiceNotification& notification);

size_t encode_begin_body(const BeginBody& body, uint8_t* output, size_t capacity);
bool decode_begin_body(const uint8_t* bytes, size_t length, BeginBody& body);
size_t encode_end_summary(const EndSummary& summary, uint8_t* output, size_t capacity);
bool decode_end_summary(const uint8_t* bytes, size_t length, EndSummary& summary);
size_t encode_error_body(const ErrorBody& error, uint8_t* output, size_t capacity);
bool decode_error_body(const uint8_t* bytes, size_t length, ErrorBody& error);

}  // namespace voice_input
