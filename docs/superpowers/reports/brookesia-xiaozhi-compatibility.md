# Brookesia Xiaozhi Compatibility Gate

Date: 2026-07-13

Gate status: Pending hardware. This report records the build-ready probe only; it does not authorize removal of the dual-image fallback or entry into the destructive partition migration.

## Candidate configuration

- ESP-IDF: 5.5.4
- Brookesia Agent Manager: 0.7.5
- Brookesia Xiaozhi Agent: 0.7.4
- Brookesia NVS service: 0.7.4
- Brookesia SNTP service: 0.7.2
- Xiaozhi endpoint: `https://api.tenclass.net/xiaozhi/ota/`
- Device identity: existing `board` NVS namespace and hardware MAC; no new user-visible identity is generated
- Language: Simplified Chinese
- Audio: mono, 16 kHz, 16-bit, Opus at 24 kbit/s, 60 ms frames
- Wake model: `wn9_nihaoxiaozhi_tts` from the existing `assets` partition
- UI assets: no Xiaozhi font or emoji collection is packaged

## Non-hardware evidence

The dependency lock excludes Brookesia 0.8. The unified build generates a 291,198-byte `generated_assets.bin` and enforces a 4 MiB ceiling. The temporary probe compiles and links against the resolved 0.7 APIs, starts `Activate` and `Start` asynchronously, and releases its subscriptions only after the `Stopped` lifecycle event.

The linked application image is `0x5c22d0` bytes. The existing dual-OTA layout intentionally remains unchanged, so its `0x4f0000` slots reject this candidate with an overflow of `0xd22d0`. This is the expected Task 10 capacity gap, not a Task 7 API or link failure; no partition or flash operation was performed.

## Hardware matrix

| Capability | Status | Required evidence |
|---|---|---|
| Existing activation or approved reactivation | Pending hardware | Activation log and identity comparison |
| Wake phrase `你好小智` | Pending hardware | Local WakeStart event |
| Manual start/listen | Pending hardware | Listening event and server session |
| User text | Pending hardware | UI/probe callback log |
| Assistant text/emotion | Pending hardware | UI/probe callback log |
| TTS audio | Pending hardware | Audible stable playback |
| Existing Xiaozhi/MCP product functions | Pending hardware | Function-by-function result |
| `Stop` while idle | Pending hardware | `Stopped` within 2 seconds |
| `Stop` while recording | Pending hardware | `Stopped` within 2 seconds |
| `Stop` while playing | Pending hardware | `Stopped` within 2 seconds |
| `Stop` while reconnecting | Pending hardware | `Stopped` within 2 seconds |
| Post-stop tasks/connections | Pending hardware | Zero Agent session residue |

Task 8 may proceed in offline shadow mode; single-partition promotion remains gated by this hardware matrix and the complete regression campaign.
