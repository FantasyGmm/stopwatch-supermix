# Brookesia Xiaozhi Compatibility Gate

Date: 2026-07-22

Gate status: Automated startup, lifecycle, 10-cycle, 20-cycle, and 50-cycle stability, full product smoke, full-Flash recovery, and final post-recovery validation passed. Spoken wake-word, physical controls, BLE/Wi-Fi coexistence, and complete UI observation remain manual checks.

## Candidate configuration

- ESP-IDF: 5.5.4
- Brookesia Agent Manager: 0.7.5
- Brookesia Xiaozhi Agent: 0.7.4
- Brookesia NVS service: 0.7.4
- Brookesia SNTP service: 0.7.2
- Xiaozhi endpoint: `https://api.tenclass.net/xiaozhi/ota/`
- Device identity: existing `board` NVS namespace and hardware MAC
- Audio: mono, 16 kHz, 16-bit, Opus at 24 kbit/s, 60 ms frames
- Wake model: `wn9_nihaoxiaozhi_tts` from the `assets` partition
- Audio implementation: light feeder/recorder compatibility overlay and PSRAM playback worker
- MCP ownership: the chat session owns and destroys the MCP engine after runtime users stop

## Automated and build evidence

The currently installed application is 6,036,080 bytes (`0x5c1a70`) with SHA256 `0bddd1cbe4502d803e19e71ba1e99efdd8c5e470659dcdefdf1df300cd86b0c7`. It leaves 1,828,240 bytes (23.25%) in the unified `0x780000` application partition. `generated_assets.bin` is 291,198 bytes with SHA256 `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f`.

The checked-in hardware campaign automates 10 and 50 balanced listening, recording, TTS, reconnect, and completed-conversation lifecycles; an all-application smoke test; and complete-Flash restore/reinstall recovery. The 50-cycle connected-watch gate is complete.

The default single-application layout is promoted in `partitions.csv`. The duplicate shadow table and obsolete dual-OTA slot tools are removed. Recovery uses the verified complete Flash backup; there is no remote update or automatic rollback in this phase.

## Hardware evidence

The tested watch (`44:1b:f6:c1:86:5c`) had a verified 16 MiB backup before candidate writes. USB debug control opened Assistant without user interaction. Wi-Fi connected; activation succeeded; decoder, encoder, WakeNet, the Agent session, MCP initialization, server audio, and microphone upload all became active without rebooting.

The current 10-cycle gate passed with all five profiles represented twice, cumulative internal loss 284 B, peak internal loss 0 B, peak PSRAM loss 464 B, and maximum stop time 307 ms. The 50-cycle gate passed with all profiles represented ten times, cumulative internal loss -20 B, peak internal loss 0 B, peak PSRAM loss 320 B, and maximum stop time 314 ms. Every measured stop returned owners and task count to the steady-state baseline. The prior 20-cycle result remains additional evidence.

Product smoke passed for Codex, Clock, Badge, IMU, Audio.FFT, and Settings. The recovery exercise restored and booted the complete 16 MiB backup, then reinstalled and booted the exact unified candidate. A final hard-reset boot followed immediately by the six-application smoke also passed after adding explicit device-parser synchronization between USB debug sessions.

The branch-advancing follow-up added another 50 measured sessions, counted as 18 + 11 + 21 across two reflashes while preserving the five-profile schedule. Shared Wi-Fi commits landed between those coherent builds, so this combined campaign is continuity evidence across the evolving branch, not a same-binary 50-cycle claim. Each profile still ran ten times. The exact final `cd5b72d-dirty` image's 21-session segment reported cumulative internal loss 176 B, peak internal loss 0 B, peak PSRAM loss 332 B, and maximum stop time 293 ms. A single cumulative heap delta is intentionally not reported across the three segments because each reflash reset the baseline. The final image then passed hard-reset boot and six-application smoke; the earlier release candidate retains the separate uninterrupted 50-cycle result.

Muted automation now maps HAL volume zero to both Brookesia volume zero and mute-on. This fixes the service default-state case where `SetVolume(0)` could skip the actual player update. The watch remained at volume zero during the final segment.

## Compatibility matrix

| Capability | Status | Evidence or remaining check |
|---|---|---|
| Existing activation | Passed | Server activation completed |
| Listen/server session | Passed | Agent connected and audio channel opened |
| Microphone/encoder | Passed | Encoder/recorder and audio upload observed |
| Decoder/playback | Passed | Decoder and PSRAM playback worker started |
| MCP product path | Passed | Initialize/tools-list observed; lifecycle leak fixed |
| Audible TTS | Passed manual | User confirmed normal voice |
| Stop in all five profiles | Passed | Each profile ran ten times in the 50-cycle mixed campaign |
| Repeated entry and post-stop cleanup | Passed | Owners zero; steady-state task delta zero |
| Full application regression | Passed | Six-app smoke before and after recovery |
| Full-Flash recovery | Passed | Backup boot and candidate reinstall/boot verified |
| Wake phrase `你好小智` | Pending manual | Physically speak and observe WakeStart |
| User text and emotion/display | Pending manual | Observe transcription and complete UI matrix |

The automated compatibility and lifecycle gate is complete. Release finalization now waits only for the explicitly manual hardware observations listed above.
