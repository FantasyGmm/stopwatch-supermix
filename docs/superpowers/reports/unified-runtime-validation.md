# Unified Runtime Validation

Date: 2026-07-22

> The secure multi-Agent pairing follow-up is documented in
> [multi-agent-secure-pairing-validation.md](multi-agent-secure-pairing-validation.md).
> Its automated gates pass; flashing and physical pairing remain pending until
> a watch USB port and verified full-Flash backup are available.

- Automated tests and unified build: Passed
- Targeted hardware startup/lifecycle: Passed
- Audible TTS after jitter-buffer fix: Passed by user observation
- 10-cycle, 20-cycle, 50-cycle, full product regression, and recovery exercise: Passed
- Final boot-to-product-smoke handoff after recovery: Passed
- Unified candidate currently installed on the watch: Yes

The merged Launcher/Assistant firmware now boots one application image, starts Xiaozhi in-process, connects to the server, runs microphone and playback, and returns to Launcher without rebooting. The 50-round mixed-profile release gate, complete-Flash recovery exercise, and final all-application smoke test have now passed on the connected watch.

## Automated evidence

The formal ESP-IDF 5.5.4 build completed after the MCP ownership fix. The exact application image currently installed after the final current-source follow-up is:

| Evidence | Value |
|---|---:|
| Application image SHA256 | `0bddd1cbe4502d803e19e71ba1e99efdd8c5e470659dcdefdf1df300cd86b0c7` |
| Application bytes | `6,036,080` (`0x5c1a70`) |
| Application partition bytes | `7,864,320` (`0x780000`) |
| Application headroom | `1,828,240` bytes (`23.25%`) |
| Application hard gate | `6,684,672` (`0x660000`) |
| Assets image SHA256 | `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` |
| Assets bytes | `291,198` |
| Static DIRAM bytes | `191,275` |
| Capacity gate | Passed |

The final image remains 23.25% below the partition limit and does not contain a separately bootable Assistant application. `generated_assets.bin` remains a separate Flash partition artifact but is written and recovered as part of the same full-firmware release.

The targeted Python contracts cover Assistant lifecycle, memory ownership, trace parsing, product smoke, boot validation, unified Flash tooling, shared Wi-Fi, and audio behavior. Host-side C++ contracts cover the runtime state machine, diagnostics, debug commands, Wi-Fi policy, and audio jitter-buffer policy.

The release tooling includes:

- `validate_assistant_graceful_close.py`: balanced listening, recording, TTS, reconnect, and completed-conversation exits with heap, task, resource, reset, and two-second stop gates;
- `validate_product_smoke.py`: automated open/close/Launcher round trips for Codex, Clock, Badge, IMU, Audio.FFT, and Settings;
- `validate_watch_boot.py`: hard reset, fatal-marker, and Launcher boot validation;
- `exercise_full_flash_recovery.sh`: restore the verified 16 MiB backup, validate the restored firmware, reinstall the unified candidate, and validate it again;
- `run_unified_hardware_validation.sh`: candidate flash, boot, 10 cycles, product smoke, 50 cycles, full-Flash recovery, and final smoke.

## Targeted hardware evidence

| Field | Result |
|---|---|
| Device | ESP32-S3 watch, MAC `44:1b:f6:c1:86:5c`, serial port `/dev/cu.usbmodem1101` in the final run |
| Complete Flash backup | 16 MiB `backups/watch-full-flash-20260718-173922.bin`; SHA256 `b6ecc3da8f0dbd41de9722a1956566ca3ce0f5506e6439fe5c4a57366c9000dc` |
| Automated control | USB debug commands opened Assistant, returned Home, queried status, and collected causal traces without human tapping |
| Wi-Fi/server | Wi-Fi acquired `192.168.50.238`; activation succeeded; MCP initialize/tools-list and the audio channel completed |
| Assistant startup | Reached `first-running`; decoder, encoder, WakeNet, playback, microphone upload, and server session were active |
| Graceful Home | Agent, Audio, Device, SNTP, Wi-Fi, capture, playback, bindings, and subscriptions stopped |
| Stopped resources | Bindings/subscriptions/Wi-Fi/capture/playback all zero; task delta returned to zero after warm-up |
| 10-cycle gate | Passed; all five profiles ran twice; cumulative internal loss 284 B; peak internal loss 0 B; peak PSRAM loss 464 B; maximum stop 307 ms |
| 20-cycle gate | Passed; 20 measured sessions, all five profiles ran four times; cumulative internal loss -4 B; peak PSRAM loss 264 B; maximum stop 314 ms |
| 50-cycle gate | Passed; 50 measured sessions, all five profiles ran ten times; cumulative internal loss -20 B; peak internal loss 0 B; peak PSRAM loss 320 B; maximum stop 314 ms |
| Product smoke | Passed before and after recovery for Codex, Clock, Badge, IMU, Audio.FFT, and Settings |
| Full-Flash recovery | Passed; verified backup restored and booted, then the unified candidate was reinstalled, hash-verified, and booted to Launcher |
| Final handoff | Passed a hard-reset Launcher boot followed immediately by six-application smoke; the cross-session debug parser synchronization correctly isolated a stale boot probe |

### Branch-advancing follow-up and muted automation

After the continuous 50-cycle release gate, shared Wi-Fi work continued landing on the branch while the follow-up campaign was running. Per the test instruction, counting continued across two reflashes instead of restarting: 18 sessions on the first coherent build, 11 on the next build, then 21 on the final `cd5b72d-dirty` build. The reflashes included the branch HEAD available at each build as well as the debug volume/mute changes, so the combined 50 sessions are workload-continuity evidence, not a claim that one final binary ran 50 uninterrupted sessions. The profile schedule continued across the boundaries, so listening, recording, TTS, reconnect, and completed conversation each ran exactly ten times. No reset, panic, watchdog, or stuck close occurred in the combined run.

The reflashes reset the heap baseline, so a single cumulative heap-loss value across all 50 sessions would be misleading. The exact final image's 21-session segment passed with cumulative internal loss 176 B, peak internal loss 0 B, peak PSRAM loss 332 B, and maximum stop time 293 ms. Its summary is `build-unified/hardware-validation/current-assistant-final-21-muted.json`. The exact current image then passed hard-reset boot validation and the six-application product smoke again. The earlier single-image continuous 50-cycle release gate remains the uninterrupted long-run evidence.

The follow-up also found that Brookesia could treat `SetVolume(0)` as a no-op when its desired volume already defaulted to zero, even though the hardware player path had not been muted. Assistant startup now synchronizes both volume and mute state (`SetMute(true)` when the HAL volume is zero). The hardware log confirms both calls on every muted lifecycle, and the watch remains configured at volume zero after validation.

The first lifecycle after a cold boot is treated as warm-up because Xiaozhi creates persistent shared infrastructure and therefore changes the task and heap baseline. Every measured steady-state session used the post-warm-up baseline. The second stop gate passed, as did all later steady-state gates.

### MCP leak diagnosis and correction

Heap tracing isolated the earlier approximately 20.7 KiB PSRAM loss per Assistant lifecycle to the MCP engine allocated by `esp_mcp_create()`. Brookesia passed that engine to `esp_xiaozhi_chat` as borrowed memory, so chat teardown did not destroy it.

The build overlay now sets `chat_config.owns_mcp_engine = true`. Native `esp_xiaozhi_chat_deinit()` consequently destroys the session engine only after its MCP runtime users stop. Temporary heap-tracing instrumentation was removed and heap tracing was disabled again in the release configuration. The 10- and 20-cycle results above show the leak has dropped from roughly 21 KiB per cycle to a bounded few hundred bytes of PSRAM variation, with no cumulative internal-heap loss.

The causal shutdown sequence remained `close-request -> stopping -> stopped callback -> release -> closed`; Launcher reopened every time. No reset, panic, or watchdog marker occurred during the 50 measured sessions.

A GMF `Got NULL Pointer` diagnostic still appears during Audio deinitialization. It is currently classified as a non-fatal upstream teardown warning because shutdown continues, all resource owners reach zero, the gate passes, Launcher reopens, and no reset follows.

The earlier audible stutter was addressed by buffering playback in PSRAM, starting after a 180 ms prebuffer, draining it on a dedicated task, and separating capture/playback I2S locks. The user subsequently confirmed normal Assistant voice.

## Compatibility and lifecycle matrix

| Capability | Status | Evidence or remaining check |
|---|---|---|
| Existing activation | Passed | Server activation completed without a new user-visible identity |
| Listen/server session | Passed | Agent and server audio channel opened |
| Microphone upload | Passed | Encoder/recorder and continuous audio transfer observed |
| MCP initialization | Passed | Initialize and tools/list received |
| TTS audio | Passed manual | User confirmed normal voice after the jitter-buffer fix |
| Stop while listening/recording/TTS/reconnecting/conversation | Passed | Each profile completed ten times in the 50-cycle campaign |
| Repeated entry | Passed | 50 measured mixed-profile sessions, after a separate warm-up |
| Post-stop runtime residue | Passed | Owners zero, task delta zero, steady-state gates true |
| Reboot/crash | Passed | None observed in lifecycle, product-smoke, or recovery validation |
| Wake phrase `你好小智` | Pending manual | Speak the phrase and observe WakeStart |
| User text and emotion/display | Pending manual | Observe transcription and UI presentation |

## Remaining manual hardware execution

The automated hardware release gate is complete. The following experience and physical-control checks remain manual:

- exercise the exact two-button physical gesture once; the same application close path is covered through USB automation, but the switches were not robotically actuated;
- manually confirm spoken wake phrase, recognized text, and the full assistant text/emotion display matrix;
- separately observe boot sound, display correctness, sleep/wake, power behavior, and BLE/Wi-Fi coexistence.

The default `partitions.csv` is now the single-application layout. The duplicate shadow table and obsolete Assistant-only/boot-slot tools have been removed. This phase has no remote update or automatic rollback; recovery continues to use the verified complete Flash backup and a USB full-Flash write.
