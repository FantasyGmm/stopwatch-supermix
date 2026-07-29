#include "voice_input_protocol.h"

#include <algorithm>

namespace voice_input {
namespace {

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

bool valid_reason(uint8_t reason)
{
    return reason >= static_cast<uint8_t>(UnavailableReason::DriverRequired) &&
           reason <= static_cast<uint8_t>(UnavailableReason::InitializationFailed);
}

bool control_values_valid(const VoiceControl& control)
{
    if (control.kind == ControlKind::Ready) {
        return control.reason == 0 && control.target_bps == kTargetBitrate;
    }
    if (control.kind == ControlKind::Unavailable) {
        return valid_reason(control.reason) && control.target_bps == 0;
    }
    return false;
}

crypto::Domain control_domain(ControlKind kind)
{
    return kind == ControlKind::Ready ? crypto::Domain::Ready
                                      : crypto::Domain::Unavailable;
}

bool valid_notification_kind(NotificationKind kind)
{
    switch (kind) {
        case NotificationKind::PttBegin:
        case NotificationKind::Audio:
        case NotificationKind::PttEnd:
        case NotificationKind::VoiceError:
            return true;
    }
    return false;
}

crypto::Domain notification_domain(NotificationKind kind)
{
    switch (kind) {
        case NotificationKind::PttBegin:
            return crypto::Domain::PttBegin;
        case NotificationKind::Audio:
            return crypto::Domain::Audio;
        case NotificationKind::PttEnd:
            return crypto::Domain::PttEnd;
        case NotificationKind::VoiceError:
            return crypto::Domain::VoiceError;
    }
    return static_cast<crypto::Domain>(0);
}

size_t logical_payload_size(NotificationKind kind)
{
    switch (kind) {
        case NotificationKind::PttBegin:
            return kBeginBodySize;
        case NotificationKind::Audio:
            return kAudioPlaintextSize;
        case NotificationKind::PttEnd:
            return kEndBodySize;
        case NotificationKind::VoiceError:
            return kErrorBodySize;
    }
    return 0;
}

bool logical_payload_valid(const VoiceNotification& notification)
{
    if (!valid_notification_kind(notification.kind) || notification.session == 0 ||
        notification.payload_size != logical_payload_size(notification.kind)) {
        return false;
    }
    if (notification.kind == NotificationKind::PttBegin) {
        BeginBody body = {};
        return notification.sequence == 0 &&
               decode_begin_body(
                   notification.payload.data(), notification.payload_size, body);
    }
    if (notification.sequence == 0) {
        return false;
    }
    if (notification.kind == NotificationKind::PttEnd) {
        EndSummary summary = {};
        return decode_end_summary(
            notification.payload.data(), notification.payload_size, summary);
    }
    if (notification.kind == NotificationKind::VoiceError) {
        ErrorBody error = {};
        return decode_error_body(
            notification.payload.data(), notification.payload_size, error);
    }
    return true;
}

void encode_header(
    const VoiceNotification& notification,
    uint16_t payload_size,
    uint8_t* output)
{
    output[0] = kProtocolVersion;
    output[1] = static_cast<uint8_t>(notification.kind);
    write_u16(output + 2, notification.session);
    write_u32(output + 4, notification.sequence);
    write_u32(output + 8, notification.watch_ms);
    write_u16(output + 12, payload_size);
}

}  // namespace

size_t encode_control(
    const VoiceControl& control,
    crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity)
{
    if (output == nullptr || capacity < kControlSize ||
        !control_values_valid(control)) {
        return 0;
    }
    uint8_t aad[kControlAadSize] = {};
    aad[0] = kProtocolVersion;
    aad[1] = static_cast<uint8_t>(control.kind);
    aad[2] = control.reason;
    std::copy(
        control.exchange_nonce.begin(), control.exchange_nonce.end(), aad + 4);
    uint8_t body[kControlBodySize] = {};
    write_u32(body, control.target_bps);
    size_t encrypted_size = 0;
    if (!crypto.encrypt(
            control_domain(control.kind),
            control.exchange_nonce,
            0,
            aad,
            sizeof(aad),
            body,
            sizeof(body),
            output + kControlAadSize,
            capacity - kControlAadSize,
            encrypted_size) ||
        encrypted_size != kControlBodySize + crypto::kTagSize) {
        return 0;
    }
    std::copy_n(aad, sizeof(aad), output);
    return kControlSize;
}

bool decode_control(
    const uint8_t* bytes,
    size_t length,
    crypto::CcmContext& crypto,
    VoiceControl& control)
{
    if (bytes == nullptr || length != kControlSize ||
        bytes[0] != kProtocolVersion || bytes[3] != 0 ||
        (bytes[1] != static_cast<uint8_t>(ControlKind::Ready) &&
         bytes[1] != static_cast<uint8_t>(ControlKind::Unavailable))) {
        return false;
    }
    VoiceControl candidate = {};
    candidate.kind = static_cast<ControlKind>(bytes[1]);
    candidate.reason = bytes[2];
    std::copy_n(bytes + 4, candidate.exchange_nonce.size(), candidate.exchange_nonce.begin());
    uint8_t body[kControlBodySize] = {};
    size_t body_size = 0;
    if (!crypto.decrypt(
            control_domain(candidate.kind),
            candidate.exchange_nonce,
            0,
            bytes,
            kControlAadSize,
            bytes + kControlAadSize,
            length - kControlAadSize,
            body,
            sizeof(body),
            body_size) ||
        body_size != sizeof(body)) {
        return false;
    }
    candidate.target_bps = read_u32(body);
    if (!control_values_valid(candidate)) {
        return false;
    }
    control = candidate;
    return true;
}

size_t encode_notification(
    const VoiceNotification& notification,
    crypto::CcmContext& crypto,
    uint8_t* output,
    size_t capacity)
{
    if (output == nullptr || !logical_payload_valid(notification)) {
        return 0;
    }
    const size_t clear_nonce_size =
        notification.kind == NotificationKind::PttBegin
            ? crypto::kExchangeNonceSize
            : 0;
    const size_t encrypted_size = notification.payload_size + crypto::kTagSize;
    const size_t wire_payload_size = clear_nonce_size + encrypted_size;
    const size_t total_size = kNotificationHeaderSize + wire_payload_size;
    if (wire_payload_size > UINT16_MAX || total_size > capacity ||
        total_size > kMaxNotificationSize) {
        return 0;
    }
    encode_header(notification, static_cast<uint16_t>(wire_payload_size), output);
    size_t aad_size = kNotificationHeaderSize;
    if (clear_nonce_size != 0) {
        std::copy(
            notification.session_nonce.begin(),
            notification.session_nonce.end(),
            output + kNotificationHeaderSize);
        aad_size += clear_nonce_size;
    }
    size_t actual_encrypted_size = 0;
    if (!crypto.encrypt(
            notification_domain(notification.kind),
            notification.session_nonce,
            notification.sequence,
            output,
            aad_size,
            notification.payload.data(),
            notification.payload_size,
            output + aad_size,
            capacity - aad_size,
            actual_encrypted_size) ||
        actual_encrypted_size != encrypted_size) {
        return 0;
    }
    return total_size;
}

bool decode_notification(
    const uint8_t* bytes,
    size_t length,
    crypto::CcmContext& crypto,
    const crypto::ExchangeNonce* session_nonce,
    VoiceNotification& notification)
{
    if (bytes == nullptr || length < kNotificationHeaderSize ||
        bytes[0] != kProtocolVersion) {
        return false;
    }
    VoiceNotification candidate = {};
    candidate.kind = static_cast<NotificationKind>(bytes[1]);
    candidate.session = read_u16(bytes + 2);
    candidate.sequence = read_u32(bytes + 4);
    candidate.watch_ms = read_u32(bytes + 8);
    const size_t wire_payload_size = read_u16(bytes + 12);
    if (!valid_notification_kind(candidate.kind) || candidate.session == 0 ||
        length != kNotificationHeaderSize + wire_payload_size) {
        return false;
    }
    const size_t clear_nonce_size =
        candidate.kind == NotificationKind::PttBegin
            ? crypto::kExchangeNonceSize
            : 0;
    const size_t expected_payload_size = logical_payload_size(candidate.kind);
    if (wire_payload_size !=
        clear_nonce_size + expected_payload_size + crypto::kTagSize) {
        return false;
    }
    size_t aad_size = kNotificationHeaderSize;
    if (clear_nonce_size != 0) {
        std::copy_n(
            bytes + kNotificationHeaderSize,
            candidate.session_nonce.size(),
            candidate.session_nonce.begin());
        aad_size += clear_nonce_size;
    } else {
        if (session_nonce == nullptr) {
            return false;
        }
        candidate.session_nonce = *session_nonce;
    }
    if (!crypto.decrypt(
            notification_domain(candidate.kind),
            candidate.session_nonce,
            candidate.sequence,
            bytes,
            aad_size,
            bytes + aad_size,
            length - aad_size,
            candidate.payload.data(),
            candidate.payload.size(),
            candidate.payload_size) ||
        candidate.payload_size != expected_payload_size ||
        !logical_payload_valid(candidate)) {
        return false;
    }
    notification = candidate;
    return true;
}

size_t encode_begin_body(const BeginBody& body, uint8_t* output, size_t capacity)
{
    if (output == nullptr || capacity < kBeginBodySize ||
        body.codec != kImaAdpcmCodec || body.sample_rate != kSampleRate ||
        body.block_samples != kBlockSamples) {
        return 0;
    }
    output[0] = body.codec;
    write_u16(output + 1, body.sample_rate);
    write_u16(output + 3, body.block_samples);
    return kBeginBodySize;
}

bool decode_begin_body(const uint8_t* bytes, size_t length, BeginBody& body)
{
    if (bytes == nullptr || length != kBeginBodySize) {
        return false;
    }
    BeginBody candidate = {};
    candidate.codec = bytes[0];
    candidate.sample_rate = read_u16(bytes + 1);
    candidate.block_samples = read_u16(bytes + 3);
    if (candidate.codec != kImaAdpcmCodec || candidate.sample_rate != kSampleRate ||
        candidate.block_samples != kBlockSamples) {
        return false;
    }
    body = candidate;
    return true;
}

size_t encode_end_summary(const EndSummary& summary, uint8_t* output, size_t capacity)
{
    if (output == nullptr || capacity < kEndBodySize) {
        return 0;
    }
    write_u32(output, summary.generated_packets);
    write_u32(output + 4, summary.accepted_packets);
    write_u32(output + 8, summary.queue_overflows);
    write_u32(output + 12, summary.encode_failures);
    write_u32(output + 16, summary.captured_samples);
    write_u32(output + 20, summary.elapsed_ms);
    write_u16(output + 24, summary.negotiated_mtu);
    return kEndBodySize;
}

bool decode_end_summary(const uint8_t* bytes, size_t length, EndSummary& summary)
{
    if (bytes == nullptr || length != kEndBodySize) {
        return false;
    }
    EndSummary candidate = {};
    candidate.generated_packets = read_u32(bytes);
    candidate.accepted_packets = read_u32(bytes + 4);
    candidate.queue_overflows = read_u32(bytes + 8);
    candidate.encode_failures = read_u32(bytes + 12);
    candidate.captured_samples = read_u32(bytes + 16);
    candidate.elapsed_ms = read_u32(bytes + 20);
    candidate.negotiated_mtu = read_u16(bytes + 24);
    summary = candidate;
    return true;
}

size_t encode_error_body(const ErrorBody& error, uint8_t* output, size_t capacity)
{
    if (output == nullptr || capacity < kErrorBodySize) {
        return 0;
    }
    write_u16(output, error.code);
    write_u16(output + 2, error.detail);
    return kErrorBodySize;
}

bool decode_error_body(const uint8_t* bytes, size_t length, ErrorBody& error)
{
    if (bytes == nullptr || length != kErrorBodySize) {
        return false;
    }
    ErrorBody candidate = {};
    candidate.code = read_u16(bytes);
    candidate.detail = read_u16(bytes + 2);
    error = candidate;
    return true;
}

}  // namespace voice_input
