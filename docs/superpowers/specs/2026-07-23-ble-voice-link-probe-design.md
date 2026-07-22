# BLE Voice Link Probe Design

## Purpose

Validate whether the existing encrypted BLE connection between the StopWatch Codex app and the Mac can carry continuous microphone audio with enough throughput, latency margin, and stability for a later Typeless push-to-talk feature.

This probe answers only the BLE transport question. It does not integrate Typeless, synthesize keyboard events, install a virtual audio device, or change the production right-button interaction.

## Scope

The probe adds an isolated watch-to-Mac diagnostic path alongside the current Mac-to-watch status path:

- a BLE notification characteristic for probe control acknowledgements and audio packets;
- a control command that starts and stops synthetic or microphone probe sessions;
- a standalone Mac command-line probe client;
- sequence, throughput, timing, queue, and disconnect metrics;
- optional WAV output for reviewing real microphone quality.

The installed menu bar Agent is not modified or reinstalled by this probe. It must be stopped while the standalone probe client owns the single BLE connection.

## Non-Goals

- Activating or controlling Typeless.
- Sending `Fn` or another keyboard shortcut.
- Installing or configuring BlackHole.
- Changing the Codex app's normal right-button behavior.
- Adding Wi-Fi audio transport.
- Selecting the final production audio codec before measurements are available.
- Claiming the BLE path works without a physical-device run.

## Architecture

### Watch Firmware

The existing `CodexBleTransport` remains the BLE peripheral and retains the encrypted status-write characteristic. The probe adds one encrypted notification characteristic to the same service. The characteristic is used only after the Mac subscribes and explicitly starts a probe session.

A bounded probe controller owns session state and queues outgoing packets. It must not call NimBLE notification APIs from the audio-capture callback. Capture or synthetic generation writes bounded packets to a queue; the BLE sender drains the queue and records congestion or drops.

The probe controller acquires `RuntimeOwner::Codex` microphone ownership before opening the recorder and releases it on every success, error, stop, disconnect, and app-close path. A busy microphone rejects the start command without starting a partial session.

### Mac Probe Client

A standalone Python script under `mac-codex-agent/` uses Bleak to:

1. find `StopWatch-Codex`;
2. connect and subscribe to the probe notification characteristic;
3. start one requested test profile;
4. validate packet headers and sequence continuity;
5. calculate throughput, arrival gaps, jitter, and loss;
6. stop the session cleanly;
7. emit a machine-readable JSON report and a concise terminal summary;
8. write decoded microphone audio to a WAV file when requested.

The script returns a non-zero status when an acceptance threshold is missed.

## Protocol

All multibyte integers use little-endian encoding. Every notification begins with a fixed header:

| Field | Size | Meaning |
| --- | ---: | --- |
| Version | 1 byte | Probe protocol version, initially `1` |
| Kind | 1 byte | Start acknowledgement, data, stop summary, or error |
| Session | 2 bytes | Session identifier selected by the Mac |
| Sequence | 4 bytes | Monotonic packet sequence within the session |
| Watch time | 4 bytes | `millis()` when the packet was produced |
| Payload length | 2 bytes | Number of payload bytes in this notification |

The watch records the negotiated ATT MTU and limits each notification to `MTU - 3`. Payloads larger than one notification are split into separately sequenced packets; the probe never depends on implicit fragmentation.

The Mac starts a session using a compact binary command written through a dedicated encrypted control characteristic. The command includes protocol version, session identifier, mode, target bytes per second, and duration. Supported initial modes are:

- `synthetic`: deterministic bytes generated without opening the microphone;
- `microphone_adpcm`: 44.1 kHz microphone PCM resampled to 16 kHz mono and encoded as 4-bit IMA-ADPCM.

The stop summary reports generated packets, accepted notifications, queue overflows, encode failures, and elapsed watch time. The Mac report compares this summary with received packets so sender congestion and receiver loss are distinguishable.

## Test Profiles

### Synthetic Transport Tests

Run each profile for 60 seconds:

1. `8 KiB/s`: target load for 16 kHz 4-bit IMA-ADPCM.
2. `16 KiB/s`: required two-times operating margin.
3. `32 KiB/s`: exploratory load equivalent to 16 kHz 16-bit mono PCM.

The deterministic payload lets the Mac detect corruption in addition to missing or reordered packets.

### Microphone Test

Run a 30-second microphone session after the 8 and 16 KiB/s synthetic profiles pass. The watch uses the existing audio backend at 44.1 kHz, the existing Espressif rate-conversion component for 16 kHz output, and an isolated IMA-ADPCM encoder. The Mac decodes the stream to 16-bit mono PCM and saves a 16 kHz WAV file.

The microphone test is judged by transport metrics and a manual listen for intelligible Chinese speech, stable speed, and absence of periodic gaps or severe artifacts.

## Acceptance Criteria

BLE is accepted for the production voice path only when all of the following are true:

- the watch completes each required profile without reboot, watchdog, or BLE disconnect;
- the 8 KiB/s and 16 KiB/s synthetic profiles have zero application-layer packet loss and zero payload corruption for 60 seconds;
- the 95th-percentile inter-arrival deviation is at most 40 ms;
- no notification gap exceeds 100 ms during steady-state transfer;
- the watch reports zero queue overflows and zero encoder failures;
- the microphone WAV duration differs from watch capture duration by no more than 1 percent;
- the microphone WAV contains intelligible speech without periodic dropouts or speed changes;
- recorder ownership and BLE resources return to their pre-test state after stop, disconnect, and app close.

The 32 KiB/s profile is informative. Failure at 32 KiB/s does not reject BLE if both required profiles pass.

## Decision Rules

- If all required criteria pass, use BLE as the primary production audio transport and retain IMA-ADPCM unless Typeless accuracy testing later rejects its quality.
- If 8 KiB/s passes but 16 KiB/s fails, BLE lacks the required safety margin. Run a separate Opus feasibility design before accepting BLE.
- If 8 KiB/s fails, reject BLE for continuous audio and design the Wi-Fi UDP path.
- If transport passes but microphone quality fails, keep BLE accepted and evaluate a better codec; do not conflate codec quality with radio throughput.

## Failure Handling

The watch stops a probe and releases all owned resources when any of these occurs:

- Mac stop command;
- BLE disconnect;
- Codex app close;
- microphone read, resample, or encode failure;
- sustained notification congestion;
- maximum configured duration reached.

The Mac client treats an incomplete stop summary, sequence regression, malformed packet, unexpected session identifier, or disconnect as a failed run. Partial metrics and WAV data are retained for diagnosis.

## Testing Strategy

Host tests cover protocol encoding, sequence accounting, synthetic payload verification, IMA-ADPCM round trips, metric calculations, and acceptance decisions. Firmware contract tests verify service permissions, bounded queues, ownership cleanup, and absence of production button or Typeless integration.

The ESP-IDF build must pass before device work. Physical validation then runs the three synthetic profiles and one microphone profile, records the firmware hash and device log, and stores JSON/WAV evidence outside Git unless the user explicitly requests committing it.

## Repository Safety

The current worktree contains unrelated Assistant and dependency work. Implementation commits for this probe must stage only probe-specific firmware, Mac client, tests, and documentation. Existing modified, deleted, and untracked files from other sessions must remain untouched.
