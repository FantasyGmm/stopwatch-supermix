# stopwatch-supermix

`stopwatch-supermix` is a custom M5Stack StopWatch firmware that turns the device into a compact launcher for daily tools, a Xiaozhi assistant boot target, and a dedicated Codex status monitor.

This project is based on the M5Stack StopWatch user demo and extends it with:

- a launcher-first app shell built on Mooncake/LVGL;
- a Codex status monitor app added as the first launcher app;
- an Assistant entry that switches into the Xiaozhi runtime stored in the second OTA slot;
- full-screen utility apps for clock, stopwatch, alarm, badge, IMU, FFT, and setup;
- helper scripts for flashing both the launcher firmware and the assistant runtime.

## Hardware Target

- Device: M5Stack StopWatch
- Chip: ESP32-S3
- Flash layout: 16 MB flash, dual OTA application slots, FAT storage partition, SPIFFS assets partition, and coredump partition
- Display/UI stack: LVGL 9, M5GFX, Mooncake, Smooth UI Toolkit
- Toolchain: ESP-IDF v5.5.4

## App Layout

The launcher installs apps in this order:

1. Codex status monitor
2. Assistant
3. Alarm clock
4. Watch face
5. Stopwatch
6. Badge
7. IMU
8. FFT
9. Setup

The Codex app is intentionally placed first so the device can boot into the launcher and quickly open the monitoring dashboard.

## Codex Status Monitor

The Codex monitor is a full-screen dashboard for a local Codex status stream. It shows:

- current Codex model;
- reasoning effort;
- remaining 5-hour usage quota;
- remaining weekly usage quota;
- today's activity minutes;
- weekly activity minutes;
- battery level and charging state;
- connection state for BLE or the Wi-Fi/WebSocket fallback.

By default, the monitor advertises a BLE peripheral named `StopWatch-Codex`. A
Mac LaunchAgent reads local Codex state and sends a compact snapshot to the
watch every two seconds. Install it with:

```bash
./mac-codex-agent/install_launch_agent.sh
```

Open the Codex app on the watch to start advertising. The Mac agent scans,
pairs using BLE Just Works, reconnects automatically, and writes only rendered
status fields. It does not send OAuth credentials, prompts, responses, or
source code to the watch.

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

When the monitor receives the chime event, it plays a short tone on the device.

## Assistant Runtime

The Assistant app does not run the Xiaozhi runtime inside the launcher process. It switches boot to the second OTA application partition (`ota_1`) and restarts the device.

Runtime layout:

- launcher app: `ota_0`, offset `0x20000`;
- assistant app: `ota_1`, offset `0x510000`;
- assistant generated assets: `assets`, offset `0xb00000`.

This separation keeps the launcher small enough to hold local utility apps while allowing the assistant runtime to be built and flashed independently.

## Navigation

The launcher and apps use the shared key manager. Long-pressing both side buttons triggers the `GoHome` event, which closes the current app and returns to the launcher.

For the Assistant app, the return shortcut is available while the launcher-side transition screen is still active. Once the device switches into the assistant OTA slot, navigation is handled by the assistant runtime.

## Repository Structure

```text
.
├── main/
│   ├── apps/                         # Launcher apps
│   │   ├── app_codex_status/         # Codex status monitor
│   │   ├── app_assistant/            # OTA handoff into Xiaozhi runtime
│   │   ├── app_launcher/             # App launcher shell
│   │   └── ...
│   └── main.cpp                      # Firmware entrypoint and app installation order
├── mac-codex-agent/                  # macOS Codex collector and BLE central
├── patches/                          # Dependency/runtime patches
├── scripts/
│   ├── flash_assistant_runtime.sh    # Flash assistant app/assets only
│   ├── flash_full_assistant_stack.sh # Flash launcher + assistant stack
│   └── force_boot_slot.py            # Force ESP-IDF OTA boot slot
├── partitions.csv                    # 16 MB flash partition table
├── repos.json                        # External component list
└── fetch_repos.py                    # Dependency fetch/update helper
```

Generated folders such as `build/`, `managed_components/`, `components/`, and `third_party/` are not intended to be committed.

## Dependencies

Fetch external components:

```bash
python3 ./fetch_repos.py
```

The component list currently includes Mooncake, Mooncake Log, Smooth UI Toolkit, M5GFX, ArduinoJson, LVGL, M5IOE1, M5PM1, and BMI270/BMM150 sensor support.

The assistant runtime is expected to be built separately. By default, the flashing scripts look for it at:

```text
third_party/xiaozhi-esp32/build-m5stack-stopwatch/
```

with these files:

```text
xiaozhi.bin
generated_assets.bin
```

## Build

Install and export ESP-IDF v5.5.4 first:

```bash
source /path/to/esp-idf-v5.5.4/export.sh
```

Fetch dependencies:

```bash
python3 ./fetch_repos.py
```

Build the launcher firmware:

```bash
idf.py build
```

## Flash

Flash only the launcher firmware:

```bash
idf.py -p /dev/cu.usbmodem1101 flash
```

Flash only the assistant runtime:

```bash
scripts/flash_assistant_runtime.sh /dev/cu.usbmodem1101
```

Flash the full launcher + assistant stack:

```bash
scripts/flash_full_assistant_stack.sh /dev/cu.usbmodem1101
```

After flashing the full stack, the script forces the boot slot back to the launcher (`ota_0`) so the device starts from the app launcher.

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

### Assistant stays on the launcher initialization screen

Check that the assistant app is actually flashed into `ota_1` and that `esp_ota_get_partition_description()` can read a valid app descriptor from that partition.

Useful checks:

```bash
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 read_flash 0xd000 0x2000 /tmp/otadata.bin
python /path/to/esp-idf/components/app_update/otatool.py -p /dev/cu.usbmodem1101 read_otadata
```

If the assistant partition is valid but the device should return to the launcher, force boot slot 0:

```bash
scripts/force_boot_slot.py 0 -p /dev/cu.usbmodem1101
```

### Device reboots after entering Assistant

The Assistant entry intentionally calls `esp_ota_set_boot_partition()` and then restarts. A reboot is expected during handoff. A reboot loop usually means the assistant runtime in `ota_1` is missing, oversized, built for the wrong partition layout, or failing early during its own boot.

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
