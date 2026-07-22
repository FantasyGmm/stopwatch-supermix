#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ble_voice_probe {

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kControlCommandSize = 14;
constexpr size_t kNotificationHeaderSize = 14;
constexpr size_t kStartAckSize = 8;
constexpr size_t kStopSummarySize = 30;
constexpr size_t kErrorPayloadSize = 4;
constexpr size_t kMaxNotificationSize = 256;
constexpr size_t kMaxNotificationPayload =
    kMaxNotificationSize - kNotificationHeaderSize;

enum class Command : uint8_t { Start = 1, Stop = 2 };
enum class Mode : uint8_t { Synthetic = 1, MicrophoneAdpcm = 2 };
enum class Kind : uint8_t { StartAck = 1, Data = 2, StopSummary = 3, Error = 4 };
enum class ErrorCode : uint16_t {
    InvalidCommand = 1,
    Busy = 2,
    NotSubscribed = 3,
    MtuTooSmall = 4,
    CaptureBusy = 5,
    RecorderOpenFailed = 6,
    ResamplerFailed = 7,
    EncoderFailed = 8,
    QueueCongested = 9,
    Disconnected = 10,
};

struct ControlCommand {
    Command command = Command::Stop;
    Mode mode = Mode::Synthetic;
    uint16_t session = 0;
    uint32_t target_bps = 0;
    uint32_t duration_ms = 0;
};

struct Notification {
    Kind kind = Kind::Error;
    uint16_t session = 0;
    uint32_t sequence = 0;
    uint32_t watch_ms = 0;
    uint16_t payload_length = 0;
    std::array<uint8_t, kMaxNotificationPayload> payload = {};
};

struct StopSummary {
    uint32_t generated_data_packets = 0;
    uint32_t accepted_data_packets = 0;
    uint32_t queue_overflows = 0;
    uint32_t encode_failures = 0;
    uint32_t elapsed_ms = 0;
    uint32_t generated_payload_bytes = 0;
    uint32_t captured_pcm_samples = 0;
    uint16_t negotiated_mtu = 23;
};

bool decode_control(const uint8_t* bytes, size_t length, ControlCommand& command);
size_t encode_notification(const Notification& packet, uint8_t* output, size_t capacity);
size_t encode_start_ack(uint16_t mtu, Mode mode, uint32_t target_bps,
                        uint8_t* output, size_t capacity);
size_t encode_stop_summary(const StopSummary& summary, uint8_t* output, size_t capacity);
size_t encode_error(ErrorCode code, uint16_t detail, uint8_t* output, size_t capacity);
void fill_synthetic_payload(uint32_t sequence, uint8_t* output, size_t length);

}  // namespace ble_voice_probe
