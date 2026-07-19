# Brookesia Xiaozhi Compatibility Gate

Date: 2026-07-19

Gate status: Targeted startup and lifecycle passed. Audible conversation, spoken wake-word, timing-window stress, full product regression, and final clean-image reflash remain pending.

## Candidate configuration

- ESP-IDF: 5.5.4
- Brookesia Agent Manager: 0.7.5
- Brookesia Xiaozhi Agent: 0.7.4
- Brookesia NVS service: 0.7.4
- Brookesia SNTP service: 0.7.2
- Xiaozhi endpoint: `https://api.tenclass.net/xiaozhi/ota/`
- Device identity: existing `board` NVS namespace and hardware MAC
- Audio: mono, 16 kHz, 16-bit, Opus at 24 kbit/s, 60 ms frames
- Wake model: `wn9_nihaoxiaozhi_tts` from the existing `assets` partition
- Audio implementation: light feeder/recorder compatibility overlay; 24 KiB worker stack in PSRAM
- Allocation policy: ordinary allocations may use PSRAM immediately; 32 KiB internal/DMA reserve retained

## Automated evidence

The 2026-07-19 validation passed 133 Python tests, 6 host C++ test binaries, the unified firmware build, both capacity gates, and the whitespace gate. The final application built from committed runtime source `435f5919a2c8c42293df4e4e3dbc6929beaa997b` is 5,998,336 bytes (`0x5b8700`) with SHA256 `881be58f332985cfc2601e870e13984a967fd6c674b264841c434e7361dfcede`. It leaves 1,865,984 bytes (23.73%) in the unified `0x780000` application partition. Static DIRAM is 185,415 bytes. `generated_assets.bin` is 291,198 bytes with SHA256 `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f`.

The legacy dual-OTA table and complete-backup restore path remain checked in. The merged Assistant path contains no boot-partition switch, restart, or `ota_1` dependency.

## Hardware evidence

The tested watch (`44:1b:f6:c1:86:5c`) had a verified 16 MiB backup before candidate writes. USB debug control opened Assistant without user interaction. Wi-Fi obtained `192.168.50.238`; activation succeeded; Decoder, Encoder, WakeNet, the agent session, MCP initialization, the server audio channel, and continuous microphone packets all became active without a reboot. Automated Home then stopped the complete service graph and released all bindings, subscriptions, Wi-Fi, capture, and playback owners. A second open succeeded and its stop boundary reported `task_delta=0` and `gate=true`.

The exact final application image SHA above was flashed after all address ranges passed esptool write-hash verification. Two automated open/run/Home lifecycles then completed against that image; the steady-state second stop reported `internal_loss=3984`, `psram_loss=39404`, `task_delta=0`, and `gate=true`.

## Compatibility matrix

| Capability | Status | Evidence or remaining check |
|---|---|---|
| Existing activation | Passed | Server activation completed |
| Listen/server session | Passed | Agent connected and audio channel opened |
| Microphone/encoder | Passed | Continuous 180-byte packets observed |
| Decoder/playback path startup | Passed | Decoder started; light feeder opened |
| MCP product path | Passed | Initialize and tools/list received |
| Wake phrase `你好小智` | Pending manual | Physically speak and observe WakeStart |
| User text | Pending manual | Observe transcription callback/UI |
| Assistant text/emotion | Pending manual | Observe callback/UI |
| Audible TTS | Pending manual | Confirm stable audible output |
| Stop while recording | Passed | Encoder/recorder stopped and capture released |
| Reopen after stop | Passed | Second session reached running |
| Post-stop tasks/connections | Passed | Bindings/subscriptions/owners zero; second-cycle `task_delta=0` |
| Stop during idle, TTS, reconnect | Pending campaign | Exercise explicit timing windows |
| 10/100-cycle stress | Pending campaign | Collect per-cycle memory and reset evidence |
| Full application regression | Pending campaign | Run all product areas and coexistence checks |

Single-partition promotion remains gated by the remaining manual and campaign rows, even though the merged Launcher/Assistant startup and normal recording-stop lifecycle are now working on hardware.
