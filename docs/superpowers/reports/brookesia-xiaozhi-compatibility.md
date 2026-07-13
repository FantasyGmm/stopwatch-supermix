# Brookesia Xiaozhi Compatibility Gate

Date: 2026-07-14

Gate status: Pending hardware. The offline candidate is build-verified, but this report does not authorize removal of the dual-image fallback or a candidate write.

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

The 2026-07-14 offline runner completed from committed source `b3e8952e2a993fc3da78c882bcdce4a99dad4008`: 114 Python tests, all 5 host C++ test binaries, the isolated clean unified build, capacity gates, and `git diff --check` passed. The candidate links against the Brookesia 0.7 versions in `dependencies.lock`; Brookesia 0.8 is not selected. Its production lifecycle starts `Activate` and `Start` asynchronously and releases subscriptions only after the current session's `Stopped` event.

The generated application is 6,005,520 bytes (`0x5ba310`) with SHA256 `bfb0a6a8098c71aac11e916ba3e8507cee36014390c9591c4e70bd0d965c3a28`. It leaves 1,858,800 bytes (`0x1c5cf0`, 23.64%) in the unified `0x780000` application partition and passes the `0x660000` hard gate. Static DIRAM is 200,931 bytes. `generated_assets.bin` is 291,198 bytes with SHA256 `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` and passes its 4 MiB gate.

The legacy dual-OTA `partitions.csv`, legacy Assistant recovery path, and complete-backup restore helper remain checked in. The Assistant product path contains no boot-partition switch, restart, or `ota_1` reference, and the new backup/restore/candidate helpers contain no `erase_flash`. No firmware was flashed, no serial port was accessed, and no device command was run. When hardware work begins, the offline gate is rerun first; the first hardware action in Stage 2 is a complete, size- and SHA256-verified 16 MiB Flash backup before any candidate write.

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

Task 8 may proceed in offline shadow mode; single-partition promotion remains gated by this hardware matrix, the lifecycle/stress gates, and the complete regression campaign.
