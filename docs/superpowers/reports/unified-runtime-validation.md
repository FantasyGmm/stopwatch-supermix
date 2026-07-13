# Unified Runtime Validation

- Automated tests: Passed (114 Python tests and 5 host C++ test binaries)
- Unified clean build: Passed (isolated clean build and capacity gates)
- Hardware compatibility: Pending hardware
- Lifecycle coexistence: Pending hardware
- 100-cycle stress: Pending hardware
- Full product regression: Pending hardware

This report deliberately separates reproducible offline evidence from evidence that can only be collected on the watch. `scripts/run_unified_offline_validation.sh` completed successfully on 2026-07-14 from committed source `b3e8952e2a993fc3da78c882bcdce4a99dad4008`. The run passed 114 Python tests, all 5 host C++ test binaries, the isolated unified clean build, both image-size gates, and `git diff --check`. A successful offline run does not change any hardware status.

## Automated evidence

The values below are copied from the clean build's generated `build-unified/unified-runtime-metrics.json` (schema 2). They are reproducible build evidence, not hardware claims.

| Evidence | Value | Source |
|---|---|---|
| Firmware Git SHA | `b3e8952e2a993fc3da78c882bcdce4a99dad4008` | `source_git_commit` |
| Application image SHA256 | `bfb0a6a8098c71aac11e916ba3e8507cee36014390c9591c4e70bd0d965c3a28` | `app_sha256` |
| Application bytes | `6,005,520` (`0x5ba310`) | `app_bytes` |
| Application partition bytes | `0x780000` (7,864,320) | `partitions_unified.csv` |
| Application headroom | `1,858,800` bytes (`0x1c5cf0`, 23.64%) | `app_headroom_bytes` and `app_headroom_percent` |
| Application hard gate | `0x660000` (6,684,672) | candidate build command |
| Assets image SHA256 | `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f` | `assets_sha256` |
| Assets bytes | `291,198` | `assets_bytes` |
| Assets hard gate | `0x400000` (4,194,304) | candidate build command |
| Static DIRAM bytes | `200,931` | `diram_bytes` |
| Capacity gate | Passed | `capacity_gate_passed: true` |

### Resolved Brookesia versions

These versions are resolved by the checked-in `dependencies.lock`; the clean candidate configuration processed the same locked versions.

| Component | Resolved version |
|---|---:|
| `espressif/brookesia_agent_helper` | 0.7.3 |
| `espressif/brookesia_agent_manager` | 0.7.5 |
| `espressif/brookesia_agent_xiaozhi` | 0.7.4 |
| `espressif/brookesia_hal_interface` | 0.7.5 |
| `espressif/brookesia_lib_utils` | 0.7.9 |
| `espressif/brookesia_mcp_utils` | 0.7.1 |
| `espressif/brookesia_service_audio` | 0.7.7 |
| `espressif/brookesia_service_device` | 0.7.1 |
| `espressif/brookesia_service_helper` | 0.7.10 |
| `espressif/brookesia_service_manager` | 0.7.8 |
| `espressif/brookesia_service_nvs` | 0.7.4 |
| `espressif/brookesia_service_sntp` | 0.7.2 |

### Repository boundary and handoff

- The Assistant product path contains no `esp_ota_set_boot_partition`, `esp_restart`, or `ota_1` reference.
- The new backup, restore, and unified-candidate helpers contain no `erase_flash` command.
- The checked-in legacy `partitions.csv` still contains both `ota_0` and `ota_1`, and the legacy Assistant flash path and complete-backup restore helper remain available. The dual-OTA recovery path has not been removed or promoted away.
- No firmware was flashed, no serial port was accessed, and no device command was run while collecting this evidence.
- After the offline validation is rerun with the watch available, the first hardware action in Stage 2 is to create and verify a complete 16 MiB Flash backup and its SHA256 sidecar. No candidate write may occur before that backup passes verification.

## Hardware evidence

No hardware evidence has been collected. Every row below stays `Pending hardware` until its described observation is recorded against the tested device, firmware Git SHA, and image SHA256.

### Xiaozhi compatibility matrix

| Capability | Status | Required evidence |
|---|---|---|
| Existing activation or approved reactivation | Pending hardware | Activation log and identity comparison |
| Wake phrase `你好小智` | Pending hardware | Local WakeStart event |
| Manual start/listen | Pending hardware | Listening event and server session |
| User text | Pending hardware | UI callback log containing recognized user text |
| Assistant text and emotion | Pending hardware | UI callback log containing text and emotion |
| TTS audio | Pending hardware | Audible, stable playback observation |
| Existing Xiaozhi/MCP product functions | Pending hardware | Function-by-function result |
| Stop while idle | Pending hardware | Stopped within 2 seconds |
| Stop while recording | Pending hardware | Stopped within 2 seconds |
| Stop while playing | Pending hardware | Stopped within 2 seconds |
| Stop while reconnecting | Pending hardware | Stopped within 2 seconds |
| Post-stop tasks and connections | Pending hardware | Zero Agent session residue |

### 10-cycle targeted lifecycle and coexistence test

| Scenario | Status | Required evidence |
|---|---|---|
| Exit while Starting | Pending hardware | Current-session Stopped acknowledgement and no delayed Start |
| Exit while Listening | Pending hardware | Stop latency, owners released, and successful second entry |
| Exit while recording | Pending hardware | Stop latency, capture owner `None`, and Clock opens immediately |
| Exit during TTS playback | Pending hardware | Stop latency, playback owner `None`, and Codex remains functional |
| Exit during Wi-Fi/Xiaozhi reconnect | Pending hardware | Stop latency, Assistant Wi-Fi lease absent, and Badge AP starts |
| Ten mixed open/close cycles | Pending hardware | Per-cycle before/running/after diagnostics and zero reset-reason change |
| BLE and Wi-Fi coexistence | Pending hardware | BLE remains connected while Assistant uses Wi-Fi |

### 100-cycle stress and leak gates

| Gate | Status | Required evidence |
|---|---|---|
| 20 idle/listening exits | Pending hardware | Cycle log and stop latency per exit |
| 20 recording exits | Pending hardware | Cycle log and capture release per exit |
| 20 TTS playback exits | Pending hardware | Cycle log and playback release per exit |
| 20 reconnect exits | Pending hardware | Cycle log and Wi-Fi lease release per exit |
| 20 completed conversations | Pending hardware | Cycle log and normal stopped acknowledgement |
| Crash/watchdog/reset count | Pending hardware | Zero events in boot and serial logs |
| Single-session internal heap | Pending hardware | After-stop loss at most 4 KiB from its paired before snapshot |
| Cumulative internal heap | Pending hardware | Loss at most 8 KiB after 100 cycles |
| Runtime residue | Pending hardware | Agent, Audio, activation tasks and subscriptions all zero |
| Resource owners | Pending hardware | Capture and playback owners `None`; Assistant Wi-Fi lease absent |

### Application regression

| Product area | Status | Required evidence |
|---|---|---|
| Launcher and Home | Pending hardware | Boot, navigation, Home behavior, and unchanged reset reason |
| Clock selector, Alarm, Watch Face, Stopwatch | Pending hardware | Function-by-function result |
| Assistant | Pending hardware | Activation, wake, conversation, text, emotion, playback, retry, and return |
| Codex | Pending hardware | BLE status plus Wi-Fi fallback before and after Assistant |
| Badge | Pending hardware | Normal view and configuration AP before and after Assistant |
| IMU, FFT, Setup, and boot sound | Pending hardware | Function-by-function result |
| Display, buttons, and power | Pending hardware | Brightness, controls, sleep, wake, and power observations |
| Cross-service coexistence | Pending hardware | BLE remains connected during Assistant and Codex monitoring resumes after exit |

### Hardware campaign identity

| Field | Status | Required evidence |
|---|---|---|
| Tested device | Pending hardware | Board identity and hardware revision |
| Complete Flash backup | Pending hardware | Exact 16 MiB size and SHA256 sidecar |
| Candidate write | Pending hardware | Four explicit address/artifact pairs and tool output |
| Boot identity | Pending hardware | Reset reason, partition label, firmware Git SHA, and image SHA256 |
| Recovery rehearsal if needed | Pending hardware | Verified full-backup restore result |
