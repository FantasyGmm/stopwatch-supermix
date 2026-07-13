# Unified Runtime Validation

- Automated tests: Passed only when the offline runner succeeds
- Unified clean build: Passed only when candidate build and capacity gates succeed
- Hardware compatibility: Pending hardware
- Lifecycle coexistence: Pending hardware
- 100-cycle stress: Pending hardware
- Full product regression: Pending hardware

This report deliberately separates reproducible offline evidence from evidence that can only be collected on the watch. Run `scripts/run_unified_offline_validation.sh` to reproduce the automated gates. A successful offline run does not change any hardware status.

## Automated evidence

The final handoff updates generated values from `build-unified/unified-runtime-metrics.json`; placeholders are not hardware claims.

| Evidence | Value | Source |
|---|---|---|
| Firmware Git SHA | Pending final handoff | `source_git_commit` |
| Application image SHA256 | Pending final handoff | `app_sha256` |
| Application bytes | Pending final handoff | `app_bytes` |
| Application partition bytes | `0x780000` (7,864,320) | `partitions_unified.csv` |
| Application headroom | Pending final handoff | `app_headroom_bytes` and `app_headroom_percent` |
| Application hard gate | `0x660000` (6,684,672) | candidate build command |
| Assets image SHA256 | Pending final handoff | `assets_sha256` |
| Assets bytes | Pending final handoff | `assets_bytes` |
| Assets hard gate | `0x400000` (4,194,304) | candidate build command |
| Static DIRAM bytes | Pending final handoff | `diram_bytes` |
| Capacity gate | Pending final handoff | `capacity_gate_passed` |

### Resolved Brookesia versions

These versions are resolved by the checked-in `dependencies.lock`; the final handoff verifies that the clean candidate build used the same lock.

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
