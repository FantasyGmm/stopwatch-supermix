# stopwatch-supermix

`stopwatch-supermix` is a custom M5Stack StopWatch firmware that turns the device into a compact launcher for daily tools, a Xiaozhi assistant boot target, and a dedicated Codex status monitor.

This project is based on the M5Stack StopWatch user demo and extends it with:

- a launcher-first app shell built on Mooncake/LVGL;
- a Codex status monitor app added as the first launcher app;
- an in-process Assistant app backed by Xiaozhi/Brookesia services, without rebooting;
- full-screen utility apps for clock, badge, IMU, FFT, and setup;
- backup-first build, flash, lifecycle, product-smoke, and recovery tooling.

## Hardware Target

- Device: M5Stack StopWatch
- Chip: ESP32-S3
- Flash layout: the unified candidate uses one 7.5 MiB factory application, FAT storage, SPIFFS assets, and coredump partitions on 16 MB Flash
- Display/UI stack: LVGL 9, M5GFX, Mooncake, Smooth UI Toolkit
- Toolchain: ESP-IDF v5.5.4

## App Layout

The launcher installs apps in this order:

1. Codex status monitor
2. Assistant
3. Clock
4. Badge
5. IMU
6. FFT
7. Setup

The Codex app is intentionally placed first so the device can boot into the launcher and quickly open the monitoring dashboard.

Clock opens a three-button selection page for Alarm, Watch Face, and Stopwatch.
Each feature retains its original display and controls. Hold the upper-left button
to return from a Clock feature to the selection page; hold both buttons to return
directly to the launcher.

## Codex Status Monitor

The Codex monitor is a full-screen dashboard for a local Codex status stream. It shows:

- current Codex model;
- reasoning effort;
- remaining 5-hour usage quota;
- remaining weekly usage quota;
- current-day Codex conversation progress alerts;
- pending, running, and completed conversation counts with a three-second carousel;
- battery level and charging state;
- connection state for BLE or the Wi-Fi/WebSocket fallback.

By default, the monitor advertises a BLE peripheral named `StopWatch-Codex`. A
manually opened Mac menu bar agent reads local Codex state and sends a compact
snapshot to the watch every two seconds. Install it with:

```bash
./mac-codex-agent/install_launch_agent.sh
```

Open the Codex app on the watch to start advertising. The Mac agent scans,
pairs using BLE Just Works, reconnects automatically, and writes only rendered
status fields. It does not send OAuth credentials, prompts, responses, or
source code to the watch.

After every successful BLE connection, the Mac agent first sends the current
Unix time and effective UTC offset. The watch applies them to its global system
clock, persists the fixed-offset timezone, and writes the RX8130 RTC, so the
same time is used by Launcher, Clock, alarms, and other applications and is
restored after reboot. A reconnect refreshes the offset for timezone changes.

The monitor maps `task_started` to Running. A `task_complete` response that asks
for confirmation, information, or another action becomes Pending; a normal
completion becomes Completed. Only Pending triggers a chime. The watch shows
only project, current short conversation title, and `正在处理`, `等待指令`, or
`已完成`. Pending records have priority and rotate exclusively; when Pending is
empty, Running records rotate. With neither active state, static `COMPLETED` is
green. Only the latest current-day state of each conversation is displayed.

The two upper arcs display the model and uppercased reasoning effort directly.
They do not show separate `MODEL`, `EFFORT`, `auto`, or `reason` captions.

Launcher and the Codex monitor share the same edge controls. Drag vertically on
the left side for brightness or the right side for volume. Changes apply
globally and persist across apps and reboots after release.

If BLE has not connected after 15 seconds, `auto` mode starts the existing
Wi-Fi/WebSocket fallback at:

```text
ws://<host>:8765/
```

The default host compiled into the firmware is:

```text
Ethan-MacBook-Air.local
```

At runtime, the app reads the host and transport configuration from the
`codex_status` settings namespace. If no transport setting exists, it uses
`auto`, which means BLE first and Wi-Fi fallback after 15 seconds.

Expected status payload:

```json
{
  "codex_model": "gpt-5",
  "codex_effort": "medium",
  "codex_5h_left_pct": 72,
  "codex_week_left_pct": 64,
  "codex_today_min": 38,
  "codex_week_min": 214
}
```

Supported chime payload:

```json
{
  "type": "chime",
  "agent": "codex"
}
```

When Pending triggers a chime event, the monitor plays the complete built-in
startup sound at the global speaker volume. Setting volume to zero keeps
Pending silent.

## Assistant Runtime

Assistant runs inside the same firmware process as Launcher and the other Mooncake applications. Opening Assistant starts the Xiaozhi services through `RuntimeSupervisor`; closing it stops the Agent, audio, network, and service bindings asynchronously, releases their owners, and returns to Launcher without rebooting.

Runtime responsibilities are intentionally separated:

- Mooncake owns application installation, UI, and navigation;
- Brookesia owns Assistant services and the Xiaozhi Agent;
- the StopWatch HAL owns display, buttons, microphone, speaker, and other hardware;
- `ResourceSupervisor` and the shared Wi-Fi/audio services arbitrate resources between apps.

The default [`partitions.csv`](partitions.csv) contains one 7.5 MiB factory application at `0x20000`; generated wake/audio assets remain in a separate `assets` partition at `0x8a0000`. Assets are part of the same full-firmware release even though they are not linked into the application binary. The old dual-OTA layout and Assistant-only slot tools have been removed.

This first-phase layout has no remote firmware update and no automatic rollback. Installations update the bootloader, partition table, unified application, and assets together over USB. Returning to an older release requires restoring a verified complete 16 MiB Flash backup over USB.

## Navigation

The launcher and apps use the shared key manager. Long-pressing both side buttons triggers the `GoHome` event, which closes the current app and returns to the launcher. In Assistant, the same event starts a one-shot graceful shutdown; the UI remains responsive on `Closing` until the runtime reports `Stopped`.

## Repository Structure

```text
.
├── main/
│   ├── apps/                         # Launcher apps
│   │   ├── app_codex_status/         # Codex status monitor
│   │   ├── app_assistant/            # In-process Xiaozhi Assistant UI/lifecycle
│   │   ├── app_launcher/             # App launcher shell
│   │   └── ...
│   └── main.cpp                      # Firmware entrypoint and app installation order
├── mac-codex-agent/                  # macOS Codex collector and BLE central
├── patches/                          # Dependency/runtime patches
├── scripts/
│   ├── build_unified_candidate.sh    # Isolated unified build and size gates
│   ├── flash_unified_candidate.sh    # Backup-gated full candidate write
│   ├── run_unified_offline_validation.sh
│   └── run_unified_hardware_validation.sh
├── partitions.csv                    # Default unified 16 MB layout
├── repos.json                        # External component list
└── fetch_repos.py                    # Dependency fetch/update helper
```

Generated folders such as `build/`, `managed_components/`, `components/`, and `third_party/` are not intended to be committed.

## Dependencies

Fetch source components at the exact commits recorded in `repos.json`:

```bash
python3 ./fetch_repos.py
```

Verify the local source trees without downloading or changing them:

```bash
python3 ./fetch_repos.py --check
```

The source component list includes Mooncake, Mooncake Log, Smooth UI Toolkit,
M5GFX, ArduinoJson, LVGL, M5IOE1, M5PM1, and BMI270/BMM150 sensor support.
Every checkout is locked to an immutable Git commit. Project patches are also
hash-checked and a patch that no longer applies is a hard failure.

ESP-IDF registry dependencies, including Brookesia, XiaoZhi Agent, ESP-SR, and
ESP WiFi Connect, are declared in `main/idf_component.yml` and resolved by the
tracked `dependencies.lock`. The generated `managed_components/` directory is
disposable.

The unified build does not require a complete standalone XiaoZhi checkout. It
tracks only the verified asset builder in `tools/xiaozhi/` and the required CJK
font in `main/assets/fonts/`; provenance and SHA256 values are recorded in
`third_party_notices/xiaozhi-build-inputs.md`.

### Dependency upgrades

- Upgrade a source component by resolving the intended release to a commit,
  updating `repos.json`, refreshing any patch hash, and rebuilding from a clean
  dependency tree.
- Upgrade the Assistant runtime by updating the Brookesia or XiaoZhi Agent
  versions in `main/idf_component.yml`, regenerating `dependencies.lock`, and
  running Assistant lifecycle and hardware regression tests.
- Upgrade the XiaoZhi asset builder or font by replacing only the tracked input,
  updating its provenance and SHA256, and passing the clean firmware build.

## Build

Install and export ESP-IDF v5.5.4 first:

```bash
source /path/to/esp-idf-v5.5.4/export.sh
```

Fetch dependencies:

```bash
python3 ./fetch_repos.py
```

Build the isolated unified candidate and enforce its capacity gates:

```bash
./scripts/build_unified_candidate.sh --clean
```

The release output is written to `build-unified/`. A normal root `idf.py build` now uses the same default single-application partition table, while the release script additionally enforces application/assets capacity gates and emits reproducible metrics.

## Flash

Before the first unified write, create and retain a complete 16 MiB backup:

```bash
./scripts/backup_watch_flash.sh \
  --port /dev/cu.usbmodem1101 \
  --output backups/watch-full-flash.bin
```

Flash the application, partition table, bootloader, and assets together. The script rejects a missing, short, or checksum-mismatched backup before writing:

```bash
./scripts/flash_unified_candidate.sh \
  --port /dev/cu.usbmodem1101 \
  --backup backups/watch-full-flash.bin
```

Run the complete hardware release gate (10 mixed lifecycles, all-app smoke, 50 mixed lifecycles, full-Flash restore exercise, and final smoke):

```bash
./scripts/run_unified_hardware_validation.sh \
  --port /dev/cu.usbmodem1101 \
  --backup backups/watch-full-flash.bin
```

## BLE And Wi-Fi Setup

BLE mode needs no watch-side network configuration. Install the Mac agent,
enable Bluetooth on the Mac, then open the Codex app on the watch. The installer
creates `StopWatch Codex Agent.app`; click **Allow** when macOS asks whether
that app may use Bluetooth. There is no need to add Python manually in System
Settings. If permission was previously denied, run
`tccutil reset Bluetooth com.stopwatch.codex-agent` and reinstall the agent.

The monitor uses the shared Wi-Fi manager only when the automatic fallback
starts or `wifi` mode is selected. If no saved SSID exists, the device starts a
configuration AP with the `StopWatch` prefix. Connect to that AP and complete
Wi-Fi setup before using the WebSocket fallback.

Connection states shown by the monitor:

- `BLE PAIR`: advertising and waiting for the Mac agent;
- `BLE WAIT`: connected but waiting for a valid status snapshot;
- `BLE LIVE`: receiving live status over BLE;
- `BLE ERR`: BLE initialization failed;
- `WIFI`: automatic fallback is starting;
- `SETUP`: configuration AP is active;
- `SCAN`: station mode is trying to connect;
- `CONNECT`: Wi-Fi is connected and the monitor is preparing WebSocket;
- `WAITING`: WebSocket connected, waiting for status JSON;
- `LIVE`: status JSON received;
- `RETRY` or `WS ERR`: WebSocket connection failed or disconnected.

## Codex Monitor Configuration

The monitor host can be changed in firmware by updating the default host in `main/apps/app_codex_status/app_codex_status.cpp`.

The runtime setting key is:

```text
namespace: codex_status
key:       host
```

The same namespace supports:

```text
key:       transport
values:    auto | ble | wifi

key:       ble_name
default:   StopWatch-Codex
```

If you add a settings UI or provisioning command later, write the host value there to avoid recompiling firmware for each workstation.

## Troubleshooting

### Assistant initialization fails or the watch reboots

A reboot is not expected when opening or closing the unified Assistant. Capture the causal trace and runtime resource status over USB:

```bash
./scripts/watch_debug.py --port /dev/cu.usbmodem1101 assistant trace
./scripts/watch_debug.py --port /dev/cu.usbmodem1101 assistant status
```

For a repeatable open/close diagnosis, use `validate_assistant_graceful_close.py`; it fails on reset/watchdog markers, a shutdown over two seconds, retained resources/tasks, more than 4 KiB single-session internal-heap loss, or more than 8 KiB cumulative campaign loss.

### Codex monitor shows SETUP or SCAN

Wi-Fi is not connected. Complete Wi-Fi setup through the device configuration AP, then make sure the Codex status server is reachable from the same network.

### Codex monitor stays on BLE PAIR

Check that Bluetooth is enabled and the Mac agent is loaded:

```bash
launchctl print "gui/$UID/com.stopwatch.codex-agent"
tail -f "$HOME/Library/Logs/stopwatch-codex-agent.log"
```

If macOS has an obsolete bond, remove `StopWatch-Codex` from Bluetooth settings
and reopen the watch app so the agent can pair again.

### Codex monitor shows WAITING

The WebSocket connection is established, but no status JSON has arrived yet. Verify that the server is sending text frames matching the expected schema.

## Acknowledgements

- Based on the M5Stack StopWatch user demo.
- Uses Mooncake, LVGL, M5GFX, Smooth UI Toolkit, and ESP-IDF.
- The Codex pet/status visual treatment includes a local integration inspired by the open-source `EthanM2025/meme` project.

## License

This project keeps the original MIT license from the M5Stack StopWatch user demo. Review third-party component licenses before redistribution.
