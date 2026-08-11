# Codex App Global Right-Button Submit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the upper-right button globally submit or steer the active Codex desktop conversation while the watch Codex app is open, without changing its existing hold-to-talk behavior.

**Architecture:** The watch classifies short single clicks, short double clicks, and holds from physical edge timing. A dedicated authenticated BLE input-action control/notification pair carries `Submit` and `ForceSteer` independently of voice readiness. The Mac Agent validates the foreground Codex composer through Accessibility, then performs the normal or forced-steer shortcut without targeting unknown focus.

**Tech Stack:** ESP-IDF/C++17, NimBLE GATT, AES-CCM, Python 3.11, asyncio/Bleak, PyObjC ApplicationServices/AppKit/Quartz, unittest, shell-driven C++ host tests.

---

## File Structure

- `main/apps/app_codex_status/codex_button_gesture.h/.cpp`: pure timing state machine for single click, double click, hold cancellation, and suppression reset.
- `main/apps/app_codex_status/input_action_protocol.h/.cpp`: fixed-size authenticated control and action frames, separate from audio framing.
- `main/apps/app_codex_status/input_action_controller.h/.cpp`: connection-scoped action sequencing and bounded outgoing queue.
- `main/apps/app_codex_status/voice_input_controller.h/.cpp`: expose the currently ignored short-click edge and route final click actions without changing voice capture internals.
- `main/apps/app_codex_status/codex_ble_transport.h/.cpp`: add input-control write and input-action notify characteristics.
- `main/apps/app_codex_status/app_codex_status.h/.cpp`: own the gesture/action controllers and connect button decisions to BLE delivery.
- `mac-codex-agent/input_action_protocol.py`: Python wire-format peer and replay guard.
- `mac-codex-agent/codex_submit.py`: foreground/composer inspection and safe normal/steer dispatch.
- `mac-codex-agent/agent.py`: independent low-rate input-action notification subscription.
- `mac-codex-agent/runtime.py`: keep the action path alive even when Typeless or audio setup is unavailable.
- `tests/*` and `mac-codex-agent/tests/*`: host, protocol, transport, runtime, and Accessibility-policy coverage.
- `scripts/run_unified_offline_validation.sh`: register the new focused C++ host tests.
- `mac-codex-agent/README.md`: document the Codex-app-global right-button behavior.

### Task 1: Classify Clicks on the Watch

**Files:**
- Create: `main/apps/app_codex_status/codex_button_gesture.h`
- Create: `main/apps/app_codex_status/codex_button_gesture.cpp`
- Create: `tests/codex_button_gesture_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing host test**

Add table-driven assertions for a 300 ms single-click delay, a double click with no preceding submit, a second press that crosses the hold threshold, suppression resets, and unsigned timer wrap:

```cpp
#include "main/apps/app_codex_status/codex_button_gesture.h"
#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    CodexButtonGesture gesture;
    assert(gesture.update(true, false, false, 100) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 399) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 400) == CodexButtonAction::Submit);

    gesture.reset();
    assert(gesture.update(true, false, false, 1000) == CodexButtonAction::None);
    assert(gesture.update(false, true, false, 1200) == CodexButtonAction::None);
    assert(gesture.update(true, false, false, 1360) == CodexButtonAction::ForceSteer);
    assert(gesture.update(false, false, false, 1400) == CodexButtonAction::None);

    gesture.reset();
    gesture.update(true, false, false, 2000);
    gesture.update(false, true, false, 2200);
    assert(gesture.update(false, true, true, 2290) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 2400) == CodexButtonAction::None);

    gesture.update(true, false, false, 3000);
    assert(gesture.update(false, false, false, 3100, true) == CodexButtonAction::None);
    assert(gesture.update(false, false, false, 3400) == CodexButtonAction::None);

    gesture.reset();
    const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 100;
    gesture.update(true, false, false, near_wrap);
    assert(gesture.update(false, false, false, 199) == CodexButtonAction::Submit);
}
```

- [ ] **Step 2: Register and run the test to verify it fails**

Add this stage near the other Codex host tests:

```bash
stage "Codex button gesture host test"
run_host_test codex_button_gesture_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    tests/codex_button_gesture_test.cpp \
    main/apps/app_codex_status/codex_button_gesture.cpp
```

Run: `./scripts/run_unified_offline_validation.sh`

Expected: FAIL because `codex_button_gesture.h` and its implementation do not exist.

- [ ] **Step 3: Implement the minimal pure state machine**

Define the public API exactly as follows:

```cpp
enum class CodexButtonAction : uint8_t { None, Submit, ForceSteer };

class CodexButtonGesture {
public:
    static constexpr uint32_t kDoubleClickMs = 300;
    void reset();
    CodexButtonAction update(
        bool short_click,
        bool right_pressed,
        bool hold_started,
        uint32_t now_ms,
        bool suppressed = false);

private:
    bool _single_pending = false;
    bool _second_press_latched = false;
    uint32_t _first_click_ms = 0;
};
```

Implement these invariants in `update`: suppression and `hold_started` call `reset`; a first `short_click` arms the timer; a right-button press within 300 ms latches the possible second click; a latched second short click returns `ForceSteer`; an unlatched expiration returns `Submit`; all elapsed comparisons use unsigned subtraction.

- [ ] **Step 4: Run the focused host stage and full offline validation**

Run: `./scripts/run_unified_offline_validation.sh`

Expected: `Codex button gesture host test` PASS and the existing voice/button stages PASS.

- [ ] **Step 5: Commit**

```bash
git add main/apps/app_codex_status/codex_button_gesture.* tests/codex_button_gesture_test.cpp scripts/run_unified_offline_validation.sh
git commit -m "Add Codex right-button gesture classifier"
```

### Task 2: Add an Authenticated Input-Action Protocol

**Files:**
- Create: `main/apps/app_codex_status/input_action_protocol.h`
- Create: `main/apps/app_codex_status/input_action_protocol.cpp`
- Create: `tests/input_action_protocol_test.cpp`
- Create: `mac-codex-agent/input_action_protocol.py`
- Create: `mac-codex-agent/tests/test_input_action_protocol.py`
- Modify: `main/apps/app_codex_status/voice_input_crypto.h`
- Modify: `mac-codex-agent/voice_input_crypto.py`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write matching failing C++ and Python vectors**

Use a separate protocol version and fixed layouts:

```text
READY control AAD: version:u8, kind:u8, reserved:u16, nonce:8 bytes
READY wire:        AAD + 8-byte AES-CCM tag
ACTION header:     version:u8, kind:u8, sequence:u32, watch_ms:u32, body_size:u16
ACTION wire:       header + 8-byte AES-CCM tag
```

Test `Submit` and `ForceSteer`, a fixed cross-language vector, wrong tags, zero/non-monotonic sequences, wrong nonce, wrong lengths, reserved bytes, and replay rejection. The Python API is:

```cpp
namespace input_action {
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kReadyControlSize = 20;
constexpr size_t kWireActionSize = 20;
enum class Kind : uint8_t { Submit = 1, ForceSteer = 2 };
struct ReadyControl { voice_input::crypto::ExchangeNonce connection_nonce = {}; };
struct Action { Kind kind = Kind::Submit; uint32_t sequence = 0; uint32_t watch_ms = 0; };
struct WireAction { Action logical = {}; std::array<uint8_t, kWireActionSize> bytes = {}; };
size_t encode_ready(const ReadyControl&, voice_input::crypto::CcmContext&, uint8_t*, size_t);
bool decode_ready(const uint8_t*, size_t, voice_input::crypto::CcmContext&, ReadyControl&);
size_t encode_action(const Action&, const voice_input::crypto::ExchangeNonce&,
                     voice_input::crypto::CcmContext&, uint8_t*, size_t);
bool decode_action(const uint8_t*, size_t,
                   const voice_input::crypto::ExchangeNonce&,
                   voice_input::crypto::CcmContext&, Action&);
}
```

```python
PROTOCOL_VERSION = 1
READY = 1
CONTROL_AAD = struct.Struct("<BBH8s")
ACTION_HEADER = struct.Struct("<BBIIH")

class InputAction(enum.IntEnum):
    SUBMIT = 1
    FORCE_STEER = 2

@dataclasses.dataclass(frozen=True)
class ActionPacket:
    action: InputAction
    sequence: int
    watch_ms: int

def encode_ready(key: bytes, connection_nonce: bytes) -> bytes:
    _validate_nonce(connection_nonce)
    aad = CONTROL_AAD.pack(PROTOCOL_VERSION, READY, 0, connection_nonce)
    return aad + encrypt(
        key, INPUT_READY_DOMAIN, connection_nonce, 0, b"", aad
    )

def decode_ready(key: bytes, raw: bytes) -> bytes:
    if len(raw) != CONTROL_AAD.size + TAG_BYTES:
        raise ValueError("invalid input Ready size")
    version, kind, reserved, connection_nonce = CONTROL_AAD.unpack_from(raw)
    if version != PROTOCOL_VERSION or kind != READY or reserved != 0:
        raise ValueError("invalid input Ready header")
    if decrypt(
        key, INPUT_READY_DOMAIN, connection_nonce, 0,
        raw[CONTROL_AAD.size:], raw[:CONTROL_AAD.size],
    ) != b"":
        raise ValueError("input Ready body must be empty")
    return connection_nonce

def encode_action(key: bytes, action: InputAction, sequence: int,
                  watch_ms: int, connection_nonce: bytes) -> bytes:
    _validate_action_values(action, sequence, watch_ms, connection_nonce)
    header = ACTION_HEADER.pack(
        PROTOCOL_VERSION, int(action), sequence, watch_ms, TAG_BYTES
    )
    return header + encrypt(
        key, _ACTION_DOMAINS[action], connection_nonce, sequence, b"", header
    )

def decode_action(key: bytes, raw: bytes,
                  connection_nonce: bytes) -> ActionPacket:
    if len(raw) != ACTION_HEADER.size + TAG_BYTES:
        raise ValueError("invalid input action size")
    version, raw_action, sequence, watch_ms, body_size = ACTION_HEADER.unpack_from(raw)
    action = InputAction(raw_action)
    if version != PROTOCOL_VERSION or body_size != TAG_BYTES:
        raise ValueError("invalid input action header")
    _validate_action_values(action, sequence, watch_ms, connection_nonce)
    if decrypt(
        key, _ACTION_DOMAINS[action], connection_nonce, sequence,
        raw[ACTION_HEADER.size:], raw[:ACTION_HEADER.size],
    ) != b"":
        raise ValueError("input action body must be empty")
    return ActionPacket(action, sequence, watch_ms)
```

The replay guard accepts only a strictly increasing sequence for the active connection nonce.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_input_action_protocol.py -v
./scripts/run_unified_offline_validation.sh
```

Expected: FAIL because the protocol modules and crypto domains are absent.

- [ ] **Step 3: Implement the shared protocol**

Add non-overlapping AES-CCM domains to both crypto implementations:

```text
InputReady = 0x21
InputSubmit = 0x22
InputForceSteer = 0x23
```

Use the header as AAD and encrypt an empty plaintext, producing only the 8-byte tag. Require a nonzero 8-byte connection nonce, `sequence >= 1`, exact fixed sizes, `body_size == 8`, and a defined action kind. Keep this module independent of audio/Typeless types.

- [ ] **Step 4: Run both language tests**

Run the two commands from Step 2.

Expected: the Python protocol test and new `Input action protocol host test` PASS with identical vectors.

- [ ] **Step 5: Commit**

```bash
git add main/apps/app_codex_status/input_action_protocol.* main/apps/app_codex_status/voice_input_crypto.h tests/input_action_protocol_test.cpp mac-codex-agent/input_action_protocol.py mac-codex-agent/voice_input_crypto.py mac-codex-agent/tests/test_input_action_protocol.py scripts/run_unified_offline_validation.sh
git commit -m "Add authenticated Codex input action protocol"
```

### Task 3: Route Gestures Through the Firmware BLE Transport

**Files:**
- Create: `main/apps/app_codex_status/input_action_controller.h`
- Create: `main/apps/app_codex_status/input_action_controller.cpp`
- Create: `tests/input_action_controller_test.cpp`
- Modify: `main/apps/app_codex_status/voice_input_controller.h`
- Modify: `main/apps/app_codex_status/voice_input_controller.cpp`
- Modify: `main/apps/app_codex_status/codex_ble_transport.h`
- Modify: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write failing controller and contract tests**

The controller test must prove: no action before authenticated Ready, sequences start at one, queue capacity is bounded, disconnect clears Ready and queued data, and rejected/congested notifications do not get retried after an ambiguous acceptance outcome.

Extend the firmware contract test to require input UUIDs `7a230006-5474-4f50-5741-544348434f44` and `7a230007-5474-4f50-5741-544348434f44`, independent subscription flags, `notify_input_action`, and reset on `handle_gap_disconnected`.

- [ ] **Step 2: Run focused tests to verify they fail**

Run:

```bash
./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
./scripts/run_unified_offline_validation.sh
```

Expected: FAIL for missing input-action controller and GATT characteristics.

- [ ] **Step 3: Expose short clicks from the existing edge gate**

Add `ShortClick` to `VoiceButtonAction`. In `VoiceButtonEdgeGate::State::Pending`, return it when the right button releases before `kChordGuardMs`. Extend `VoiceButtonDispatcher` with a `CodexButtonGesture` member and a `CodexButtonActionSink*` parameter. Feed each short click, current right-button state, `Press` as `hold_started`, and wake/Home suppression into the classifier. Dispatch only final `Submit` or `ForceSteer` to the action sink.

Keep `VoiceInputController` as the voice sink; do not add desktop-action logic to it.

- [ ] **Step 4: Implement the bounded action controller**

Use this interface:

```cpp
class InputActionController {
public:
    void set_ready(const input_action::ReadyControl& ready);
    void disconnect();
    bool enqueue(CodexButtonAction action, uint32_t now_ms);
    bool take(input_action::WireAction& output);
    void record_result(bool accepted);
};
```

It owns the current connection nonce, a monotonic `uint32_t` sequence, and at most four encoded actions. It clears everything on disconnect and never regenerates/retries a dequeued action.

- [ ] **Step 5: Add independent GATT control and notification characteristics**

Add constants:

```text
INPUT_CONTROL_UUID = 7a230006-5474-4f50-5741-544348434f44
INPUT_TX_UUID      = 7a230007-5474-4f50-5741-544348434f44
```

Decode authenticated Ready controls into a bounded queue. Track the input TX subscription separately from voice TX. Add `input_action_subscribed()` and `notify_input_action()`. On disconnect, reset the Ready replay guard, input subscription, value handle, and connection-scoped controller state.

- [ ] **Step 6: Wire the Codex app**

Make `AppCodexStatus` implement `CodexButtonActionSink`. Its handler enqueues the final action in `InputActionController`. During `onRunning`, drain authenticated controls before actions, then drain encoded action notifications through `notify_input_action`. Reset the gesture classifier and action controller in `onOpen`, `onClose`, Home, wake suppression, and BLE disconnect paths.

- [ ] **Step 7: Run firmware tests and build**

Run:

```bash
./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
./scripts/run_unified_offline_validation.sh
./scripts/build_unified_candidate.sh
```

Expected: contract and host tests PASS; unified firmware build succeeds.

- [ ] **Step 8: Commit**

```bash
git add main/apps/app_codex_status tests/input_action_controller_test.cpp mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py scripts/run_unified_offline_validation.sh
git commit -m "Route Codex button actions over BLE"
```

### Task 4: Implement Safe Codex Desktop Submission

**Files:**
- Create: `mac-codex-agent/codex_submit.py`
- Create: `mac-codex-agent/tests/test_codex_submit.py`

- [ ] **Step 1: Write failing policy tests with a fake Accessibility adapter**

Cover foreground bundle mismatch, empty composer, missing composer, idle Send, running Queue, running Steer, unknown semantics, single click, and forced steer. Use these types:

```python
class SubmitSemantic(enum.Enum):
    SEND = "send"
    QUEUE = "queue"
    STEER = "steer"
    UNKNOWN = "unknown"

@dataclasses.dataclass(frozen=True)
class ComposerSnapshot:
    codex_foreground: bool
    editable: bool
    has_text: bool
    semantic: SubmitSemantic

class CodexSubmitController:
    def handle(self, action: InputAction) -> bool:
        snapshot = self._accessibility.snapshot()
        if not (
            snapshot.codex_foreground
            and snapshot.editable
            and snapshot.has_text
        ):
            return False
        if action is InputAction.SUBMIT:
            self._accessibility.normal_submit()
            return True
        if action is not InputAction.FORCE_STEER:
            return False
        if snapshot.semantic in (SubmitSemantic.SEND, SubmitSemantic.STEER):
            self._accessibility.normal_submit()
            return True
        if snapshot.semantic is SubmitSemantic.QUEUE:
            self._accessibility.alternate_submit()
            return True
        return False
```

Assert `SUBMIT` calls `normal_submit`; `FORCE_STEER` calls `normal_submit` for `SEND` or `STEER`, `alternate_submit` for `QUEUE`, and neither method for an unsafe snapshot.

- [ ] **Step 2: Run the test to verify it fails**

Run: `./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_codex_submit.py -v`

Expected: FAIL because `codex_submit.py` does not exist.

- [ ] **Step 3: Implement the pure decision policy**

Require all three safety predicates before dispatch: Codex foreground, editable composer, and non-empty text. Return `False` and log a reason for every ignored action. `FORCE_STEER` with `UNKNOWN` fails closed.

- [ ] **Step 4: Implement the production Accessibility adapter**

Use `NSWorkspace.sharedWorkspace().frontmostApplication()` and require bundle ID `com.openai.codex`. Starting from the focused AX element, verify an editable text role/value and inspect nearby submit control `AXTitle`, `AXDescription`, and `AXHelp`. Normalize the current English and Simplified Chinese labels:

```python
SEND_LABELS = frozenset(("send", "发送"))
QUEUE_LABELS = frozenset(("queue", "加入队列"))
STEER_LABELS = frozenset(("steer", "调整方向"))
```

The normal submit emits Return. The alternate submit emits Command+Shift+Return. Both use complete down/up Quartz events and are callable only after a safe snapshot. AX traversal is bounded by a fixed node/depth limit so a malformed tree cannot stall the Agent.

- [ ] **Step 5: Run focused tests**

Run: `./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_codex_submit.py -v`

Expected: all policy and adapter-mocking tests PASS.

- [ ] **Step 6: Commit**

```bash
git add mac-codex-agent/codex_submit.py mac-codex-agent/tests/test_codex_submit.py
git commit -m "Add safe Codex desktop submit controller"
```

### Task 5: Keep Input Actions Alive Independently of Voice

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`

- [ ] **Step 1: Write failing transport and runtime tests**

Add tests that require:

- input notification subscription starts before voice readiness is published;
- an authenticated action reaches `CodexSubmitController`;
- Typeless-not-running and driver-required states disable only voice TX, not input TX;
- disconnect stops both subscriptions exactly once;
- input decode/handler failure is isolated and cannot break status delivery;
- duplicate action sequences do not submit twice.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py -v
```

Expected: FAIL because the transport and runtime have no input-action lifecycle.

- [ ] **Step 3: Add an independent low-rate transport subscription**

In `BleStatusTransport`, add:

```python
async def enable_input_actions(self, ready_payload: bytes, handler) -> None:
    if not self.connected:
        raise ConnectionError("StopWatch BLE connection is not active")
    if self._input_notify_started:
        raise RuntimeError("input action subscription is already active")
    loop = asyncio.get_running_loop()

    def on_notification(_sender, data) -> None:
        payload = bytes(data)
        loop.call_soon_threadsafe(
            lambda: asyncio.create_task(handler(payload))
        )

    await asyncio.wait_for(
        self._client.start_notify(INPUT_TX_UUID, on_notification),
        timeout=GATT_OPERATION_TIMEOUT_SECONDS,
    )
    self._input_notify_started = True
    try:
        await asyncio.wait_for(
            self._client.write_gatt_char(
                INPUT_CONTROL_UUID, ready_payload, response=True
            ),
            timeout=GATT_OPERATION_TIMEOUT_SECONDS,
        )
    except BaseException:
        await self.disable_input_actions()
        raise

async def disable_input_actions(self) -> None:
    if self.connected and self._input_notify_started:
        await asyncio.wait_for(
            self._client.stop_notify(INPUT_TX_UUID),
            timeout=GATT_OPERATION_TIMEOUT_SECONDS,
        )
    self._input_notify_started = False
```

Subscribe to `INPUT_TX_UUID`, then write Ready to `INPUT_CONTROL_UUID`. Use a bounded queue of eight actions and the same thread-safe callback admission pattern as voice, but separate generation/failure/cleanup state. Disabling voice must not touch input actions.

- [ ] **Step 4: Integrate the runtime lifecycle**

After loading the shared key, create a fresh input nonce, encode Ready, construct the replay guard and `CodexSubmitController`, and enable input actions before calling voice `prepare()`. Decode each packet against the active nonce and call the submit controller once. Store input lifecycle state in `_BleSessionContext`; shut it down within the existing bounded disconnect deadline.

If the key cannot be loaded, neither authenticated voice nor authenticated input actions start. If only voice readiness fails, input actions remain enabled.

- [ ] **Step 5: Run transport/runtime tests**

Run the command from Step 2.

Expected: all transport/runtime tests PASS, including Typeless-unavailable action delivery.

- [ ] **Step 6: Commit**

```bash
git add mac-codex-agent/agent.py mac-codex-agent/runtime.py mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py
git commit -m "Keep Codex button actions independent of voice"
```

### Task 6: Document and Verify the Complete Feature

**Files:**
- Modify: `mac-codex-agent/README.md`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

- [ ] **Step 1: Add a failing documentation contract**

Require the README to state:

```text
Inside the watch Codex app, an upper-right single click submits after the
300 ms double-click window. A fast double click steers the running turn. Holding
the button still controls Typeless. These click actions work even when Typeless
is unavailable, but only when Codex is foreground with a non-empty composer.
```

- [ ] **Step 2: Run the contract to verify it fails**

Run: `./mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_unified_voice_validation.py -v`

Expected: FAIL because the README still says short taps are ignored.

- [ ] **Step 3: Update the README and validation checklist**

Replace the obsolete short-tap sentence with the confirmed single/double/hold semantics. Add manual cases for Codex foreground/background, idle/running Queue/running Steer, empty composer, Typeless unavailable, wake, and A+B Home.

- [ ] **Step 4: Run focused and full verification**

Run:

```bash
./mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -v
./scripts/run_unified_offline_validation.sh
./scripts/build_unified_candidate.sh
git diff --check
git status --short
```

Expected: all Python and C++ tests PASS, unified firmware builds, `git diff --check` is silent, and status contains only the intended feature files plus the pre-existing untracked `.superpowers/` directory.

- [ ] **Step 5: Perform hardware acceptance**

With the updated Agent running and the watch Codex app open:

1. Enter text with Codex idle; single click submits once after about 300 ms.
2. Enter text during a running turn in Queue mode; single click queues it.
3. Enter text during a running turn in Queue mode; double click steers it.
4. Enter text during a running turn in Steer mode; double click steers once.
5. Put Codex in the background; both click gestures are ignored.
6. Stop Typeless; click actions still work and a hold remains rejected as voice unavailable.
7. Verify hold-to-talk, A+B Home, and screen-off wake do not submit accidentally.

- [ ] **Step 6: Commit**

```bash
git add mac-codex-agent/README.md mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "Document Codex global button submission"
```
