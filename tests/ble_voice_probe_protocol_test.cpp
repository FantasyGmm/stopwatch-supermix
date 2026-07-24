#include "main/apps/app_codex_status/ble_voice_probe_protocol.h"

#include <array>
#include <cassert>

int main()
{
    using namespace ble_voice_probe;

    const std::array<uint8_t, kControlCommandSize> start = {
        1, 1, 1, 0, 0x34, 0x12,
        0x00, 0x20, 0x00, 0x00,
        0x60, 0xea, 0x00, 0x00,
    };
    ControlCommand command;
    assert(decode_control(start.data(), start.size(), command));
    assert(command.command == Command::Start);
    assert(command.mode == Mode::Synthetic);
    assert(command.session == 0x1234);
    assert(command.target_bps == 8192);
    assert(command.duration_ms == 60000);
    assert(!decode_control(start.data(), start.size() - 1, command));

    auto invalid = start;
    invalid[3] = 1;
    assert(!decode_control(invalid.data(), invalid.size(), command));
    invalid = start;
    invalid[1] = 3;
    assert(!decode_control(invalid.data(), invalid.size(), command));
    invalid = start;
    invalid[2] = 4;
    assert(!decode_control(invalid.data(), invalid.size(), command));
    invalid = start;
    invalid[4] = 0;
    invalid[5] = 0;
    assert(!decode_control(invalid.data(), invalid.size(), command));

    invalid = start;
    invalid[6] = 0xff;
    invalid[7] = 0x03;
    invalid[8] = 0;
    invalid[9] = 0;
    assert(!decode_control(invalid.data(), invalid.size(), command));
    invalid = start;
    invalid[6] = 0x01;
    invalid[7] = 0x80;
    invalid[8] = 0;
    invalid[9] = 0;
    assert(!decode_control(invalid.data(), invalid.size(), command));

    auto encrypted = start;
    encrypted[2] = static_cast<uint8_t>(Mode::MicrophoneAdpcmAesCcm);
    assert(decode_control(encrypted.data(), encrypted.size(), command));
    assert(command.mode == Mode::MicrophoneAdpcmAesCcm);

    invalid = start;
    invalid[2] = static_cast<uint8_t>(Mode::MicrophoneAdpcm);
    invalid[6] = 0x01;
    invalid[7] = 0x20;
    invalid[8] = 0;
    invalid[9] = 0;
    assert(!decode_control(invalid.data(), invalid.size(), command));

    invalid = start;
    invalid[10] = 0xe7;
    invalid[11] = 0x03;
    invalid[12] = 0;
    invalid[13] = 0;
    assert(!decode_control(invalid.data(), invalid.size(), command));
    invalid = start;
    invalid[10] = 0xc1;
    invalid[11] = 0xd4;
    invalid[12] = 0x01;
    invalid[13] = 0;
    assert(!decode_control(invalid.data(), invalid.size(), command));

    invalid = start;
    invalid[1] = static_cast<uint8_t>(Command::Stop);
    invalid[6] = 1;
    assert(!decode_control(invalid.data(), invalid.size(), command));
    invalid = start;
    invalid[1] = static_cast<uint8_t>(Command::Stop);
    invalid[10] = 1;
    assert(!decode_control(invalid.data(), invalid.size(), command));

    Notification packet;
    packet.kind = Kind::Data;
    packet.session = 0x1234;
    packet.sequence = 0x01020304;
    packet.watch_ms = 0x11223344;
    packet.payload_length = 3;
    fill_synthetic_payload(packet.sequence, packet.payload.data(), packet.payload_length);
    std::array<uint8_t, kMaxNotificationSize> encoded = {};
    const size_t length = encode_notification(packet, encoded.data(), encoded.size());
    assert(length == kNotificationHeaderSize + 3);
    assert(encoded[0] == 1 && encoded[1] == 2);
    assert(encoded[2] == 0x34 && encoded[3] == 0x12);
    assert(encoded[4] == 0x04 && encoded[7] == 0x01);
    assert(encoded[14] == 0x04 && encoded[15] == 0x05 && encoded[16] == 0x06);
    std::array<uint8_t, kNotificationHeaderSize> header = {};
    assert(encode_notification_header(packet, header.data(), header.size()) ==
           kNotificationHeaderSize);
    assert(std::equal(header.begin(), header.end(), encoded.begin()));
    packet.session = 0;
    assert(encode_notification(packet, encoded.data(), encoded.size()) == 0);
    packet.session = 0x1234;
    assert(encode_notification(packet, nullptr, encoded.size()) == 0);
    assert(encode_notification(packet, encoded.data(), length - 1) == 0);

    std::array<uint8_t, kStartAckSize> ack = {};
    assert(encode_start_ack(256, Mode::MicrophoneAdpcm, 8192, ack.data(), ack.size()) ==
           kStartAckSize);
    assert(ack[0] == 0x00 && ack[1] == 0x01);
    assert(ack[2] == 0x02 && ack[3] == 0x00);
    assert(ack[4] == 0x00 && ack[5] == 0x20 && ack[6] == 0x00 && ack[7] == 0x00);
    assert(encode_start_ack(256, Mode::Synthetic, 8192, nullptr, ack.size()) == 0);
    assert(encode_start_ack(256, Mode::Synthetic, 8192, ack.data(), ack.size() - 1) == 0);

    StopSummary summary;
    summary.generated_data_packets = 10;
    summary.accepted_data_packets = 9;
    summary.queue_overflows = 1;
    summary.encode_failures = 2;
    summary.elapsed_ms = 60000;
    summary.generated_payload_bytes = 8192;
    summary.captured_pcm_samples = 960000;
    summary.negotiated_mtu = 256;
    std::array<uint8_t, kStopSummarySize> summary_bytes = {};
    assert(encode_stop_summary(summary, summary_bytes.data(), summary_bytes.size()) ==
           kStopSummarySize);
    assert(summary_bytes[0] == 10 && summary_bytes[4] == 9);
    assert(summary_bytes[28] == 0x00 && summary_bytes[29] == 0x01);
    assert(encode_stop_summary(summary, nullptr, summary_bytes.size()) == 0);
    assert(encode_stop_summary(summary, summary_bytes.data(), summary_bytes.size() - 1) == 0);

    summary.crypto_packets = 10;
    summary.crypto_failures = 1;
    summary.crypto_total_us = 500;
    summary.crypto_max_us = 75;
    summary.crypto_p99_us = 70;
    std::array<uint8_t, kEncryptedStopSummarySize> encrypted_summary_bytes = {};
    assert(encode_stop_summary(
               summary,
               Mode::MicrophoneAdpcmAesCcm,
               encrypted_summary_bytes.data(),
               encrypted_summary_bytes.size()) == kEncryptedStopSummarySize);
    assert(std::equal(
        summary_bytes.begin(), summary_bytes.end(), encrypted_summary_bytes.begin()));
    assert(encrypted_summary_bytes[30] == 10);
    assert(encrypted_summary_bytes[34] == 1);
    assert(encrypted_summary_bytes[38] == 0xf4);
    assert(encrypted_summary_bytes[39] == 0x01);
    assert(encode_stop_summary(
               summary,
               Mode::MicrophoneAdpcmAesCcm,
               encrypted_summary_bytes.data(),
               encrypted_summary_bytes.size() - 1) == 0);

    std::array<uint8_t, kErrorPayloadSize> error = {};
    assert(encode_error(ErrorCode::Busy, 0x1234, error.data(), error.size()) ==
           kErrorPayloadSize);
    assert(error[0] == 0x02 && error[1] == 0x00);
    assert(error[2] == 0x34 && error[3] == 0x12);
    assert(encode_error(ErrorCode::Busy, 0x1234, nullptr, error.size()) == 0);
    assert(encode_error(ErrorCode::Busy, 0x1234, error.data(), error.size() - 1) == 0);
}
