# Codex Status BLE Design

## Goal

Make the StopWatch Codex application useful without requiring the watch and Mac to share a Wi-Fi network. A Mac-side status agent will collect Codex model, reasoning effort, quota, activity, and completion events, then deliver them to the watch over Bluetooth Low Energy (BLE). The existing Wi-Fi WebSocket transport remains available as a fallback.

## Upstream Baseline

The source project is `EthanM2025/meme`. Its latest `main` revision checked on 2026-07-11 is `9067f2311b4f228135dccccfa24d7a8483188ec1`, authored on 2026-06-04. The current StopWatch port was committed on 2026-06-15, and its Codex pet asset files have the same SHA-1 hashes as the upstream files. There is no newer upstream firmware revision to merge.

The upstream source has been downloaded to a temporary checkout for reference. Only the Codex status collection logic will be adapted. Claude integration, speech recognition, push-to-talk audio, text injection, and the upstream standalone firmware are outside this change.

## Scope

This change includes:

- a focused Mac status agent stored in this repository;
- Codex state collection adapted from upstream `codex_adapter.py`;
- native task-completion detection adapted from upstream `server.py`;
- a BLE Central client on the Mac;
- a NimBLE GATT peripheral in the StopWatch launcher firmware;
- a transport-independent status model shared by BLE and WebSocket input;
- BLE-first connection behavior with delayed Wi-Fi WebSocket fallback;
- setup scripts and documentation for running the Mac agent at login.

This change does not alter the Xiaozhi assistant image in `ota_1`. BLE runs only in the launcher image in `ota_0` and stops when the Codex app closes or the device switches to the assistant image.

## Architecture

The Mac remains the source of truth because Codex state is stored on the Mac and the watch cannot directly access Codex databases or OAuth credentials.

```text
Codex files and quota API
          |
          v
Mac CodexStatusCollector
          |
          v
CodexStatusSnapshot JSON
          |
          +---- BLE Central writer ----> StopWatch NimBLE GATT server
          |
          +---- optional WebSocket ----> existing StopWatch WS client
                                            |
                                            v
                                  CodexStatusModel/parser
                                            |
                                            v
                                      LVGL dashboard
```

The firmware UI must not know whether a payload arrived through BLE or WebSocket. Both transports submit complete JSON messages to the same parser and UI update path.

## Mac Status Agent

The Mac agent lives under `mac-codex-agent/` and uses Python 3.9 or newer.

Responsibilities:

1. Read the current Codex model and reasoning effort from `~/.codex/logs_2.sqlite`, with `~/.codex/config.toml` as fallback.
2. Read the OAuth access token from `~/.codex/auth.json` and query the Codex quota endpoint with a 60-second cache.
3. Track time while Codex is the frontmost macOS application and persist day/week counters in `~/.codex/.stopwatch_usage.json`.
4. Read the latest native `task_complete` event from the rollout referenced by `~/.codex/state_5.sqlite`.
5. Emit a compact JSON status snapshot every two seconds.
6. Emit `{"type":"chime","agent":"codex"}` once for each newly observed completion.
7. Scan for the StopWatch BLE service, connect, discover the write characteristic, and reconnect with bounded exponential backoff.

The agent uses `bleak` for the first implementation because it exposes macOS CoreBluetooth while keeping the upstream Python collector reusable. A LaunchAgent installer starts it at login and sends logs to `~/Library/Logs/stopwatch-codex-agent.log`.

No Codex OAuth token or database content is sent to the watch. Only the rendered status fields are transmitted.

## BLE Roles And GATT Service

The StopWatch is the BLE peripheral. The Mac is the BLE central. This lets the watch advertise a stable service while the Mac agent handles discovery and reconnects.

Device name: `StopWatch-Codex`

Service UUID: `7a230001-5474-4f50-5741-544348434f44`

Characteristics:

| Name | UUID | Properties | Purpose |
| --- | --- | --- | --- |
| Status RX | `7a230002-5474-4f50-5741-544348434f44` | write, write-without-response | Mac sends framed JSON messages to the watch |
| Device TX | `7a230003-5474-4f50-5741-544348434f44` | read, notify | Watch exposes protocol version, connection state, and optional future control events |

The initial security mode uses BLE bonding with Just Works pairing. This encrypts bonded reconnects but does not claim man-in-the-middle protection because the watch has no numeric comparison workflow. The status payload contains no credentials or source text.

## Framing Protocol

BLE writes are limited by the negotiated ATT payload size, so the protocol must not assume that a JSON message fits in one write.

Every fragment starts with a six-byte header:

| Byte | Field |
| --- | --- |
| 0 | protocol version, initially `1` |
| 1 | message type: `1=status`, `2=event` |
| 2-3 | little-endian sequence number |
| 4 | zero-based fragment index |
| 5 | total fragment count |

The remaining bytes are UTF-8 JSON. The Mac chooses a fragment size from the characteristic's maximum write length. The firmware accepts messages up to 2048 bytes, requires ordered fragments with matching sequence/type/count, and discards incomplete messages after three seconds. Complete payloads are queued for processing from the app task rather than parsed inside the NimBLE callback.

Writes use response mode initially to guarantee ordering and surface failures. The two-second update interval is low enough that throughput is not a concern.

## Firmware Components

The current `AppCodexStatus` file mixes view creation, network lifecycle, WebSocket callbacks, JSON parsing, and status state. The change introduces focused units:

- `codex_status_model.*`: parsed status values, payload validation, and chime event classification;
- `codex_status_transport.h`: transport callback contract for complete payloads and connection state;
- `codex_ble_transport.*`: NimBLE peripheral, GATT callbacks, fragment reassembly, and an application queue;
- existing `app_codex_status.*`: lifecycle orchestration and LVGL rendering;
- existing WebSocket code: retained as the fallback transport and routed through the common model.

`AppCodexStatus::onOpen()` starts BLE advertising immediately. If no BLE central connects within 15 seconds, the app starts the existing Wi-Fi/WebSocket path. A live BLE connection has priority. If BLE connects after WebSocket starts, the WebSocket is stopped. If BLE disconnects, advertising resumes and Wi-Fi fallback remains available.

`AppCodexStatus::onClose()` stops advertising, disconnects BLE, releases the NimBLE host resources owned by the app, stops WebSocket activity, deletes timers, and destroys the view. This app-scoped lifecycle is intentional: it minimizes interference with the launcher and the assistant OTA handoff. Completion chimes are therefore available only while the Codex app is open.

## User Interface States

The existing dashboard layout remains unchanged. The connection label gains these states:

- `BLE PAIR`: advertising and waiting for the Mac;
- `BLE LIVE`: BLE connected and at least one valid status payload received;
- `BLE ERR`: BLE initialization or protocol error;
- `WIFI`: BLE fallback timer elapsed and Wi-Fi is connecting;
- existing `SETUP`, `SCAN`, `WAITING`, `LIVE`, `RETRY`, and `WS ERR` states for WebSocket fallback.

The host label shows `StopWatch-Codex` in BLE mode and the configured hostname in Wi-Fi mode. Received values are marked stale after ten seconds without a valid status payload; the connection label changes from a live state to a waiting/retry state while keeping the last values visible.

## Configuration

NVS namespace `codex_status` retains the existing `host` key and adds:

- `transport`: `auto`, `ble`, or `wifi`; default `auto`;
- `ble_name`: optional device-name override; default `StopWatch-Codex`.

`auto` means BLE first with Wi-Fi fallback after 15 seconds. `ble` never starts Wi-Fi. `wifi` preserves current behavior and does not initialize Bluetooth.

The first implementation does not add an on-watch settings editor. Values can be changed through a maintenance command/script, while the defaults provide the intended behavior.

## Error Handling

- Malformed JSON is ignored and does not change the live timestamp.
- Unsupported protocol versions are rejected and reported as `BLE ERR`.
- Oversized messages, duplicate fragments, out-of-order fragments, and fragment-count changes reset the in-progress message.
- A Mac disconnect causes the watch to resume advertising.
- A watch disconnect causes the Mac agent to rescan with exponential backoff capped at 30 seconds.
- Quota API failures retain cached quota values and continue sending model/activity fields.
- Missing Codex files produce partial snapshots instead of terminating the agent.
- The agent never replays the completion that existed when it first started.

## Resource And OTA Boundaries

Bluetooth is currently disabled in the launcher `sdkconfig`; NimBLE and the ESP32-S3 BLE controller must be enabled. The current launcher image has approximately 18 percent free space in its application partition. The final implementation is accepted only if the image still fits the `ota_0` partition and boots without a reset loop.

The Xiaozhi assistant has its own image and configuration in `ota_1`. No BLE component is linked into that image. Shared NVS keys use the `codex_status` namespace and must not overwrite assistant settings.

## Verification

Mac tests:

- unit tests for framing and reassembly vectors;
- collector tests using temporary SQLite/JSONL/auth fixtures and mocked HTTP responses;
- completion detector test proving startup does not replay an old completion;
- disconnect/reconnect test with a mocked Bleak client.

Firmware verification:

- build with ESP-IDF 5.5.4 and record binary size;
- validate BLE-only, Wi-Fi-only, and automatic fallback modes;
- enter and leave the Codex app repeatedly using the two-button long press;
- switch to the assistant image and back without initialization hangs;
- reboot the watch and Mac and confirm automatic reconnect;
- leave the monitor running for at least 30 minutes and confirm no restart loop;
- trigger a real Codex completion and confirm exactly one 660 Hz chime.

## Acceptance Criteria

The change is complete when:

1. The Mac agent starts without the upstream speech/Claude dependencies.
2. Opening Codex on the watch advertises `StopWatch-Codex` and receives a status snapshot over BLE.
3. Model, effort, quotas, day/week minutes, battery, and completion chime work as before.
4. Wi-Fi WebSocket remains usable as an explicit mode and automatic fallback.
5. Two-button long press always returns to Launcher and releases transport resources.
6. Assistant OTA handoff and post-reboot initialization still work.
7. The firmware fits the partition and passes the long-running reset-loop check.
