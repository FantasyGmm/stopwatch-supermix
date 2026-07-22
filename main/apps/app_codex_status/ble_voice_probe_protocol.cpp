#include "ble_voice_probe_protocol.h"

#include <algorithm>

namespace ble_voice_probe {
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

bool is_valid_mode(uint8_t value)
{
    return value == static_cast<uint8_t>(Mode::Synthetic) ||
           value == static_cast<uint8_t>(Mode::MicrophoneAdpcm);
}

}  // namespace

bool decode_control(const uint8_t* bytes, size_t length, ControlCommand& command)
{
    if (bytes == nullptr || length != kControlCommandSize ||
        bytes[0] != kProtocolVersion || bytes[3] != 0 ||
        (bytes[1] != static_cast<uint8_t>(Command::Start) &&
         bytes[1] != static_cast<uint8_t>(Command::Stop)) ||
        !is_valid_mode(bytes[2])) {
        return false;
    }

    const uint16_t session = read_u16(bytes + 4);
    const uint32_t target_bps = read_u32(bytes + 6);
    const uint32_t duration_ms = read_u32(bytes + 10);
    if (session == 0) {
        return false;
    }

    const Command parsed_command = static_cast<Command>(bytes[1]);
    const Mode parsed_mode = static_cast<Mode>(bytes[2]);
    if (parsed_command == Command::Stop) {
        if (target_bps != 0 || duration_ms != 0) {
            return false;
        }
    } else if (duration_ms < 1000 || duration_ms > 120000 ||
               (parsed_mode == Mode::Synthetic &&
                (target_bps < 1024 || target_bps > 32768)) ||
               (parsed_mode == Mode::MicrophoneAdpcm && target_bps != 8192)) {
        return false;
    }

    command.command = parsed_command;
    command.mode = parsed_mode;
    command.session = session;
    command.target_bps = target_bps;
    command.duration_ms = duration_ms;
    return true;
}

size_t encode_notification(const Notification& packet, uint8_t* output, size_t capacity)
{
    const size_t total = kNotificationHeaderSize + packet.payload_length;
    if (output == nullptr || packet.session == 0 ||
        packet.payload_length > packet.payload.size() ||
        total > capacity) {
        return 0;
    }
    output[0] = kProtocolVersion;
    output[1] = static_cast<uint8_t>(packet.kind);
    write_u16(output + 2, packet.session);
    write_u32(output + 4, packet.sequence);
    write_u32(output + 8, packet.watch_ms);
    write_u16(output + 12, packet.payload_length);
    std::copy_n(packet.payload.data(), packet.payload_length, output + kNotificationHeaderSize);
    return total;
}

size_t encode_start_ack(uint16_t mtu, Mode mode, uint32_t target_bps,
                        uint8_t* output, size_t capacity)
{
    if (output == nullptr || capacity < kStartAckSize) {
        return 0;
    }
    write_u16(output, mtu);
    output[2] = static_cast<uint8_t>(mode);
    output[3] = 0;
    write_u32(output + 4, target_bps);
    return kStartAckSize;
}

size_t encode_stop_summary(const StopSummary& summary, uint8_t* output, size_t capacity)
{
    if (output == nullptr || capacity < kStopSummarySize) {
        return 0;
    }
    write_u32(output, summary.generated_data_packets);
    write_u32(output + 4, summary.accepted_data_packets);
    write_u32(output + 8, summary.queue_overflows);
    write_u32(output + 12, summary.encode_failures);
    write_u32(output + 16, summary.elapsed_ms);
    write_u32(output + 20, summary.generated_payload_bytes);
    write_u32(output + 24, summary.captured_pcm_samples);
    write_u16(output + 28, summary.negotiated_mtu);
    return kStopSummarySize;
}

size_t encode_error(ErrorCode code, uint16_t detail, uint8_t* output, size_t capacity)
{
    if (output == nullptr || capacity < kErrorPayloadSize) {
        return 0;
    }
    write_u16(output, static_cast<uint16_t>(code));
    write_u16(output + 2, detail);
    return kErrorPayloadSize;
}

void fill_synthetic_payload(uint32_t sequence, uint8_t* output, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        output[index] = static_cast<uint8_t>((sequence + index) & 0xffU);
    }
}

}  // namespace ble_voice_probe
