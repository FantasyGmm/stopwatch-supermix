# Codex Status BLE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver live Codex status and completion events from a Mac to the StopWatch over BLE, with the existing Wi-Fi WebSocket path retained as fallback.

**Architecture:** A focused Python agent collects Codex state and writes framed JSON to a custom BLE GATT characteristic. The launcher firmware acts as a NimBLE peripheral, reassembles frames outside the UI, and routes complete JSON through the existing status renderer. BLE starts when the Codex app opens; automatic mode starts Wi-Fi fallback after 15 seconds without a BLE connection.

**Tech Stack:** Python 3.9, `bleak`, `unittest`, ESP-IDF 5.5.4, NimBLE, FreeRTOS queues, cJSON, LVGL 9.

---

### Task 1: Mac BLE framing protocol

**Files:**
- Create: `mac-codex-agent/protocol.py`
- Create: `mac-codex-agent/tests/test_protocol.py`

- [ ] **Step 1: Write the framing behavior test**

```python
def test_encode_message_splits_and_numbers_fragments():
    frames = encode_message(b'{"codex_model":"gpt-5"}', message_type=1,
                            sequence=0x1234, max_write_size=14)
    assert [f[:6] for f in frames] == [
        bytes([1, 1, 0x34, 0x12, 0, 4]),
        bytes([1, 1, 0x34, 0x12, 1, 4]),
        bytes([1, 1, 0x34, 0x12, 2, 4]),
        bytes([1, 1, 0x34, 0x12, 3, 4]),
    ]
    assert b"".join(frame[6:] for frame in frames) == b'{"codex_model":"gpt-5"}'
```

- [ ] **Step 2: Run the test and verify RED**

Run: `python3 -m unittest discover -s mac-codex-agent/tests -v`

Expected: import failure because `protocol.py` does not exist.

- [ ] **Step 3: Implement the public encoder**

```python
PROTOCOL_VERSION = 1
HEADER_SIZE = 6

def encode_message(payload: bytes, message_type: int, sequence: int,
                   max_write_size: int) -> list[bytes]:
    chunk_size = max_write_size - HEADER_SIZE
    if chunk_size <= 0 or not payload or len(payload) > 2048:
        raise ValueError("invalid BLE message size")
    count = (len(payload) + chunk_size - 1) // chunk_size
    if count > 255:
        raise ValueError("too many BLE fragments")
    return [
        bytes((PROTOCOL_VERSION, message_type, sequence & 0xff,
               (sequence >> 8) & 0xff, index, count)) +
        payload[index * chunk_size:(index + 1) * chunk_size]
        for index in range(count)
    ]
```

- [ ] **Step 4: Add invalid-size cases and run GREEN**

Run: `python3 -m unittest discover -s mac-codex-agent/tests -v`

Expected: protocol tests pass.

### Task 2: Focused Codex state collector

**Files:**
- Create: `mac-codex-agent/codex_adapter.py`
- Create: `mac-codex-agent/completion.py`
- Create: `mac-codex-agent/tests/test_completion.py`
- Create: `mac-codex-agent/tests/fixtures/task_complete.jsonl`

- [ ] **Step 1: Write a completion detector test**

```python
def test_latest_completion_returns_thread_and_timestamp(tmp_path):
    rollout = tmp_path / "rollout.jsonl"
    rollout.write_text(
        '{"timestamp":"2026-07-11T10:00:00Z","event_msg":'
        '{"type":"task_complete"}}\n', encoding="utf-8")
    assert read_latest_completion_from_rollout(rollout) == \
        "2026-07-11T10:00:00Z"
```

- [ ] **Step 2: Run the detector test and verify RED**

Run: `python3 -m unittest mac-codex-agent/tests/test_completion.py -v`

Expected: import failure because `completion.py` does not exist.

- [ ] **Step 3: Adapt the upstream collector**

Adapt `codex_adapter.py` from upstream revision `9067f231` while preserving the public functions `daemon_loop() -> None` and `get_state() -> dict[str, Any]`.

The returned mapping must contain `codex_model`, `codex_effort`, `five_hour_left_pct`, `codex_week_left_pct`, `today_minutes`, and `week_minutes`. Missing databases, auth, network, or AppleScript must return partial/default state instead of raising.

- [ ] **Step 4: Implement bounded rollout tail parsing**

```python
def read_latest_completion_from_rollout(path: Path,
                                        max_bytes: int = 512 * 1024) -> str | None:
    with path.open("rb") as stream:
        stream.seek(0, os.SEEK_END)
        size = stream.tell()
        stream.seek(max(0, size - max_bytes))
        lines = stream.read().decode("utf-8", errors="ignore").splitlines()
    for line in reversed(lines):
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        event = item.get("event_msg") or {}
        if event.get("type") == "task_complete":
            return str(item.get("timestamp") or event.get("timestamp") or "") or None
    return None
```

- [ ] **Step 5: Run collector tests and syntax checks**

Run: `python3 -m unittest discover -s mac-codex-agent/tests -v && python3 -m py_compile mac-codex-agent/*.py`

Expected: all tests and compilation pass.

### Task 3: Mac BLE agent and login setup

**Files:**
- Create: `mac-codex-agent/agent.py`
- Create: `mac-codex-agent/requirements.txt`
- Create: `mac-codex-agent/install_launch_agent.sh`
- Create: `mac-codex-agent/README.md`
- Create: `mac-codex-agent/tests/test_agent.py`

- [ ] **Step 1: Write the no-replay completion test**

```python
async def test_first_completion_is_baseline_not_event():
    source = FakeSource(completions=["old", "new"])
    transport = FakeTransport()
    agent = CodexStatusAgent(source, transport)
    await agent.tick()
    await agent.tick()
    assert [json.loads(item)["type"] for item in transport.events] == ["chime"]
```

- [ ] **Step 2: Run the agent test and verify RED**

Run: `python3 -m unittest mac-codex-agent/tests/test_agent.py -v`

Expected: import failure because `agent.py` does not exist.

- [ ] **Step 3: Implement BLE connection and state ticks**

The public agent owns a monotonically wrapping 16-bit sequence and uses these constants:

```python
DEVICE_NAME = "StopWatch-Codex"
SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"
STATUS_RX_UUID = "7a230002-5474-4f50-5741-544348434f44"
STATUS_INTERVAL_SECONDS = 2.0
```

Use `BleakScanner.find_device_by_filter`, `BleakClient`, `client.mtu_size` when available, and response-mode `write_gatt_char`. On disconnect, rescan with backoff `1, 2, 4, 8, 16, 30` seconds.

- [ ] **Step 4: Add installation assets**

`requirements.txt` contains `bleak>=0.22,<2`. The installer creates `.venv`, installs requirements, writes `~/Library/LaunchAgents/com.stopwatch.codex-agent.plist`, bootstraps it with `launchctl`, and logs under `~/Library/Logs/`.

- [ ] **Step 5: Run tests without requiring Bluetooth hardware**

Run: `python3 -m unittest discover -s mac-codex-agent/tests -v`

Expected: framing, completion, and mocked agent tests pass.

### Task 4: Firmware status model and fragment reassembler

**Files:**
- Create: `main/apps/app_codex_status/codex_status_model.h`
- Create: `main/apps/app_codex_status/codex_status_model.cpp`
- Create: `main/apps/app_codex_status/codex_ble_framing.h`
- Create: `main/apps/app_codex_status/codex_ble_framing.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] **Step 1: Define transport-independent public values**

```cpp
struct CodexStatusSnapshot {
    std::string model;
    std::string effort;
    int five_hour_left_pct = -1;
    int week_left_pct = -1;
    int today_minutes = -1;
    int week_minutes = -1;
};

enum class CodexMessageKind { Invalid, Status, Chime };

CodexMessageKind parse_codex_message(const char* data, size_t length,
                                     CodexStatusSnapshot& snapshot);
```

- [ ] **Step 2: Implement strict fragment validation**

`CodexBleReassembler::push(const uint8_t*, size_t, uint32_t now_ms)` accepts protocol version 1, message types 1 and 2, ordered fragments, total payload no larger than 2048 bytes, and a three-second timeout. It returns `std::optional<CodexBleMessage>` only for a complete payload.

- [ ] **Step 3: Route WebSocket JSON through the shared model**

Replace field-by-field cJSON access in `AppCodexStatus::handle_status_json` with `parse_codex_message`, preserving the current widget updates and 660 Hz Codex chime.

- [ ] **Step 4: Build the non-BLE refactor**

Run: `idf.py build`

Expected: existing WebSocket firmware builds before NimBLE is introduced.

### Task 5: StopWatch NimBLE peripheral

**Files:**
- Create: `main/apps/app_codex_status/codex_ble_transport.h`
- Create: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `main/idf_component.yml`
- Modify: `sdkconfig.defaults`
- Modify: `sdkconfig`

- [ ] **Step 1: Enable the BLE-only NimBLE stack**

Set `CONFIG_BT_ENABLED=y`, BLE-only controller mode, `CONFIG_BT_NIMBLE_ENABLED=y`, one connection, one bond, peripheral/broadcaster roles, GATT server support, and software Wi-Fi/BLE coexistence. Keep Classic Bluetooth and Bluedroid disabled.

- [ ] **Step 2: Implement the app-scoped transport contract**

```cpp
class CodexBleTransport {
public:
    bool start(const std::string& device_name);
    void stop();
    void poll(uint32_t now_ms);
    bool connected() const;
    bool take_message(CodexBleMessage& message);
};
```

The NimBLE access callback copies each write into a fixed-size FreeRTOS queue. `poll()` performs reassembly and publishes complete messages. GAP connect/disconnect events update an atomic flag; disconnect restarts advertising.

- [ ] **Step 3: Implement UUIDs and advertising**

Advertise `StopWatch-Codex` and service UUID `7a230001-5474-4f50-5741-544348434f44`. Register Status RX UUID `7a230002-5474-4f50-5741-544348434f44` for encrypted write and write-without-response. Configure Just Works, Secure Connections where supported, bonding, and encryption key distribution.

- [ ] **Step 4: Integrate BLE-first lifecycle**

Read `transport` and `ble_name` from `codex_status`. In `auto`, start BLE immediately and defer `ensure_network_started()` for 15 seconds. In `ble`, never start Wi-Fi. In `wifi`, skip BLE. A BLE connection stops WebSocket; disconnect resumes advertising. `onClose()` stops both transports before destroying LVGL objects.

- [ ] **Step 5: Build and inspect partition margin**

Run: `idf.py fullclean && idf.py build`

Expected: successful build, `StopWatch-UserDemo.bin` smaller than the `ota_0` partition, and no undefined NimBLE symbols.

### Task 6: Documentation, installation, and hardware verification

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-07-11-codex-status-ble-design.md` only if implementation constraints require an explicit correction

- [ ] **Step 1: Document BLE and fallback setup**

Add exact commands for creating the Mac agent virtual environment, installing it as a LaunchAgent, checking logs, choosing `auto|ble|wifi`, and troubleshooting pairing.

- [ ] **Step 2: Install and test the Mac agent**

Run: `mac-codex-agent/install_launch_agent.sh`

Expected: LaunchAgent is loaded and logs show scanning for `StopWatch-Codex` without Python errors.

- [ ] **Step 3: Flash the full firmware stack**

Run: `scripts/flash_full_assistant_stack.sh /dev/cu.usbmodem1101`

Expected: launcher, assistant, assets, and `ota_0` boot selection write successfully with hash verification.

- [ ] **Step 4: Verify end-to-end behavior**

Open Codex, confirm `BLE PAIR` changes to `BLE LIVE`, compare model/effort/quota values with the Mac collector, trigger one real Codex completion and hear one chime, then long-hold both buttons to return to Launcher.

- [ ] **Step 5: Verify restart and OTA boundaries**

Reboot, enter Codex again, switch into Assistant, return to Launcher, and repeat. Monitor serial output long enough to confirm there is no reset loop or initialization hang.

- [ ] **Step 6: Run final checks and commit**

Run: `python3 -m unittest discover -s mac-codex-agent/tests -v`, `idf.py build`, and `git diff --check`.

Expected: all tests pass, firmware builds within partition, and the diff has no whitespace errors.
