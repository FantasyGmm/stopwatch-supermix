#include "main/apps/app_codex_status/voice_input_protocol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

namespace {

constexpr voice_input::crypto::Key kTestKey = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
constexpr voice_input::crypto::ExchangeNonce kTestNonce = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};
constexpr uint32_t kTestEpoch = 7;
constexpr std::array<uint8_t, 28> kReadyVector = {
    0x04, 0x01, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0xb0, 0xf4, 0xd7, 0xed, 0xb4, 0x99, 0xc5, 0x1c,
    0x18, 0xfe, 0xe1, 0xe9,
};
constexpr std::array<uint8_t, 39> kBeginVector = {
    0x04, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0xdb, 0x7a, 0x3c, 0x2d, 0x9d, 0x0b,
    0x84, 0xd4, 0xce, 0xe7, 0x3f, 0x0f, 0x58,
};

}  // namespace

int main()
{
    using namespace voice_input;

    crypto::CcmContext context(kTestKey);
    VoiceControl epoch_ready = {};
    epoch_ready.kind = ControlKind::Ready;
    epoch_ready.key_epoch = kTestEpoch;
    epoch_ready.exchange_nonce = kTestNonce;
    epoch_ready.target_bps = kTargetBitrate;
    std::array<uint8_t, kControlSize> epoch_ready_bytes = {};
    assert(encode_control(
               epoch_ready,
               context,
               epoch_ready_bytes.data(),
               epoch_ready_bytes.size()) == epoch_ready_bytes.size());
    VoiceControl decoded_epoch_ready = {};
    assert(decode_control(
        epoch_ready_bytes.data(),
        epoch_ready_bytes.size(),
        context,
        kTestEpoch,
        decoded_epoch_ready));
    assert(decoded_epoch_ready.key_epoch == kTestEpoch);
    assert(!decode_control(
        epoch_ready_bytes.data(),
        epoch_ready_bytes.size(),
        context,
        kTestEpoch + 1,
        decoded_epoch_ready));

    VoiceControl ready = {};
    assert(decode_control(
        kReadyVector.data(),
        kReadyVector.size(),
        context,
        kTestEpoch,
        ready));
    assert(ready.kind == ControlKind::Ready);
    assert(ready.reason == 0);
    assert(ready.exchange_nonce == kTestNonce);
    assert(ready.target_bps == kTargetBitrate);

    std::array<uint8_t, kControlSize> encoded_ready = {};
    assert(encode_control(ready, context, encoded_ready.data(), encoded_ready.size()) ==
           kReadyVector.size());
    assert(encoded_ready == kReadyVector);

    VoiceControl unavailable = {};
    unavailable.kind = ControlKind::Unavailable;
    unavailable.reason = static_cast<uint8_t>(UnavailableReason::DriverRequired);
    unavailable.key_epoch = kTestEpoch;
    unavailable.exchange_nonce = kTestNonce;
    unavailable.target_bps = 0;
    assert(encode_control(
               unavailable, context, encoded_ready.data(), encoded_ready.size()) ==
           kControlSize);
    VoiceControl decoded_unavailable = {};
    assert(decode_control(
        encoded_ready.data(),
        encoded_ready.size(),
        context,
        kTestEpoch,
        decoded_unavailable));
    assert(decoded_unavailable.kind == ControlKind::Unavailable);
    assert(decoded_unavailable.reason == unavailable.reason);
    assert(decoded_unavailable.target_bps == 0);

    VoiceNotification begin = {};
    begin.kind = NotificationKind::PttBegin;
    begin.session = 7;
    begin.sequence = 0;
    begin.watch_ms = 100;
    begin.key_epoch = kTestEpoch;
    begin.session_nonce = kTestNonce;
    begin.payload_size = encode_begin_body(
        BeginBody{}, begin.payload.data(), begin.payload.size());
    std::array<uint8_t, kMaxNotificationSize> encoded = {};
    const size_t begin_size = encode_notification(
        begin, context, encoded.data(), encoded.size());
    assert(begin_size == kBeginVector.size());
    assert(std::equal(kBeginVector.begin(), kBeginVector.end(), encoded.begin()));

    VoiceNotification decoded_begin = {};
    assert(decode_notification(
        encoded.data(),
        begin_size,
        context,
        kTestEpoch,
        nullptr,
        decoded_begin));
    assert(decoded_begin.kind == NotificationKind::PttBegin);
    assert(decoded_begin.session == 7);
    assert(decoded_begin.sequence == 0);
    assert(decoded_begin.watch_ms == 100);
    assert(decoded_begin.session_nonce == kTestNonce);
    BeginBody decoded_body = {};
    assert(decode_begin_body(
        decoded_begin.payload.data(), decoded_begin.payload_size, decoded_body));
    assert(decoded_body.codec == kImaAdpcmCodec);
    assert(decoded_body.sample_rate == kSampleRate);
    assert(decoded_body.block_samples == kBlockSamples);

    VoiceNotification audio = {};
    audio.kind = NotificationKind::Audio;
    audio.session = 7;
    audio.sequence = 1;
    audio.watch_ms = 120;
    audio.key_epoch = kTestEpoch;
    audio.session_nonce = kTestNonce;
    audio.payload_size = kAudioPlaintextSize;
    for (size_t index = 0; index < audio.payload_size; ++index) {
        audio.payload[index] = static_cast<uint8_t>(index);
    }
    const size_t audio_size = encode_notification(
        audio, context, encoded.data(), encoded.size());
    assert(audio_size == kAudioCharacteristicValueSize);
    assert(audio_size == 190);
    assert(encode_notification(
               audio, context, encoded.data(), kAudioCharacteristicValueSize - 1) ==
           0);
    VoiceNotification decoded_audio = {};
    assert(decode_notification(
        encoded.data(),
        audio_size,
        context,
        kTestEpoch,
        &kTestNonce,
        decoded_audio));
    assert(decoded_audio.payload_size == kAudioPlaintextSize);
    assert(std::equal(
        audio.payload.begin(),
        audio.payload.begin() + static_cast<std::ptrdiff_t>(audio.payload_size),
        decoded_audio.payload.begin()));

    EndSummary summary = {};
    summary.generated_packets = 10;
    summary.accepted_packets = 9;
    summary.queue_overflows = 1;
    summary.encode_failures = 2;
    summary.captured_samples = 3200;
    summary.elapsed_ms = 200;
    summary.negotiated_mtu = 256;
    VoiceNotification end = {};
    end.kind = NotificationKind::PttEnd;
    end.session = 7;
    end.sequence = 2;
    end.watch_ms = 300;
    end.key_epoch = kTestEpoch;
    end.session_nonce = kTestNonce;
    end.payload_size = encode_end_summary(
        summary, end.payload.data(), end.payload.size());
    const size_t end_size = encode_notification(
        end, context, encoded.data(), encoded.size());
    assert(end_size == kNotificationHeaderSize + kEndBodySize + crypto::kTagSize);
    VoiceNotification decoded_end = {};
    assert(decode_notification(
        encoded.data(),
        end_size,
        context,
        kTestEpoch,
        &kTestNonce,
        decoded_end));
    EndSummary decoded_summary = {};
    assert(decode_end_summary(
        decoded_end.payload.data(), decoded_end.payload_size, decoded_summary));
    assert(decoded_summary.generated_packets == 10);
    assert(decoded_summary.accepted_packets == 9);
    assert(decoded_summary.queue_overflows == 1);
    assert(decoded_summary.encode_failures == 2);
    assert(decoded_summary.captured_samples == 3200);
    assert(decoded_summary.elapsed_ms == 200);
    assert(decoded_summary.negotiated_mtu == 256);

    VoiceNotification error = {};
    error.kind = NotificationKind::VoiceError;
    error.session = 7;
    error.sequence = 3;
    error.watch_ms = 310;
    error.key_epoch = kTestEpoch;
    error.session_nonce = kTestNonce;
    error.payload_size = encode_error_body(
        ErrorBody{4, 0x1234}, error.payload.data(), error.payload.size());
    const size_t error_size = encode_notification(
        error, context, encoded.data(), encoded.size());
    VoiceNotification decoded_error = {};
    assert(decode_notification(
        encoded.data(),
        error_size,
        context,
        kTestEpoch,
        &kTestNonce,
        decoded_error));
    ErrorBody decoded_error_body = {};
    assert(decode_error_body(
        decoded_error.payload.data(), decoded_error.payload_size, decoded_error_body));
    assert(decoded_error_body.code == 4);
    assert(decoded_error_body.detail == 0x1234);

    auto rejected = kReadyVector;
    rejected[0] = 2;
    assert(!decode_control(
        rejected.data(), rejected.size(), context, kTestEpoch, ready));
    rejected = kReadyVector;
    rejected[2] = 1;
    assert(!decode_control(
        rejected.data(), rejected.size(), context, kTestEpoch, ready));
    rejected = kReadyVector;
    rejected.back() ^= 1;
    assert(!decode_control(
        rejected.data(), rejected.size(), context, kTestEpoch, ready));

    auto tampered_begin = kBeginVector;
    for (const size_t index : {size_t{0}, size_t{2}, size_t{12}, size_t{18},
                               size_t{26},
                               tampered_begin.size() - 1}) {
        auto changed = tampered_begin;
        changed[index] ^= 1;
        VoiceNotification rejected_notification = {};
        assert(!decode_notification(
            changed.data(),
            changed.size(),
            context,
            kTestEpoch,
            nullptr,
            rejected_notification));
    }

    VoiceNotification invalid = begin;
    invalid.session = 0;
    assert(encode_notification(invalid, context, encoded.data(), encoded.size()) == 0);
    invalid = begin;
    invalid.sequence = 1;
    assert(encode_notification(invalid, context, encoded.data(), encoded.size()) == 0);
    invalid = begin;
    invalid.payload[0] = 2;
    assert(encode_notification(invalid, context, encoded.data(), encoded.size()) == 0);
    invalid = audio;
    invalid.payload_size = kAudioPlaintextSize - 1;
    assert(encode_notification(invalid, context, encoded.data(), encoded.size()) == 0);
    invalid = audio;
    invalid.sequence = 0;
    assert(encode_notification(invalid, context, encoded.data(), encoded.size()) == 0);
    invalid = audio;
    invalid.key_epoch = 0;
    assert(encode_notification(invalid, context, encoded.data(), encoded.size()) == 0);

    auto invalid_length = kBeginVector;
    invalid_length[16] = 20;
    assert(!decode_notification(
        invalid_length.data(),
        invalid_length.size(),
        context,
        kTestEpoch,
        nullptr,
        decoded_begin));

    std::array<uint8_t, kBeginVector.size()> authenticated_bad_codec = {};
    std::copy_n(kBeginVector.data(), kNotificationHeaderSize, authenticated_bad_codec.data());
    std::copy(
        kTestNonce.begin(),
        kTestNonce.end(),
        authenticated_bad_codec.begin() + kNotificationHeaderSize);
    const std::array<uint8_t, kBeginBodySize> invalid_codec = {
        2, 0x80, 0x3e, 0x40, 0x01,
    };
    size_t encrypted_body_size = 0;
    assert(context.encrypt(
        crypto::Domain::PttBegin,
        kTestNonce,
        0,
        authenticated_bad_codec.data(),
        kNotificationHeaderSize + crypto::kExchangeNonceSize,
        invalid_codec.data(),
        invalid_codec.size(),
        authenticated_bad_codec.data() + kNotificationHeaderSize +
            crypto::kExchangeNonceSize,
        authenticated_bad_codec.size() - kNotificationHeaderSize -
            crypto::kExchangeNonceSize,
        encrypted_body_size));
    assert(!decode_notification(
        authenticated_bad_codec.data(),
        authenticated_bad_codec.size(),
        context,
        kTestEpoch,
        nullptr,
        decoded_begin));

    const size_t second_audio_size = encode_notification(
        audio, context, encoded.data(), encoded.size());
    auto changed_audio = encoded;
    changed_audio[kNotificationHeaderSize] ^= 1;
    assert(!decode_notification(
        changed_audio.data(),
        second_audio_size,
        context,
        kTestEpoch,
        &kTestNonce,
        decoded_audio));
    auto wrong_session_nonce = kTestNonce;
    wrong_session_nonce[0] ^= 1;
    assert(!decode_notification(
        encoded.data(),
        second_audio_size,
        context,
        kTestEpoch,
        &wrong_session_nonce,
        decoded_audio));
}
