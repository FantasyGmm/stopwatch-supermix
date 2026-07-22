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
}
