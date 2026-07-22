# BLE Voice Link Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and physically validate an isolated encrypted BLE path that streams deterministic traffic and 16 kHz IMA-ADPCM watch microphone audio to a standalone Mac probe client.

**Architecture:** Extend the existing Codex BLE peripheral service with one encrypted control-write characteristic and one encrypted notification characteristic. A bounded firmware controller produces packets on a worker task, while the Codex app drains the queue and calls NimBLE; a separate Bleak CLI verifies sequence, payload, timing, sender counters, and microphone WAV quality without modifying the installed menu Agent or production button behavior.

**Tech Stack:** C++17, ESP-IDF 5.5.4, NimBLE GATT, FreeRTOS queues/tasks, StopWatch audio backend, Espressif `esp_ae_rate_cvt`, Python 3 `unittest`, Bleak, standard-library JSON/WAV tooling.

---

## Scope And Safety

This plan implements only the BLE feasibility probe approved in `docs/superpowers/specs/2026-07-23-ble-voice-link-probe-design.md`.

- Do not modify Typeless, keyboard event generation, BlackHole, Wi-Fi audio, or the production right-button interaction.
- Do not reinstall or modify the menu bar Agent. Quit `StopWatch Codex Agent` before the standalone probe owns BLE.
- Keep the current Mac-to-watch status characteristic intact.
- Keep raw device evidence under `/tmp/stopwatch-ble-voice-probe/`; do not add it to Git.
- The worktree already contains unrelated modified, deleted, and untracked files. Every commit command below stages only the named probe files.
- Use the verified 16 MiB backup and repository flash guard before any device write.

## File Map

### Firmware protocol and logic

- Create `main/apps/app_codex_status/ble_voice_probe_protocol.h`: wire constants, enums, decoded command/header/summary types, packet serialization, deterministic synthetic payload.
- Create `main/apps/app_codex_status/ble_voice_probe_protocol.cpp`: little-endian codecs and input validation with no ESP-IDF dependency.
- Create `main/apps/app_codex_status/ble_voice_probe_session.h`: pure pacing and session-accounting model.
- Create `main/apps/app_codex_status/ble_voice_probe_session.cpp`: elapsed-time byte budget, sequence allocation, counters, and summary construction.
- Create `main/apps/app_codex_status/ima_adpcm_codec.h`: self-contained IMA-ADPCM block encoder interface.
- Create `main/apps/app_codex_status/ima_adpcm_codec.cpp`: 16-bit PCM to 4-bit IMA-ADPCM encoder.
- Create `main/apps/app_codex_status/ble_voice_probe_controller.h`: bounded producer lifecycle and app-facing queue API.
- Create `main/apps/app_codex_status/ble_voice_probe_controller.cpp`: synthetic producer, microphone producer, resampler, resource ownership, stop/error cleanup.

### Existing firmware integration

- Modify `main/apps/app_codex_status/codex_ble_transport.h`: voice-control queue, subscription/MTU state, and notification API.
- Modify `main/apps/app_codex_status/codex_ble_transport.cpp`: two new UUIDs, encrypted permissions, MTU/subscription events, and custom notifications.
- Modify `main/apps/app_codex_status/app_codex_status.h`: own the probe controller.
- Modify `main/apps/app_codex_status/app_codex_status.cpp`: route BLE control commands and drain probe packets; do not attach any key event.
- Modify `scripts/run_unified_offline_validation.sh`: compile and run the three new C++ host tests.

`main/CMakeLists.txt` already uses `file(GLOB_RECURSE)` for `main/apps/*.cpp`, so no CMake source-list edit is needed.

### Host tests and Mac probe

- Create `tests/ble_voice_probe_protocol_test.cpp`: wire layout, malformed input, deterministic payload.
- Create `tests/ble_voice_probe_session_test.cpp`: pacing, sequence, sender accounting, wrap-safe elapsed time.
- Create `tests/ima_adpcm_codec_test.cpp`: block header, known zero/step vectors, bounds.
- Create `mac-codex-agent/voice_probe_protocol.py`: matching wire codec and IMA-ADPCM decoder.
- Create `mac-codex-agent/voice_probe_metrics.py`: packet accounting, timing metrics, WAV duration comparison, acceptance decisions.
- Create `mac-codex-agent/ble_voice_probe.py`: standalone Bleak CLI, partial-report preservation, WAV writing, and exit status.
- Create `mac-codex-agent/tests/test_voice_probe_protocol.py`: Python wire/codec tests.
- Create `mac-codex-agent/tests/test_voice_probe_metrics.py`: threshold and decision-rule tests.
- Create `mac-codex-agent/tests/test_ble_voice_probe.py`: fake-Bleak lifecycle and CLI report tests.
- Create `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`: encryption, bounded queue, cleanup, and isolation contracts.
- Create `docs/ble-voice-link-probe.md`: operator commands and result interpretation.

## Fixed Wire Contract

Use these values in both languages; changing them requires updating both protocol test suites in the same commit.

```text
Service UUID:       7a230001-5474-4f50-5741-544348434f44
Status RX UUID:     7a230002-5474-4f50-5741-544348434f44 (unchanged)
Voice Control UUID: 7a230003-5474-4f50-5741-544348434f44
Voice TX UUID:      7a230004-5474-4f50-5741-544348434f44

Control command: <BBBBHII, 14 bytes
  version, command, mode, reserved, session, target_bps, duration_ms

Notification header: <BBHIIH, 14 bytes
  version, kind, session, sequence, watch_ms, payload_length

Start acknowledgement payload: <HBBI, 8 bytes
  negotiated_mtu, mode, reserved, target_bps

Stop summary payload: <IIIIIIIH, 30 bytes
  generated_data_packets, accepted_data_packets, queue_overflows,
  encode_failures, elapsed_ms, generated_payload_bytes,
  captured_pcm_samples, negotiated_mtu

Error payload: <HH, 4 bytes
  error_code, detail
```

Enums:

```text
Command: start=1, stop=2
Mode: synthetic=1, microphone_adpcm=2
Kind: start_ack=1, data=2, stop_summary=3, error=4
Error: invalid_command=1, busy=2, not_subscribed=3, mtu_too_small=4,
       capture_busy=5, recorder_open_failed=6, resampler_failed=7,
       encoder_failed=8, queue_congested=9, disconnected=10
```

The sequence covers every notification in a session: start acknowledgement is sequence `0`, data starts at `1`, and the stop summary consumes the final sequence. Synthetic payload byte `i` is `(packet_sequence + i) & 0xff`.

### Task 1: Implement The Shared Firmware Wire Protocol

**Files:**
- Create: `main/apps/app_codex_status/ble_voice_probe_protocol.h`
- Create: `main/apps/app_codex_status/ble_voice_probe_protocol.cpp`
- Test: `tests/ble_voice_probe_protocol_test.cpp`

- [ ] **Step 1: Write the failing protocol test**

Create `tests/ble_voice_probe_protocol_test.cpp` with exact-size and little-endian assertions:

```cpp
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
```

- [ ] **Step 2: Run the test and verify the missing header failure**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ble_voice_probe_protocol_test.cpp \
  main/apps/app_codex_status/ble_voice_probe_protocol.cpp \
  -o /tmp/ble_voice_probe_protocol_test
```

Expected: compilation fails because `ble_voice_probe_protocol.h` and `.cpp` do not exist.

- [ ] **Step 3: Add the protocol declarations**

Create `main/apps/app_codex_status/ble_voice_probe_protocol.h` with this public contract:

```cpp
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
```

- [ ] **Step 4: Implement strict little-endian serialization**

Create `main/apps/app_codex_status/ble_voice_probe_protocol.cpp`. Use local `read_u16`, `read_u32`, `write_u16`, and `write_u32` helpers. `decode_control` must reject a nonzero reserved byte, unknown enum values, session `0`, synthetic targets outside `1024..32768`, durations outside `1000..120000`, and a microphone start whose target is not `8192`. A stop command is valid only when target and duration are both zero.

The notification encoder must start with:

```cpp
size_t encode_notification(const Notification& packet, uint8_t* output, size_t capacity)
{
    const size_t total = kNotificationHeaderSize + packet.payload_length;
    if (output == nullptr || packet.payload_length > packet.payload.size() ||
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
```

Implement `fill_synthetic_payload` exactly as:

```cpp
void fill_synthetic_payload(uint32_t sequence, uint8_t* output, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        output[index] = static_cast<uint8_t>((sequence + index) & 0xffU);
    }
}
```

- [ ] **Step 5: Run the focused test**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ble_voice_probe_protocol_test.cpp \
  main/apps/app_codex_status/ble_voice_probe_protocol.cpp \
  -o /tmp/ble_voice_probe_protocol_test
/tmp/ble_voice_probe_protocol_test
```

Expected: exit `0` with no output.

- [ ] **Step 6: Commit the protocol unit**

```bash
git add \
  main/apps/app_codex_status/ble_voice_probe_protocol.h \
  main/apps/app_codex_status/ble_voice_probe_protocol.cpp \
  tests/ble_voice_probe_protocol_test.cpp
git commit -m "Add BLE voice probe protocol"
```

### Task 2: Add Pure Session Pacing And Sender Accounting

**Files:**
- Create: `main/apps/app_codex_status/ble_voice_probe_session.h`
- Create: `main/apps/app_codex_status/ble_voice_probe_session.cpp`
- Test: `tests/ble_voice_probe_session_test.cpp`

- [ ] **Step 1: Write the failing pacing and counter test**

Create `tests/ble_voice_probe_session_test.cpp`:

```cpp
#include "main/apps/app_codex_status/ble_voice_probe_session.h"

#include <cassert>
#include <cstdint>

int main()
{
    using namespace ble_voice_probe;

    Session session;
    ControlCommand command;
    command.command = Command::Start;
    command.mode = Mode::Synthetic;
    command.session = 7;
    command.target_bps = 8192;
    command.duration_ms = 60000;
    assert(session.start(command, 1000, 256));
    assert(session.allocate_sequence() == 0);
    assert(session.allocate_sequence() == 1);
    assert(session.synthetic_bytes_due(1000) == 0);
    assert(session.synthetic_bytes_due(1100) == 819);
    session.record_generated(200);
    assert(session.synthetic_bytes_due(1100) == 619);
    session.record_notify_accepted();
    session.record_queue_overflow();
    session.record_encode_failure();
    session.record_captured_samples(320);
    assert(!session.duration_reached(60999));
    assert(session.duration_reached(61000));

    StopSummary summary = session.summary(61000);
    assert(summary.generated_data_packets == 1);
    assert(summary.accepted_data_packets == 1);
    assert(summary.queue_overflows == 1);
    assert(summary.encode_failures == 1);
    assert(summary.elapsed_ms == 60000);
    assert(summary.generated_payload_bytes == 200);
    assert(summary.captured_pcm_samples == 320);
    assert(summary.negotiated_mtu == 256);

    Session wrapped;
    assert(wrapped.start(command, UINT32_MAX - 20U, 185));
    assert(wrapped.synthetic_bytes_due(79) == 819);
}
```

- [ ] **Step 2: Verify the test fails before implementation**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ble_voice_probe_session_test.cpp \
  main/apps/app_codex_status/ble_voice_probe_session.cpp \
  -o /tmp/ble_voice_probe_session_test
```

Expected: compilation fails because the session files do not exist.

- [ ] **Step 3: Implement the pure session model**

Expose this interface from `ble_voice_probe_session.h`:

```cpp
#pragma once

#include "ble_voice_probe_protocol.h"

#include <cstdint>

namespace ble_voice_probe {

class Session {
public:
    bool start(const ControlCommand& command, uint32_t now_ms, uint16_t negotiated_mtu);
    void reset();
    bool active() const;
    uint16_t id() const;
    Mode mode() const;
    uint32_t target_bps() const;
    uint32_t allocate_sequence();
    uint32_t synthetic_bytes_due(uint32_t now_ms) const;
    bool duration_reached(uint32_t now_ms) const;
    void record_generated(uint16_t payload_bytes);
    void record_notify_accepted();
    void record_queue_overflow();
    void record_encode_failure();
    void record_captured_samples(uint32_t samples);
    StopSummary summary(uint32_t now_ms) const;

private:
    ControlCommand _command = {};
    uint32_t _started_ms = 0;
    uint32_t _next_sequence = 0;
    StopSummary _summary = {};
    bool _active = false;
};

}  // namespace ble_voice_probe
```

In `ble_voice_probe_session.cpp`, compute elapsed time with unsigned subtraction and compute due bytes in 64 bits:

```cpp
uint32_t Session::synthetic_bytes_due(uint32_t now_ms) const
{
    if (!_active || _command.mode != Mode::Synthetic) {
        return 0;
    }
    const uint64_t elapsed = static_cast<uint32_t>(now_ms - _started_ms);
    const uint64_t expected = elapsed * _command.target_bps / 1000U;
    return expected > _summary.generated_payload_bytes
               ? static_cast<uint32_t>(expected - _summary.generated_payload_bytes)
               : 0;
}
```

`record_generated` increments both packet and payload counters. `record_notify_accepted` counts data notifications only. `summary` returns a copy with `elapsed_ms` updated from `now_ms - _started_ms`.

- [ ] **Step 4: Run protocol and session tests**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ble_voice_probe_session_test.cpp \
  main/apps/app_codex_status/ble_voice_probe_session.cpp \
  -o /tmp/ble_voice_probe_session_test
/tmp/ble_voice_probe_session_test
/tmp/ble_voice_probe_protocol_test
```

Expected: both commands exit `0`.

- [ ] **Step 5: Commit the session model**

```bash
git add \
  main/apps/app_codex_status/ble_voice_probe_session.h \
  main/apps/app_codex_status/ble_voice_probe_session.cpp \
  tests/ble_voice_probe_session_test.cpp
git commit -m "Add BLE voice probe session accounting"
```

### Task 3: Add The Self-Contained IMA-ADPCM Block Encoder

**Files:**
- Create: `main/apps/app_codex_status/ima_adpcm_codec.h`
- Create: `main/apps/app_codex_status/ima_adpcm_codec.cpp`
- Test: `tests/ima_adpcm_codec_test.cpp`

- [ ] **Step 1: Write known-vector and bounds tests**

Create `tests/ima_adpcm_codec_test.cpp`:

```cpp
#include "main/apps/app_codex_status/ima_adpcm_codec.h"

#include <array>
#include <cassert>

int main()
{
    using namespace ble_voice_probe;

    const std::array<int16_t, 5> zero = {0, 0, 0, 0, 0};
    std::array<uint8_t, 16> encoded = {};
    size_t length = encode_ima_adpcm_block(
        zero.data(), zero.size(), encoded.data(), encoded.size());
    assert(length == 6);
    assert(encoded[0] == 0 && encoded[1] == 0);
    assert(encoded[2] == 0 && encoded[3] == 0);
    assert(encoded[4] == 0 && encoded[5] == 0);

    const std::array<int16_t, 2> step = {0, 7};
    length = encode_ima_adpcm_block(
        step.data(), step.size(), encoded.data(), encoded.size());
    assert(length == 5);
    assert(encoded[4] == 0x04);

    assert(ima_adpcm_block_size(320) == 164);
    assert(encode_ima_adpcm_block(nullptr, 320, encoded.data(), encoded.size()) == 0);
    assert(encode_ima_adpcm_block(step.data(), step.size(), encoded.data(), 4) == 0);
}
```

- [ ] **Step 2: Verify the missing codec failure**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ima_adpcm_codec_test.cpp \
  main/apps/app_codex_status/ima_adpcm_codec.cpp \
  -o /tmp/ima_adpcm_codec_test
```

Expected: compilation fails because the codec files do not exist.

- [ ] **Step 3: Implement standard IMA-ADPCM block encoding**

Declare:

```cpp
namespace ble_voice_probe {
size_t ima_adpcm_block_size(size_t sample_count);
size_t encode_ima_adpcm_block(const int16_t* samples, size_t sample_count,
                              uint8_t* output, size_t capacity);
}
```

Each block is independently decodable:

- bytes `0..1`: first PCM sample, signed little-endian predictor;
- byte `2`: initial IMA index, always `0`;
- byte `3`: reserved, always `0`;
- remaining samples: low nibble first, then high nibble.

Use the standard 89-entry step table and 16-entry index table:

```cpp
constexpr int kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

constexpr int kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
};
```

Clamp predictor to `[-32768, 32767]` and index to `[0, 88]` after every nibble. A 320-sample block must produce 164 bytes.

- [ ] **Step 4: Run the codec test**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ima_adpcm_codec_test.cpp \
  main/apps/app_codex_status/ima_adpcm_codec.cpp \
  -o /tmp/ima_adpcm_codec_test
/tmp/ima_adpcm_codec_test
```

Expected: exit `0`.

- [ ] **Step 5: Commit the codec**

```bash
git add \
  main/apps/app_codex_status/ima_adpcm_codec.h \
  main/apps/app_codex_status/ima_adpcm_codec.cpp \
  tests/ima_adpcm_codec_test.cpp
git commit -m "Add IMA ADPCM probe codec"
```

### Task 4: Mirror The Protocol And Codec On macOS

**Files:**
- Create: `mac-codex-agent/voice_probe_protocol.py`
- Test: `mac-codex-agent/tests/test_voice_probe_protocol.py`

- [ ] **Step 1: Write Python wire and decode tests**

Create `mac-codex-agent/tests/test_voice_probe_protocol.py` with:

```python
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_probe_protocol import (
    DATA,
    MICROPHONE_ADPCM,
    SYNTHETIC,
    decode_ima_adpcm_block,
    decode_notification,
    encode_start,
    encode_stop,
    expected_synthetic_payload,
)


class VoiceProbeProtocolTests(unittest.TestCase):
    def test_control_commands_match_firmware_layout(self):
        self.assertEqual(
            encode_start(0x1234, SYNTHETIC, 8192, 60000),
            bytes.fromhex("0101010034120020000060ea0000"),
        )
        self.assertEqual(
            encode_stop(0x1234),
            bytes.fromhex("0102010034120000000000000000"),
        )

    def test_decodes_notification_and_rejects_bad_payload_length(self):
        raw = struct.pack("<BBHIIH", 1, DATA, 7, 3, 100, 3) + b"abc"
        packet = decode_notification(raw)
        self.assertEqual((packet.session, packet.sequence, packet.payload), (7, 3, b"abc"))
        with self.assertRaises(ValueError):
            decode_notification(raw[:-1])

    def test_synthetic_pattern_uses_packet_sequence(self):
        self.assertEqual(expected_synthetic_payload(0x102, 4), b"\x02\x03\x04\x05")

    def test_decodes_known_ima_blocks(self):
        self.assertEqual(
            decode_ima_adpcm_block(b"\x00\x00\x00\x00\x00\x00", sample_count=5),
            [0] * 5,
        )
        self.assertEqual(
            decode_ima_adpcm_block(b"\x00\x00\x00\x00\x04", sample_count=2),
            [0, 7],
        )


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Verify the import failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_protocol.py -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'voice_probe_protocol'`.

- [ ] **Step 3: Implement the Python protocol module**

Use `struct.Struct` definitions, immutable dataclasses, and no third-party codec package:

```python
PROTOCOL_VERSION = 1
START = 1
STOP = 2
SYNTHETIC = 1
MICROPHONE_ADPCM = 2
START_ACK = 1
DATA = 2
STOP_SUMMARY = 3
ERROR = 4

CONTROL = struct.Struct("<BBBBHII")
HEADER = struct.Struct("<BBHIIH")
START_ACK_PAYLOAD = struct.Struct("<HBBI")
STOP_SUMMARY_PAYLOAD = struct.Struct("<IIIIIIIH")
ERROR_PAYLOAD = struct.Struct("<HH")

@dataclasses.dataclass(frozen=True)
class Notification:
    kind: int
    session: int
    sequence: int
    watch_ms: int
    payload: bytes

@dataclasses.dataclass(frozen=True)
class StartAck:
    negotiated_mtu: int
    mode: int
    target_bps: int

@dataclasses.dataclass(frozen=True)
class StopSummary:
    generated_data_packets: int
    accepted_data_packets: int
    queue_overflows: int
    encode_failures: int
    elapsed_ms: int
    generated_payload_bytes: int
    captured_pcm_samples: int
    negotiated_mtu: int

    def pack(self) -> bytes:
        return STOP_SUMMARY_PAYLOAD.pack(
            self.generated_data_packets,
            self.accepted_data_packets,
            self.queue_overflows,
            self.encode_failures,
            self.elapsed_ms,
            self.generated_payload_bytes,
            self.captured_pcm_samples,
            self.negotiated_mtu,
        )

    @classmethod
    def from_payload(cls, payload: bytes) -> "StopSummary":
        if len(payload) != STOP_SUMMARY_PAYLOAD.size:
            raise ValueError("invalid stop summary size")
        return cls(*STOP_SUMMARY_PAYLOAD.unpack(payload))

def encode_start(session: int, mode: int, target_bps: int, duration_ms: int) -> bytes:
    if not 1 <= session <= 0xffff:
        raise ValueError("session must be 1..65535")
    return CONTROL.pack(PROTOCOL_VERSION, START, mode, 0, session, target_bps, duration_ms)

def encode_stop(session: int) -> bytes:
    return CONTROL.pack(PROTOCOL_VERSION, STOP, SYNTHETIC, 0, session, 0, 0)

def pack_notification(
    kind: int,
    session: int,
    sequence: int,
    watch_ms: int,
    payload: bytes,
) -> bytes:
    return HEADER.pack(
        PROTOCOL_VERSION,
        kind,
        session,
        sequence,
        watch_ms,
        len(payload),
    ) + payload
```

`decode_notification` must require exactly `HEADER.size + payload_length` bytes and reject unknown version/kind. Add `decode_start_ack` and `decode_error` functions with exact payload-size checks; use `StopSummary.from_payload` for stop summaries. Implement `decode_ima_adpcm_block(payload, sample_count)` with the same step/index tables as Task 3, low nibble first, predictor/index clamping after each sample, and an exact encoded-size check. Production microphone packets always pass `sample_count=320`, which prevents the unused high nibble in an odd-length block from becoming a false sample.

- [ ] **Step 4: Run the Python protocol test**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_protocol.py -v
```

Expected: four tests pass.

- [ ] **Step 5: Commit the Mac protocol**

```bash
git add \
  mac-codex-agent/voice_probe_protocol.py \
  mac-codex-agent/tests/test_voice_probe_protocol.py
git commit -m "Add Mac BLE voice probe protocol"
```

### Task 5: Implement Metrics And Acceptance Decisions

**Files:**
- Create: `mac-codex-agent/voice_probe_metrics.py`
- Test: `mac-codex-agent/tests/test_voice_probe_metrics.py`

- [ ] **Step 1: Write failing transport and decision tests**

Create `mac-codex-agent/tests/test_voice_probe_metrics.py` with `unittest` cases that build `Notification` objects directly:

```python
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_probe_metrics import ProbeMetrics
from voice_probe_protocol import (
    DATA,
    MICROPHONE_ADPCM,
    SYNTHETIC,
    Notification,
    StopSummary,
    expected_synthetic_payload,
)


class VoiceProbeMetricsTests(unittest.TestCase):
    def test_clean_required_profile_passes_at_requested_rate(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        sizes = [234] * 34 + [236]
        for index, size in enumerate(sizes):
            sequence = index + 1
            timestamp_ms = round(index * 1000 / len(sizes))
            metrics.observe(
                Notification(
                    DATA,
                    9,
                    sequence,
                    timestamp_ms,
                    expected_synthetic_payload(sequence, size),
                ),
                arrival_ns=timestamp_ms * 1_000_000,
            )
        result = metrics.finish(
            summary=StopSummary(35, 35, 0, 0, 1000, 8192, 0, 256),
            disconnected=False,
        )
        self.assertTrue(result["automated_pass"])
        self.assertEqual(result["sequence_gaps"], 0)
        self.assertEqual(result["payload_corruptions"], 0)
        self.assertEqual(result["throughput_error_percent"], 0.0)

    def test_loss_corruption_congestion_and_timing_fail(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 1000, b"bad"),
            arrival_ns=1_000_000_000,
        )
        metrics.observe(
            Notification(DATA, 9, 3, 1020, expected_synthetic_payload(3, 4)),
            arrival_ns=1_150_000_000,
        )
        result = metrics.finish(
            summary=StopSummary(3, 2, 1, 0, 40, 7, 0, 256),
            disconnected=False,
        )
        self.assertFalse(result["automated_pass"])
        self.assertEqual(result["sequence_gaps"], 1)
        self.assertEqual(result["payload_corruptions"], 1)
        self.assertEqual(result["max_arrival_gap_ms"], 150.0)

    def test_microphone_duration_uses_decoded_samples(self):
        metrics = ProbeMetrics(session=9, mode=MICROPHONE_ADPCM, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 1000, bytes(164)),
            arrival_ns=1_000_000_000,
        )
        result = metrics.finish(
            summary=StopSummary(1, 1, 0, 0, 2000, 164, 32000, 256),
            disconnected=False,
            decoded_pcm_samples=32000,
        )
        self.assertEqual(result["wav_duration_error_percent"], 0.0)
        self.assertTrue(result["manual_audio_review_required"])


if __name__ == "__main__":
    unittest.main()
```

Also test a `41.0 ms` p95 deviation fails, a `101.0 ms` steady-state gap fails, and a failed `32768 B/s` profile is marked `exploratory` without changing the required-profile aggregate.

- [ ] **Step 2: Verify the module is missing**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_metrics.py -v
```

Expected: import failure for `voice_probe_metrics`.

- [ ] **Step 3: Implement metrics with explicit definitions**

Implement `ProbeMetrics.observe` and `finish` with these definitions:

- sequence loss is the sum of positive gaps between consecutive data sequence values;
- regression or duplicate sequence is a separate hard failure;
- synthetic corruption compares each payload with `expected_synthetic_payload(sequence, len(payload))`;
- inter-arrival deviation is `abs(mac_arrival_delta_ms - watch_delta_ms)` for consecutive data packets;
- p95 uses nearest-rank index `ceil(0.95 * count) - 1` over sorted deviations;
- maximum arrival gap uses consecutive data packets only;
- duration error is `abs(decoded_pcm_samples - captured_pcm_samples) / captured_pcm_samples * 100`;
- synthetic throughput is `generated_payload_bytes * 1000 / elapsed_ms` and throughput error is relative to `target_bps`;
- required automatic thresholds are zero loss/corruption/regression/overflow/encode failure, sender generated equals sender accepted equals receiver data count, synthetic throughput error `<= 1%`, p95 `<= 40 ms`, max gap `<= 100 ms`, no disconnect, and microphone duration error `<= 1%`;
- `32768 B/s` synthetic is exploratory; its result is still reported but does not reject required profiles.

Return a JSON-serializable dictionary containing all raw counters, calculated values, failure reasons, `automated_pass`, `required_profile`, and `manual_audio_review_required`.

- [ ] **Step 4: Run metrics and protocol tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_protocol.py \
  mac-codex-agent/tests/test_voice_probe_metrics.py -v
```

Expected: all tests pass.

- [ ] **Step 5: Commit metrics**

```bash
git add \
  mac-codex-agent/voice_probe_metrics.py \
  mac-codex-agent/tests/test_voice_probe_metrics.py
git commit -m "Add BLE voice probe acceptance metrics"
```

### Task 6: Add Encrypted GATT Control And Notification Plumbing

**Files:**
- Modify: `main/apps/app_codex_status/codex_ble_transport.h`
- Modify: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Create: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`

- [ ] **Step 1: Write the failing firmware contract test**

The test must read only the probe integration files. Start the file with:

```python
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
```

Then add this test class:

```python
class BleVoiceProbeFirmwareContractTests(unittest.TestCase):
    def test_gatt_has_isolated_encrypted_voice_characteristics(self):
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("kVoiceControlUuid", source)
        self.assertIn("kVoiceTxUuid", source)
        self.assertIn("BLE_GATT_CHR_F_WRITE_ENC", source)
        self.assertIn("BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC", source)
        self.assertIn("BLE_GAP_EVENT_SUBSCRIBE", source)
        self.assertIn("BLE_GAP_EVENT_MTU", source)
        self.assertIn("ble_gatts_notify_custom", source)

    def test_transport_bounds_control_queue_and_mtu_payload(self):
        header = (ROOT / "main/apps/app_codex_status/codex_ble_transport.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/codex_ble_transport.cpp").read_text()
        self.assertIn("take_voice_control", header)
        self.assertIn("voice_notification_limit", header)
        self.assertIn("kVoiceControlQueueDepth", source)
        self.assertIn("event->mtu.value", source)
        self.assertIn("negotiated_mtu - 3", source)
```

- [ ] **Step 2: Run and verify the contract fails**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
```

Expected: failures for missing UUIDs and APIs.

- [ ] **Step 3: Extend the transport interface**

Add these types and methods to `codex_ble_transport.h`:

```cpp
struct CodexBleVoiceControl {
    uint16_t length = 0;
    std::array<uint8_t, ble_voice_probe::kControlCommandSize> data = {};
};

enum class CodexBleNotifyResult { Accepted, NotReady, Congested, InvalidSize };

bool take_voice_control(CodexBleVoiceControl& control);
bool voice_subscribed() const;
uint16_t negotiated_mtu() const;
size_t voice_notification_limit() const;
CodexBleNotifyResult notify_voice(const uint8_t* data, size_t length);
void handle_voice_subscription(uint16_t attr_handle, bool enabled);
void handle_mtu_update(uint16_t connection_handle, uint16_t mtu);
```

Add `_voice_control_queue`, `_voice_subscribed`, and `_negotiated_mtu{23}` members. Reset them on start, disconnect, and stop.

- [ ] **Step 4: Register secure characteristics and events**

In `codex_ble_transport.cpp`:

```cpp
constexpr size_t kVoiceControlQueueDepth = 8;

const ble_uuid128_t kVoiceControlUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x03, 0x00, 0x23, 0x7a);
const ble_uuid128_t kVoiceTxUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x04, 0x00, 0x23, 0x7a);
uint16_t g_voice_tx_value_handle = 0;
```

Register control with `BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC`. Register voice TX with `BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC`, and pass `&g_voice_tx_value_handle` through `val_handle`. The global handle is required because the static GATT definition is created before any `CodexBleTransport` instance; reset it before service registration and after NimBLE deinitialization.

Handle only the voice handle in subscription events:

```cpp
case BLE_GAP_EVENT_SUBSCRIBE:
    g_transport->handle_voice_subscription(
        event->subscribe.attr_handle,
        event->subscribe.cur_notify != 0);
    return 0;
case BLE_GAP_EVENT_MTU:
    g_transport->handle_mtu_update(event->mtu.conn_handle, event->mtu.value);
    return 0;
```

`voice_notification_limit()` returns `min(kMaxNotificationSize, negotiated_mtu - 3)` and returns `0` when MTU is `<= 3`. `notify_voice` rejects a disconnected, unsubscribed, zero `g_voice_tx_value_handle`, null, empty, or oversize send before allocating an mbuf. Map `BLE_HS_ENOMEM` and `BLE_HS_EBUSY` to `Congested`; map `0` to `Accepted`.

- [ ] **Step 5: Re-run the contract and existing Agent tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_protocol.py -v
```

Expected: all tests pass and the old status framing tests remain unchanged.

- [ ] **Step 6: Commit BLE transport plumbing**

```bash
git add \
  main/apps/app_codex_status/codex_ble_transport.h \
  main/apps/app_codex_status/codex_ble_transport.cpp \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py
git commit -m "Add encrypted BLE voice probe characteristics"
```

### Task 7: Add The Bounded Synthetic Producer And App Routing

**Files:**
- Create: `main/apps/app_codex_status/ble_voice_probe_controller.h`
- Create: `main/apps/app_codex_status/ble_voice_probe_controller.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`

- [ ] **Step 1: Extend the contract for isolation and cleanup**

Add a complete isolation test:

```python
def test_controller_is_bounded_and_not_attached_to_a_button(self):
    controller = (
        ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
    ).read_text()
    app = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()
    self.assertIn("xQueueCreate(kVoicePacketQueueDepth", controller)
    self.assertIn("kMaxConsecutiveQueueFailures", controller)
    self.assertIn("fill_synthetic_payload", controller)
    self.assertIn("take_voice_control", app)
    self.assertIn("notify_voice", app)
    self.assertIn("handle_disconnect", app)
    self.assertIn("shutdown", app)
    self.assertNotIn("Typeless", controller)
    self.assertNotIn("KeyEvent::Right", controller)
    self.assertNotIn("start_session(key_event", app)
```

- [ ] **Step 2: Run the contract and verify the new assertions fail**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
```

Expected: failure because controller files and app routing are absent.

- [ ] **Step 3: Define the controller API and bounded packet queue**

Use this app-facing interface:

```cpp
class BleVoiceProbeController {
public:
    BleVoiceProbeController();
    ~BleVoiceProbeController();
    bool initialize();
    void handle_control(const CodexBleVoiceControl& frame, bool subscribed,
                        uint16_t negotiated_mtu, uint32_t now_ms);
    void poll(uint32_t now_ms);
    bool take_notification(ble_voice_probe::Notification& packet);
    void record_notify_result(ble_voice_probe::Kind kind,
                              CodexBleNotifyResult result, uint32_t now_ms);
    void handle_disconnect();
    void shutdown();

private:
    static void producer_entry(void* context);
    void producer_loop();
    bool start_session(const ble_voice_probe::ControlCommand& command,
                       uint16_t negotiated_mtu, uint32_t now_ms);
    void request_stop(bool emit_summary, ble_voice_probe::ErrorCode reason);
    bool enqueue_packet(ble_voice_probe::Notification& packet);
    void finalize_stop(uint32_t now_ms);
};
```

Use `kVoicePacketQueueDepth = 32` and a queue item containing one `Notification`. The producer task never receives a transport pointer and never calls NimBLE. It only uses `enqueue_packet`.

- [ ] **Step 4: Implement synthetic pacing and congestion handling**

On a valid subscribed start:

1. start `Session`;
2. enqueue a `StartAck` using sequence `0`;
3. wake the producer task;
4. emit data until `duration_reached`;
5. request a clean stop and enqueue `StopSummary` only after prior data has drained.

The synthetic loop computes `due = session.synthetic_bytes_due(now_ms)`, chooses `min(due, notification_payload_capacity)`, allocates a sequence, fills deterministic bytes, and records generated bytes only after queue insertion succeeds.

Three consecutive queue insertion failures set `QueueCongested`; each failed insertion increments `queue_overflows`. A non-accepted NimBLE result increments the same consecutive-congestion count; only an `Accepted` data notification increments `accepted_data_packets`.

- [ ] **Step 5: Route control and output through the Codex app**

Create the controller in `onOpen()` after BLE transport starts. In `onRunning()` after `_ble_transport->poll(now)`:

```cpp
CodexBleVoiceControl control;
while (_ble_transport->take_voice_control(control)) {
    _voice_probe->handle_control(
        control,
        _ble_transport->voice_subscribed(),
        _ble_transport->negotiated_mtu(),
        now);
}
_voice_probe->poll(now);
ble_voice_probe::Notification notification;
while (_voice_probe->take_notification(notification)) {
    std::array<uint8_t, ble_voice_probe::kMaxNotificationSize> bytes = {};
    const size_t length = ble_voice_probe::encode_notification(
        notification, bytes.data(), bytes.size());
    _voice_probe->record_notify_result(
        notification.kind,
        _ble_transport->notify_voice(bytes.data(), length),
        now);
}
```

When `_ble_was_connected` transitions to false, call `_voice_probe->handle_disconnect()`. In `onClose()`, call `shutdown()` and destroy the controller before stopping and destroying the BLE transport.

- [ ] **Step 6: Run contracts and pure C++ tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
/tmp/ble_voice_probe_protocol_test
/tmp/ble_voice_probe_session_test
```

Expected: all tests exit `0`.

- [ ] **Step 7: Commit synthetic firmware support**

```bash
git add \
  main/apps/app_codex_status/ble_voice_probe_controller.h \
  main/apps/app_codex_status/ble_voice_probe_controller.cpp \
  main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py
git commit -m "Add bounded BLE synthetic voice probe"
```

### Task 8: Add Microphone Capture, Resampling, And Guaranteed Cleanup

**Files:**
- Modify: `main/apps/app_codex_status/ble_voice_probe_controller.h`
- Modify: `main/apps/app_codex_status/ble_voice_probe_controller.cpp`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`

- [ ] **Step 1: Extend the contract for ownership and audio cleanup**

Add a complete resource-cleanup contract method:

```python
def test_microphone_resources_are_acquired_and_released(self):
    controller = (
        ROOT / "main/apps/app_codex_status/ble_voice_probe_controller.cpp"
    ).read_text()
    self.assertIn("acquireCapture(RuntimeOwner::Codex)", controller)
    self.assertIn("releaseCapture(RuntimeOwner::Codex)", controller)
    self.assertIn("openRecorder(44100, 16, 1)", controller)
    self.assertIn("closeRecorder()", controller)
    self.assertIn("esp_ae_rate_cvt_open", controller)
    self.assertIn("esp_ae_rate_cvt_close", controller)
    self.assertIn("encode_ima_adpcm_block", controller)
    self.assertIn("record_captured_samples", controller)
```

- [ ] **Step 2: Verify the cleanup contract fails**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
```

Expected: failures for capture, resampler, and codec calls.

- [ ] **Step 3: Add microphone start preconditions**

Use fixed 20 ms blocks:

```cpp
constexpr uint32_t kCaptureRate = 44100;
constexpr uint32_t kOutputRate = 16000;
constexpr size_t kCaptureSamplesPerBlock = 882;
constexpr size_t kOutputSamplesPerBlock = 320;
constexpr size_t kAdpcmBytesPerBlock = 164;
```

Compute `notification_limit = min(kMaxNotificationSize, negotiated_mtu - 3)` only after confirming `negotiated_mtu > 3`. Reject microphone start with `MtuTooSmall` unless `notification_limit >= kNotificationHeaderSize + 164`. Acquire `RuntimeOwner::Codex` before opening the recorder. If acquisition or recorder open fails, emit the corresponding error without starting the stream and release any ownership already acquired. All recorder and resampler open/process/close calls run on the producer task; the start acknowledgement is queued only after microphone setup succeeds.

- [ ] **Step 4: Implement the 44.1 kHz to 16 kHz producer path**

Open the Espressif converter with:

```cpp
esp_ae_rate_cvt_cfg_t config = {
    .src_rate = 44100,
    .dest_rate = 16000,
    .channel = 1,
    .bits_per_sample = 16,
    .complexity = 3,
    .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY,
};
```

Call `esp_ae_rate_cvt_get_max_out_sample_num` once to size the converter output buffer. For each producer iteration:

1. read exactly `882 * sizeof(int16_t)` bytes;
2. call `esp_ae_rate_cvt_process` with `882` input samples and the converter-reported output capacity;
3. append the actual returned samples to a small PCM accumulator;
4. while at least `320` samples are available, encode one 164-byte independent IMA block;
5. enqueue that data notification and record exactly `320` transportable PCM samples;
6. retain fewer than `320` samples for the next iteration and discard only the final incomplete block at stop;
7. stop at requested duration or any read/resample/encode failure.

The output data packet payload is one entire ADPCM block; never split a block across notifications.

- [ ] **Step 5: Centralize cleanup and make it idempotent**

Use one function called from explicit stop, duration completion, disconnect, app close, queue congestion, read failure, resampler failure, and encoder failure:

```cpp
void BleVoiceProbeController::release_audio_resources()
{
    if (_resampler != nullptr) {
        esp_ae_rate_cvt_close(_resampler);
        _resampler = nullptr;
    }
    if (_recorder_open) {
        GetStopWatchAudioBackend().closeRecorder();
        _recorder_open = false;
    }
    if (_capture_owned) {
        GetResourceSupervisor().releaseCapture(RuntimeOwner::Codex);
        _capture_owned = false;
    }
}
```

`shutdown()` must signal the worker, wait for task exit with a bounded event/semaphore, call this cleanup again safely, delete queues/event objects, and leave no active session.

- [ ] **Step 6: Run codec, contract, and resource supervisor tests**

```bash
/tmp/ima_adpcm_codec_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp \
  -o /tmp/resource_supervisor_test
/tmp/resource_supervisor_test
```

Expected: all commands exit `0`.

- [ ] **Step 7: Commit microphone firmware support**

```bash
git add \
  main/apps/app_codex_status/ble_voice_probe_controller.h \
  main/apps/app_codex_status/ble_voice_probe_controller.cpp \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py
git commit -m "Add BLE microphone probe producer"
```

### Task 9: Build The Standalone Bleak Probe Client

**Files:**
- Create: `mac-codex-agent/ble_voice_probe.py`
- Create: `mac-codex-agent/tests/test_ble_voice_probe.py`

- [ ] **Step 1: Write fake-Bleak lifecycle tests**

Test an injected fake client and scanner rather than macOS Bluetooth itself. Start with this successful lifecycle test:

- subscribe before start write;
- start acknowledgement, data, and stop summary handling;
- automatic stop in `finally` on timeout or malformed packet;
- disconnect after notifications stop;
- partial JSON report written on failure;
- 16 kHz, 16-bit, mono WAV header and decoded sample count;
- nonzero result for failed required profile;
- `32768 B/s` report retained as exploratory;
- refusal when `pgrep -f "StopWatch Codex Agent.app/Contents/MacOS/StopWatchCodexAgent"` reports the installed Agent is running.

```python
import asyncio
import itertools
import json
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from ble_voice_probe import Profile, VoiceProbeClient
from voice_probe_protocol import (
    DATA,
    START_ACK,
    STOP_SUMMARY,
    SYNTHETIC,
    StopSummary,
    expected_synthetic_payload,
    pack_notification,
)


class FakeClient:
    def __init__(self, events):
        self.events = events
        self.is_connected = False
        self.callback = None
        self.operations = []

    async def connect(self):
        self.operations.append("connect")
        self.is_connected = True

    async def start_notify(self, uuid, callback):
        self.operations.append(("start_notify", uuid))
        self.callback = callback

    async def write_gatt_char(self, uuid, payload, response):
        self.operations.append(("write", uuid, payload[1], response))
        if payload[1] == 1:
            for event in self.events:
                self.callback(None, bytearray(event))

    async def stop_notify(self, uuid):
        self.operations.append(("stop_notify", uuid))

    async def disconnect(self):
        self.operations.append("disconnect")
        self.is_connected = False


class BleVoiceProbeTests(unittest.TestCase):
    def test_subscribes_collects_summary_and_disconnects(self):
        sizes = [234] * 34 + [236]
        events = [
            pack_notification(
                START_ACK,
                7,
                0,
                0,
                struct.pack("<HBBI", 256, SYNTHETIC, 0, 8192),
            )
        ]
        for index, size in enumerate(sizes):
            sequence = index + 1
            timestamp_ms = round(index * 1000 / len(sizes))
            events.append(
                pack_notification(
                    DATA,
                    7,
                    sequence,
                    timestamp_ms,
                    expected_synthetic_payload(sequence, size),
                )
            )
        events.append(
            pack_notification(
                STOP_SUMMARY,
                7,
                36,
                1000,
                StopSummary(35, 35, 0, 0, 1000, 8192, 0, 256).pack(),
            )
        )
        fake = FakeClient(events)
        clock_values = itertools.count(start=0, step=29_000_000)

        async def find_device(_name, _service):
            return object()

        runner = VoiceProbeClient(
            find_device=find_device,
            client_factory=lambda _device, _disconnect: fake,
            clock_ns=lambda: next(clock_values),
            agent_running=lambda: False,
        )
        with tempfile.TemporaryDirectory() as directory:
            report = pathlib.Path(directory) / "report.json"
            exit_code = asyncio.run(
                runner.run(
                    Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                    report_path=report,
                    firmware_revision="test-revision",
                )
            )
            saved = json.loads(report.read_text())

        self.assertEqual(exit_code, 0)
        self.assertTrue(saved["metrics"]["automated_pass"])
        self.assertLess(
            fake.operations.index(("start_notify", runner.voice_tx_uuid)),
            next(
                index
                for index, operation in enumerate(fake.operations)
                if isinstance(operation, tuple) and operation[0] == "write"
            ),
        )
        self.assertEqual(fake.operations[-1], "disconnect")


if __name__ == "__main__":
    unittest.main()
```

Add focused methods to the same test class for timeout cleanup, malformed notification cleanup, partial report persistence, microphone WAV parameters/sample count, failed-required-profile exit code, exploratory result labeling, and running-Agent refusal. Each method uses `FakeClient`; the timeout fake emits only `StartAck`, and the malformed fake emits `b"bad"` after `StartAck`. For the WAV test emit one valid 164-byte ADPCM data block, set stop-summary `captured_pcm_samples=320`, and assert with `wave.open` that channels are `1`, sample width is `2`, frame rate is `16000`, and frame count is `320`.

- [ ] **Step 2: Run and verify the client import fails**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe.py -v
```

Expected: import failure for `ble_voice_probe`.

- [ ] **Step 3: Implement the client lifecycle**

Define constants:

```python
DEVICE_NAME = "StopWatch-Codex"
SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"
VOICE_CONTROL_UUID = "7a230003-5474-4f50-5741-544348434f44"
VOICE_TX_UUID = "7a230004-5474-4f50-5741-544348434f44"
```

Define the testable public surface before implementing the lifecycle:

```python
@dataclasses.dataclass(frozen=True)
class Profile:
    session: int
    mode: int
    target_bps: int
    duration_ms: int

class VoiceProbeClient:
    def __init__(
        self,
        find_device=None,
        client_factory=None,
        clock_ns=time.monotonic_ns,
        agent_running=None,
    ):
        self.voice_control_uuid = VOICE_CONTROL_UUID
        self.voice_tx_uuid = VOICE_TX_UUID
        self._find_device = find_device or find_watch
        self._client_factory = client_factory or make_bleak_client
        self._clock_ns = clock_ns
        self._agent_running = agent_running or stopwatch_agent_running

```

Add `VoiceProbeClient.run` with the exact signature `async def run(self, profile: Profile, report_path: pathlib.Path, wav_path: Optional[pathlib.Path] = None, firmware_revision: str = "", allow_running_agent: bool = False) -> int`, then implement it in this order:

1. discover by name or service UUID;
2. connect with a disconnect callback;
3. call `start_notify(VOICE_TX_UUID, callback)`;
4. write the start command with `response=True`;
5. wait for matching start acknowledgement;
6. collect matching-session data until stop summary or timeout `duration + 10 seconds`;
7. in `finally`, write stop when still connected, stop notifications, and disconnect;
8. always write a report, including exception text and partial counters;
9. return `0` only when automated acceptance passes.

Use `time.monotonic_ns()` in the notification callback and queue parsed notifications onto the asyncio loop. Reject malformed packets, unexpected sessions, sequence regression, incomplete summary, or disconnect.

- [ ] **Step 4: Implement CLI arguments and WAV output**

Expose:

```text
ble_voice_probe.py synthetic --target-bps {8192,16384,32768} --duration 60
ble_voice_probe.py microphone --target-bps 8192 --duration 30 --wav PATH
```

Both forms require `--report PATH` and accept `--device-name`, `--firmware-revision`, and `--allow-running-agent` for unit tests only. Normal hardware commands must not pass `--allow-running-agent`.

For microphone packets, append decoded PCM samples and write them with:

```python
with wave.open(str(path), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(16000)
    wav.writeframes(struct.pack("<{}h".format(len(samples)), *samples))
```

The report includes ISO timestamp, Git revision argument, device name, requested profile, negotiated MTU, all sender/receiver metrics, automated result, manual-audio-review flag, exception text, and WAV path.

- [ ] **Step 5: Run all Mac probe tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_protocol.py \
  mac-codex-agent/tests/test_voice_probe_metrics.py \
  mac-codex-agent/tests/test_ble_voice_probe.py -v
```

Expected: all tests pass without requiring Bluetooth hardware.

- [ ] **Step 6: Commit the standalone client**

```bash
git add \
  mac-codex-agent/ble_voice_probe.py \
  mac-codex-agent/tests/test_ble_voice_probe.py
git commit -m "Add standalone Mac BLE voice probe"
```

### Task 10: Integrate Offline Validation And Operator Documentation

**Files:**
- Modify: `scripts/run_unified_offline_validation.sh`
- Create: `docs/ble-voice-link-probe.md`

- [ ] **Step 1: Add the three C++ tests to offline validation**

Change the displayed total from `16` to `19`. Insert protocol, session, and codec steps before the isolated firmware build:

```bash
echo "[15/19] BLE voice probe protocol host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_protocol_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_protocol.cpp \
    -o "$HOST_TEST_DIR/ble_voice_probe_protocol_test"
"$HOST_TEST_DIR/ble_voice_probe_protocol_test"

echo "[16/19] BLE voice probe session host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_session_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_session.cpp \
    -o "$HOST_TEST_DIR/ble_voice_probe_session_test"
"$HOST_TEST_DIR/ble_voice_probe_session_test"

echo "[17/19] IMA ADPCM codec host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ima_adpcm_codec_test.cpp \
    main/apps/app_codex_status/ima_adpcm_codec.cpp \
    -o "$HOST_TEST_DIR/ima_adpcm_codec_test"
"$HOST_TEST_DIR/ima_adpcm_codec_test"

echo "[18/19] Isolated unified candidate clean build and capacity gates"
./scripts/build_unified_candidate.sh --clean

echo "[19/19] Whitespace error check"
git diff --check
```

The existing Python discovery step automatically includes all four new Python test files.

- [ ] **Step 2: Write the operator document**

Document:

- the probe is diagnostic and does not control Typeless or buttons;
- the menu Agent must be quit, not merely connection-disabled;
- open the Codex app before discovery;
- exact synthetic and microphone commands from Task 12;
- JSON field meanings and the thresholds from the design;
- 32 KiB/s is exploratory;
- physical listening remains a separate acceptance item;
- reports/WAV/logs belong under `/tmp`, not Git;
- cleanup checks: close Codex app, open Assistant, verify Assistant can acquire the microphone, then reopen Codex and reconnect status BLE.

- [ ] **Step 3: Run the complete offline gate**

```bash
./scripts/run_unified_offline_validation.sh
```

Expected: all 19 displayed steps pass, the isolated unified firmware builds, and capacity gates pass. Record the exact app size in the implementation report.

- [ ] **Step 4: Check only probe-specific diffs**

```bash
git diff --check
git status --short
git diff -- \
  main/apps/app_codex_status/ble_voice_probe_protocol.h \
  main/apps/app_codex_status/ble_voice_probe_protocol.cpp \
  main/apps/app_codex_status/ble_voice_probe_session.h \
  main/apps/app_codex_status/ble_voice_probe_session.cpp \
  main/apps/app_codex_status/ima_adpcm_codec.h \
  main/apps/app_codex_status/ima_adpcm_codec.cpp \
  main/apps/app_codex_status/ble_voice_probe_controller.h \
  main/apps/app_codex_status/ble_voice_probe_controller.cpp \
  main/apps/app_codex_status/codex_ble_transport.h \
  main/apps/app_codex_status/codex_ble_transport.cpp \
  main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/voice_probe_protocol.py \
  mac-codex-agent/voice_probe_metrics.py \
  mac-codex-agent/ble_voice_probe.py \
  mac-codex-agent/tests/test_voice_probe_protocol.py \
  mac-codex-agent/tests/test_voice_probe_metrics.py \
  mac-codex-agent/tests/test_ble_voice_probe.py \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py \
  tests/ble_voice_probe_protocol_test.cpp \
  tests/ble_voice_probe_session_test.cpp \
  tests/ima_adpcm_codec_test.cpp \
  scripts/run_unified_offline_validation.sh \
  docs/ble-voice-link-probe.md
```

Expected: unrelated worktree files still appear in `git status`, but the scoped diff contains only this probe.

- [ ] **Step 5: Commit validation wiring and docs**

```bash
git add \
  scripts/run_unified_offline_validation.sh \
  docs/ble-voice-link-probe.md
git commit -m "Document BLE voice probe validation"
```

### Task 11: Flash The Probe Firmware With Backup Guard

**Files:**
- Evidence only: `/tmp/stopwatch-ble-voice-probe/<timestamp>/`

- [ ] **Step 1: Verify backup, build, port, and stopped menu Agent**

```bash
BACKUP=backups/watch-full-flash-20260718-173922.bin
test "$(wc -c < "$BACKUP" | tr -d '[:space:]')" = "16777216"
shasum -a 256 -c "${BACKUP}.sha256"
test -f build-unified/StopWatch-UserDemo.bin
PORTS=(/dev/cu.usbmodem*)
test "${#PORTS[@]}" = "1"
PORT="${PORTS[1]}"
! pgrep -f "StopWatch Codex Agent.app/Contents/MacOS/StopWatchCodexAgent"
```

Expected: backup check reports `OK`, exactly one serial device exists, and the final command exits `0` because the Agent is not running. In zsh arrays are one-indexed, so `${PORTS[1]}` is intentional.

- [ ] **Step 2: Flash through the repository backup gate**

```bash
BACKUP=backups/watch-full-flash-20260718-173922.bin
PORTS=(/dev/cu.usbmodem*)
test "${#PORTS[@]}" = "1"
PORT="${PORTS[1]}"
./scripts/flash_unified_candidate.sh --port "$PORT" --backup "$BACKUP"
```

Expected: bootloader, partition table, unified app, and assets writes complete and esptool performs a hard reset. Do not erase the device.

- [ ] **Step 3: Start a persistent serial log and open Codex**

```bash
PORTS=(/dev/cu.usbmodem*)
test "${#PORTS[@]}" = "1"
PORT="${PORTS[1]}"
EVIDENCE="/tmp/stopwatch-ble-voice-probe/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$EVIDENCE"
printf '%s\n' "$EVIDENCE" > /tmp/stopwatch-ble-voice-probe/current
mac-codex-agent/.venv/bin/python scripts/watch_debug.py \
  --port "$PORT" --follow app open Codex \
  > "$EVIDENCE/watch.log" 2>&1 &
WATCH_LOG_PID=$!
sleep 5
```

Expected: the log contains `DEBUG APP OPEN codex OK`, the Codex app is visible, and BLE advertising resumes as `StopWatch-Codex`.

- [ ] **Step 4: Record firmware identity**

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
git rev-parse HEAD > "$EVIDENCE/firmware-git-revision.txt"
shasum -a 256 build-unified/StopWatch-UserDemo.bin \
  > "$EVIDENCE/firmware-app.sha256"
```

Expected: both files are nonempty and remain outside Git.

### Task 12: Run Physical BLE Throughput And Microphone Acceptance

**Files:**
- Evidence only: `/tmp/stopwatch-ble-voice-probe/<timestamp>/`

- [ ] **Step 1: Run the required 8 KiB/s profile for 60 seconds**

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
REVISION="$(cat "$EVIDENCE/firmware-git-revision.txt")"
mac-codex-agent/.venv/bin/python mac-codex-agent/ble_voice_probe.py \
  synthetic --target-bps 8192 --duration 60 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/synthetic-8192.json"
```

Expected: exit `0`; zero loss, corruption, overflow, encode failure, and disconnect; p95 deviation `<= 40 ms`; maximum steady-state gap `<= 100 ms`.

If this step fails, retain evidence, skip microphone testing, reject BLE for continuous voice, and create a Wi-Fi UDP transport design.

- [ ] **Step 2: Run the required 16 KiB/s margin profile for 60 seconds**

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
REVISION="$(cat "$EVIDENCE/firmware-git-revision.txt")"
mac-codex-agent/.venv/bin/python mac-codex-agent/ble_voice_probe.py \
  synthetic --target-bps 16384 --duration 60 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/synthetic-16384.json"
```

Expected: exit `0` with the same required thresholds.

If 8 KiB/s passed and this step fails, retain evidence, do not accept BLE yet, and create a separate Opus feasibility design.

- [ ] **Step 3: Run the exploratory 32 KiB/s profile**

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
REVISION="$(cat "$EVIDENCE/firmware-git-revision.txt")"
set +e
mac-codex-agent/.venv/bin/python mac-codex-agent/ble_voice_probe.py \
  synthetic --target-bps 32768 --duration 60 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/synthetic-32768.json"
EXPLORATORY_RC=$?
set -e
printf '%s\n' "$EXPLORATORY_RC" > "$EVIDENCE/synthetic-32768.exit-code"
```

Expected: result is recorded. A failure here does not reject BLE after both required profiles pass.

- [ ] **Step 4: Record a 30-second microphone sample**

Speak normal Mandarin continuously while this command runs:

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
REVISION="$(cat "$EVIDENCE/firmware-git-revision.txt")"
mac-codex-agent/.venv/bin/python mac-codex-agent/ble_voice_probe.py \
  microphone --target-bps 8192 --duration 30 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/microphone.json" \
  --wav "$EVIDENCE/microphone.wav"
```

Expected: exit `0`, zero transport failures, and WAV duration error `<= 1%` against `captured_pcm_samples`.

- [ ] **Step 5: Listen and record the manual quality decision**

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
afplay "$EVIDENCE/microphone.wav"
printf '%s\n' \
  'Manual audio review: intelligible Mandarin; stable speed; no periodic dropout: PASS' \
  > "$EVIDENCE/manual-audio-review.txt"
```

Only write `PASS` after listening confirms all three conditions. Otherwise write `FAIL` plus the observed artifact. A quality failure with clean transport rejects IMA-ADPCM quality, not BLE throughput.

- [ ] **Step 6: Stop the probe serial log and scan it for fatal events**

```bash
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
WATCH_LOG_PID="$(pgrep -f "scripts/watch_debug.py.*--follow app open Codex" | head -1)"
test -n "$WATCH_LOG_PID"
kill "$WATCH_LOG_PID" 2>/dev/null || true
wait "$WATCH_LOG_PID" 2>/dev/null || true
! rg -n "Guru Meditation|watchdog|abort\(\)|assert failed|rst:0x" "$EVIDENCE/watch.log"
```

Expected: no fatal reset signatures. BLE disconnect/reconnect logs caused by the probe are allowed.

- [ ] **Step 7: Verify disconnect and app-close cleanup**

```bash
PORTS=(/dev/cu.usbmodem*)
test "${#PORTS[@]}" = "1"
PORT="${PORTS[1]}"
EVIDENCE="$(cat /tmp/stopwatch-ble-voice-probe/current)"
mac-codex-agent/.venv/bin/python scripts/watch_debug.py --port "$PORT" app home
sleep 2
mac-codex-agent/.venv/bin/python scripts/watch_debug.py \
  --port "$PORT" --follow app open Assistant \
  > "$EVIDENCE/assistant-cleanup.log" 2>&1 &
ASSISTANT_LOG_PID=$!
sleep 15
kill "$ASSISTANT_LOG_PID" 2>/dev/null || true
wait "$ASSISTANT_LOG_PID" 2>/dev/null || true
mac-codex-agent/.venv/bin/python scripts/watch_debug.py \
  --port "$PORT" assistant status \
  > "$EVIDENCE/assistant-status.log" 2>&1
rg -n "ASTATUS .*capture=1" "$EVIDENCE/assistant-status.log"
! rg -n "Microphone busy|Guru Meditation|watchdog|abort\(\)|assert failed" \
  "$EVIDENCE/assistant-cleanup.log" "$EVIDENCE/assistant-status.log"
mac-codex-agent/.venv/bin/python scripts/watch_debug.py --port "$PORT" app home
mac-codex-agent/.venv/bin/python scripts/watch_debug.py --port "$PORT" app open Codex
```

Expected: Assistant reaches a state where capture owner is `RuntimeOwner::Assistant` (`capture=1`) without a microphone-busy error, reboot, watchdog, or initialization stall. After Codex reopens, launch the normal menu Agent manually and verify status BLE reconnects; quit it again before any repeat probe.

- [ ] **Step 8: Apply the decision rule and report evidence separately**

Report four distinct results:

1. host tests and firmware build;
2. flash command and firmware hash;
3. required/exploratory BLE metrics from JSON;
4. manual microphone listening and post-close resource cleanup.

Accept BLE as the production transport only when both required synthetic profiles, microphone automated metrics, manual audio review, and cleanup checks pass. Do not claim Typeless compatibility from this probe; that is a later end-to-end test.

Do not commit `/tmp/stopwatch-ble-voice-probe` evidence. Commit source only after all code-review findings are resolved:

```bash
git status --short
git log --oneline --max-count=12
```

Expected: the probe commits are present, unrelated worktree changes remain untouched, and no raw evidence appears as an untracked repository file.
