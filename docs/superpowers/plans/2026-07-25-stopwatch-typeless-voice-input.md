# StopWatch Typeless Voice Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add production push-to-talk voice input from the StopWatch Codex app to Typeless through the existing Mac Agent, BLE IMA-ADPCM transport, and an Agent-managed BlackHole 2ch device.

**Architecture:** The watch turns right-button edges into version-2 voice events and streams the already validated 16 kHz IMA-ADPCM blocks over the existing BLE connection. The Mac Agent decodes those events, feeds PCM into an in-process native Core Audio bridge targeting BlackHole, leases the system default input, and emits paired Fn taps around each voice session. BlackHole installation is initiated from the existing menu application using a pinned, hash-verified package that is present only in the generated local app bundle.

**Tech Stack:** ESP-IDF 5.5, C++17, NimBLE, FreeRTOS, Python 3, asyncio, Bleak, PyObjC/AppKit/Quartz, Core Audio and AudioToolbox, Objective-C++, BlackHole 2ch 0.7.1, Python `unittest`, C++ host tests.

---

## Conditional Encryption Amendment

Before Task 2, execute Task 15. It measures AES-CCM on the actual watch and BLE
path under the gate in
`docs/superpowers/specs/2026-07-25-voice-encryption-performance-gate.md`.

- If Task 15 passes, update Tasks 5, 6, 7, 9, 10, 12, 13, and 14 to carry
  authenticated AES-CCM controls and audio with a locally generated device key
  stored in Mac Keychain and compiled into the watch through an ignored
  generated header.
- If Task 15 shows a large crypto-specific performance impact, stop and ask the
  user whether encryption is still required before changing those tasks.
- The fixed Task-15 key is diagnostic only and is never used by the installed
  Agent.

## Scope And File Map

### Mac Agent: new files

- `mac-codex-agent/voice_driver.py`: pinned BlackHole metadata, package verification, install state, and authorized installation.
- `mac-codex-agent/voice_shortcut.py`: Accessibility readiness, Typeless process detection, and paired Fn taps.
- `mac-codex-agent/voice_audio.py`: Python wrapper around the native Core Audio bridge and the default-input lease.
- `mac-codex-agent/voice_input_protocol.py`: production voice control and notification framing.
- `mac-codex-agent/voice_session.py`: Mac voice session state machine, ADPCM decoding, gap handling, route acquisition, and cleanup.
- `mac-codex-agent/native/voice_audio_ring.h`: bounded single-producer/single-consumer stereo PCM ring.
- `mac-codex-agent/native/voice_audio_bridge.h`: stable C ABI consumed through `ctypes`.
- `mac-codex-agent/native/voice_audio_bridge.mm`: AudioQueue output, BlackHole lookup, sample conversion, and default-input Core Audio operations.
- `mac-codex-agent/native/voice_audio_ring_test.cpp`: native ring and 16-to-48 kHz conversion tests.
- `mac-codex-agent/tests/test_voice_driver.py`: driver verification and installation classification.
- `mac-codex-agent/tests/test_voice_shortcut.py`: Fn pairing, Accessibility, and Typeless process checks.
- `mac-codex-agent/tests/test_voice_audio.py`: bridge wrapper and input restoration.
- `mac-codex-agent/tests/test_voice_input_protocol.py`: byte-for-byte protocol tests.
- `mac-codex-agent/tests/test_voice_session.py`: production Mac session state and cleanup.

### Mac Agent: modified files

- `.gitignore`: exclude downloaded BlackHole packages and built native libraries.
- `mac-codex-agent/install_launch_agent.sh`: pin and verify BlackHole, build the native bridge, and package all voice modules.
- `mac-codex-agent/requirements.txt`: include the PyObjC frameworks used by Quartz and Core Audio control.
- `mac-codex-agent/agent.py`: subscribe to voice notifications and send readiness controls through the existing BLE client.
- `mac-codex-agent/runtime.py`: own `VoiceSessionService`, expose voice state, and clean up on reconnect or quit.
- `mac-codex-agent/app_state.py`: add presentable voice states without changing connection preference semantics.
- `mac-codex-agent/menu_app.py`: add voice status and setup/accessibility actions to the existing menu.
- `mac-codex-agent/tests/test_agent.py`, `test_runtime.py`, `test_app_state.py`, `test_menu_app.py`, and `test_install_script.py`: integration contracts.
- `mac-codex-agent/README.md`: setup, privacy boundary, usage, recovery, and non-autostart behavior.

### Firmware: new files

- `main/apps/app_codex_status/voice_input_protocol.h/.cpp`: version-2 ready/unavailable and PTT notification encoding.
- `main/apps/app_codex_status/voice_input_session.h/.cpp`: pure push-to-talk state and sequence accounting.
- `main/apps/app_codex_status/voice_capture_pipeline.h/.cpp`: recorder ownership, resampling, ADPCM block production, and bounded shutdown shared by probe and production controllers.
- `main/apps/app_codex_status/voice_input_controller.h/.cpp`: button-driven production capture and BLE packet queue.
- `tests/voice_input_protocol_test.cpp`: protocol byte layout and rejection tests.
- `tests/voice_input_session_test.cpp`: state-transition and summary-counter tests.
- `tests/voice_capture_pipeline_contract_test.py`: firmware ownership and callback-safety contracts.

### Firmware: modified files

- `main/apps/app_codex_status/ble_voice_probe_controller.h/.cpp`: delegate microphone mechanics to the shared capture pipeline while retaining the diagnostic CLI.
- `main/apps/app_codex_status/codex_ble_transport.h/.cpp`: accept both protocol versions, restore the production BLE identity, and expose production voice controls.
- `main/apps/app_codex_status/app_codex_status.h/.cpp`: create the production controller, route right-button edges, preserve wake/home behavior, and render voice status.
- `main/CMakeLists.txt`: compile the production voice modules.
- `scripts/run_unified_offline_validation.sh`: add the new host and native bridge tests with renumbered stages.
- `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`: preserve probe isolation and verify the production identity restoration.

## Task 1: Pin And Verify The BlackHole Package

**Files:**
- Create: `mac-codex-agent/voice_driver.py`
- Create: `mac-codex-agent/tests/test_voice_driver.py`
- Modify: `.gitignore`

- [ ] **Step 1: Write failing package-verification tests**

```python
class VoiceDriverTests(unittest.TestCase):
    def test_accepts_only_the_pinned_blackhole_package(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        self.assertTrue(verify_package(package, expected))
        self.assertFalse(verify_package(package, "0" * 64))

    def test_classifies_missing_receipt_and_visible_device(self):
        self.assertEqual(
            detect_driver_state(False, False), DriverState.DRIVER_REQUIRED
        )
        self.assertEqual(
            detect_driver_state(True, False), DriverState.RESTART_REQUIRED
        )
        self.assertEqual(detect_driver_state(True, True), DriverState.READY)
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_driver.py -v
```

Expected: import failure because `voice_driver.py` does not exist.

- [ ] **Step 3: Implement the pinned package model**

```python
BLACKHOLE_VERSION = "0.7.1"
BLACKHOLE_PACKAGE_NAME = "BlackHole2ch-0.7.1.pkg"
BLACKHOLE_URL = (
    "https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg"
)
BLACKHOLE_SHA256 = (
    "57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf"
)
BLACKHOLE_RECEIPT = "audio.existential.BlackHole2ch"
BLACKHOLE_DEVICE_NAME = "BlackHole 2ch"


class DriverState(enum.Enum):
    DRIVER_REQUIRED = "driver_required"
    RESTART_REQUIRED = "restart_required"
    READY = "ready"
    INSTALLING = "installing"
    ERROR = "error"


def verify_package(path: pathlib.Path, expected_sha256: str = BLACKHOLE_SHA256) -> bool:
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as package:
        for chunk in iter(lambda: package.read(1024 * 1024), b""):
            digest.update(chunk)
    return hmac.compare_digest(digest.hexdigest(), expected_sha256)


def detect_driver_state(receipt_present: bool, device_visible: bool) -> DriverState:
    if device_visible:
        return DriverState.READY
    if receipt_present:
        return DriverState.RESTART_REQUIRED
    return DriverState.DRIVER_REQUIRED
```

Add these ignore rules:

```gitignore
mac-codex-agent/.driver-cache/
mac-codex-agent/.native/
*.pkg
libstopwatch_voice_audio.dylib
```

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the Step 2 command.

Expected: all `VoiceDriverTests` pass.

- [ ] **Step 5: Commit the package contract**

```bash
git add .gitignore mac-codex-agent/voice_driver.py \
  mac-codex-agent/tests/test_voice_driver.py
git commit -m "Add BlackHole package verification"
```

## Task 2: Build And Package The Native Core Audio Bridge

**Files:**
- Create: `mac-codex-agent/native/voice_audio_ring.h`
- Create: `mac-codex-agent/native/voice_audio_bridge.h`
- Create: `mac-codex-agent/native/voice_audio_bridge.mm`
- Create: `mac-codex-agent/native/voice_audio_ring_test.cpp`
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`

- [ ] **Step 1: Write the failing native ring test**

```cpp
int main()
{
    VoiceAudioRing<24> ring;
    const int16_t mono[] = {100, -200};
    assert(ring.writeMono16kAsStereo48k(mono, 2) == 6);

    int16_t output[12] = {};
    assert(ring.readStereo(output, 6) == 6);
    const int16_t expected[] = {
        100, 100, 100, 100, 100, 100,
        -200, -200, -200, -200, -200, -200,
    };
    assert(std::equal(std::begin(output), std::end(output), std::begin(expected)));

    assert(ring.readStereo(output, 1) == 0);
    return 0;
}
```

- [ ] **Step 2: Compile the test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror \
  -Imac-codex-agent/native \
  mac-codex-agent/native/voice_audio_ring_test.cpp \
  -o /tmp/voice_audio_ring_test
```

Expected: compilation fails because `voice_audio_ring.h` is absent.

- [ ] **Step 3: Implement the bounded native ring**

Implement `VoiceAudioRing<CapacityFrames>` with:

```cpp
size_t writeMono16kAsStereo48k(const int16_t* samples, size_t count);
size_t readStereo(int16_t* interleaved_stereo, size_t frame_count);
size_t queuedFrames() const;
uint64_t droppedFrames() const;
void clear();
```

Use atomic read/write frame indices, duplicate each 16 kHz mono sample into
three 48 kHz stereo frames, reject excess frames without overwriting unread
audio, and zero neither caller-owned input nor unread ring storage.

- [ ] **Step 4: Compile and run the native ring test**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror \
  -Imac-codex-agent/native \
  mac-codex-agent/native/voice_audio_ring_test.cpp \
  -o /tmp/voice_audio_ring_test
/tmp/voice_audio_ring_test
```

Expected: exit code 0.

- [ ] **Step 5: Write the C ABI and AudioQueue implementation**

Expose only these functions from `voice_audio_bridge.h`:

```c
int swva_find_device_uid(const char* name, char* output, size_t output_size);
int swva_get_default_input_uid(char* output, size_t output_size);
int swva_set_default_input_uid(const char* uid);
int swva_open(const char* output_device_uid);
int swva_start(void);
size_t swva_write_mono_16k(const int16_t* samples, size_t count);
uint64_t swva_dropped_frames(void);
void swva_stop(void);
void swva_close(void);
```

Implement a 48 kHz, signed 16-bit, stereo `AudioQueue`. Set
`kAudioQueueProperty_CurrentDevice` to the supplied BlackHole UID before
allocating three 20 ms buffers. The native callback reads from
`VoiceAudioRing<48000>` and fills missing frames with zero. It must not call
Python, allocate heap memory, log, or block.

- [ ] **Step 6: Add a deterministic bridge build to the installer**

Add:

```bash
NATIVE_BUILD_DIR="$ROOT_DIR/.native"
NATIVE_LIBRARY="$NATIVE_BUILD_DIR/libstopwatch_voice_audio.dylib"
mkdir -p "$NATIVE_BUILD_DIR"
xcrun clang++ -std=c++17 -O2 -dynamiclib \
    -fvisibility=hidden \
    -framework AudioToolbox \
    -framework CoreAudio \
    -framework CoreFoundation \
    "$ROOT_DIR/native/voice_audio_bridge.mm" \
    -o "$NATIVE_LIBRARY"
```

Copy the resulting library to
`$APP_RESOURCES/native/libstopwatch_voice_audio.dylib`. Extend
`test_install_script.py` to assert the exact frameworks, source path, output
path, and app-resource copy.

- [ ] **Step 7: Build the dylib and run installer contract tests**

Run:

```bash
xcrun clang++ -std=c++17 -O2 -dynamiclib \
  -fvisibility=hidden \
  -framework AudioToolbox -framework CoreAudio -framework CoreFoundation \
  mac-codex-agent/native/voice_audio_bridge.mm \
  -o /tmp/libstopwatch_voice_audio.dylib
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py -v
```

Expected: dylib builds and all installer contract tests pass.

- [ ] **Step 8: Commit the native bridge**

```bash
git add mac-codex-agent/native mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py
git commit -m "Add native BlackHole audio bridge"
```

## Task 3: Wrap Core Audio And Guarantee Input Restoration

**Files:**
- Create: `mac-codex-agent/voice_audio.py`
- Create: `mac-codex-agent/tests/test_voice_audio.py`

- [ ] **Step 1: Write failing route-lease tests**

```python
def test_route_restores_the_exact_previous_uid(self):
    bridge = FakeBridge(default_uid="BuiltInMic", devices={"BlackHole 2ch": "BH2"})
    lease = DefaultInputLease(bridge)
    lease.acquire("BlackHole 2ch")
    self.assertEqual(bridge.default_uid, "BH2")
    lease.release()
    self.assertEqual(bridge.default_uid, "BuiltInMic")


def test_route_restores_after_sink_start_failure(self):
    bridge = FakeBridge(default_uid="BuiltInMic", fail_open=True)
    route = VoiceAudioRoute(bridge)
    with self.assertRaises(VoiceAudioError):
        route.start()
    self.assertEqual(bridge.default_uid, "BuiltInMic")
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_audio.py -v
```

Expected: import failure because `voice_audio.py` is absent.

- [ ] **Step 3: Implement the ctypes wrapper and leases**

Define:

```python
class VoiceAudioBridge:
    def find_device_uid(self, name: str) -> str: ...
    def default_input_uid(self) -> str: ...
    def set_default_input_uid(self, uid: str) -> None: ...
    def open(self, output_uid: str) -> None: ...
    def start(self) -> None: ...
    def write(self, samples: Sequence[int]) -> int: ...
    def dropped_frames(self) -> int: ...
    def stop(self) -> None: ...
    def close(self) -> None: ...


class DefaultInputLease:
    def acquire(self, device_name: str) -> None: ...
    def release(self) -> None: ...


class VoiceAudioRoute:
    def start(self) -> None: ...
    def write(self, samples: Sequence[int]) -> int: ...
    def stop(self) -> None: ...
```

Load the dylib from
`Contents/Resources/native/libstopwatch_voice_audio.dylib` when bundled and
from `mac-codex-agent/.native/` during development. `VoiceAudioRoute.start()`
must open and prime BlackHole before changing the default input. Every exception
after lease acquisition must call `release()` exactly once.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the Step 2 command.

Expected: all route and restoration tests pass.

- [ ] **Step 5: Commit the audio wrapper**

```bash
git add mac-codex-agent/voice_audio.py \
  mac-codex-agent/tests/test_voice_audio.py
git commit -m "Add recoverable BlackHole audio route"
```

## Task 4: Add Accessibility, Typeless, And Paired Fn Controls

**Files:**
- Create: `mac-codex-agent/voice_shortcut.py`
- Create: `mac-codex-agent/tests/test_voice_shortcut.py`
- Modify: `mac-codex-agent/requirements.txt`

- [ ] **Step 1: Write failing shortcut tests**

```python
def test_start_and_stop_emit_complete_fn_taps(self):
    events = []
    controller = FnShortcutController(
        trusted=lambda: True,
        typeless_running=lambda: True,
        post_key=lambda down: events.append(down),
        sleep=lambda _seconds: None,
    )
    controller.start()
    controller.stop()
    self.assertEqual(events, [True, False, True, False])


def test_failed_start_never_arms_a_stop_tap(self):
    controller = FnShortcutController(
        trusted=lambda: False,
        typeless_running=lambda: True,
    )
    with self.assertRaises(AccessibilityRequired):
        controller.start()
    self.assertFalse(controller.active)
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_shortcut.py -v
```

Expected: import failure because `voice_shortcut.py` is absent.

- [ ] **Step 3: Implement the shortcut boundary**

Use `AXIsProcessTrustedWithOptions` for readiness and Quartz key code `0x3F`
for Fn:

```python
def _post_fn(down: bool) -> None:
    event = Quartz.CGEventCreateKeyboardEvent(None, 0x3F, down)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, event)


class FnShortcutController:
    def start(self) -> None:
        if not self._trusted():
            raise AccessibilityRequired()
        if not self._typeless_running():
            raise TypelessNotRunning()
        self._tap()
        self.active = True

    def stop(self) -> None:
        if not self.active:
            return
        self._tap()
        self.active = False
```

Detect Typeless through `NSRunningApplication` bundle identifier
`now.typeless.desktop`; do not launch or modify it. Add the exact PyObjC
framework dependency required for Quartz/ApplicationServices.

- [ ] **Step 4: Run shortcut and dependency tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_shortcut.py \
  mac-codex-agent/tests/test_dependency_bootstrap.py -v
```

Expected: all tests pass.

- [ ] **Step 5: Commit the shortcut control**

```bash
git add mac-codex-agent/voice_shortcut.py \
  mac-codex-agent/tests/test_voice_shortcut.py \
  mac-codex-agent/requirements.txt
git commit -m "Add paired Typeless Fn control"
```

## Task 5: Define The Production Voice Protocol On Mac

**Files:**
- Create: `mac-codex-agent/voice_input_protocol.py`
- Create: `mac-codex-agent/tests/test_voice_input_protocol.py`

- [ ] **Step 1: Write failing byte-layout tests**

```python
def test_ready_control_matches_firmware_layout(self):
    self.assertEqual(
        encode_ready(8192),
        bytes.fromhex("0201000000200000"),
    )


def test_decodes_begin_audio_end_and_rejects_stale_versions(self):
    begin = pack_notification(PTT_BEGIN, 7, 0, 100, b"")
    self.assertEqual(decode_notification(begin).session, 7)
    with self.assertRaises(ValueError):
        decode_notification(bytes([1]) + begin[1:])
```

- [ ] **Step 2: Run the protocol tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_input_protocol.py -v
```

Expected: import failure because `voice_input_protocol.py` is absent.

- [ ] **Step 3: Implement version-2 framing**

Use:

```python
PROTOCOL_VERSION = 2
READY = 1
UNAVAILABLE = 2
PTT_BEGIN = 1
AUDIO = 2
PTT_END = 3
VOICE_ERROR = 4

CONTROL = struct.Struct("<BBBBI")
HEADER = struct.Struct("<BBHIIH")
END_PAYLOAD = struct.Struct("<IIIIIIH")
ERROR_PAYLOAD = struct.Struct("<HH")
```

`encode_ready()` must accept only 8192 B/s. `decode_notification()` must reject
zero sessions, unknown kinds, payload-length mismatches, an AUDIO payload other
than 164 bytes, and a nonempty PTT_BEGIN payload. Define an immutable
`EndSummary` containing generated packets, accepted packets, queue overflows,
encode failures, captured samples, elapsed milliseconds, and negotiated MTU.

- [ ] **Step 4: Run protocol tests and verify GREEN**

Run the Step 2 command.

Expected: all production protocol tests pass.

- [ ] **Step 5: Commit the Mac protocol**

```bash
git add mac-codex-agent/voice_input_protocol.py \
  mac-codex-agent/tests/test_voice_input_protocol.py
git commit -m "Add production BLE voice protocol"
```

## Task 6: Implement The Mac Voice Session State Machine

**Files:**
- Create: `mac-codex-agent/voice_session.py`
- Create: `mac-codex-agent/tests/test_voice_session.py`
- Modify: `mac-codex-agent/voice_probe_protocol.py`

- [ ] **Step 1: Extract the tested ADPCM decoder**

Move `decode_ima_adpcm_block`, `_INDEX_TABLE`, and `_STEP_TABLE` into a focused
`ima_adpcm.py` module, then import it from both probe and production protocol
modules. First change the probe tests to import the decoder from
`ima_adpcm.py`, run them to observe the missing-module failure, then add the
module without changing decoder behavior.

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_protocol.py -v
```

Expected after extraction implementation: all existing decoder vectors pass.

- [ ] **Step 2: Write failing session lifecycle tests**

```python
async def test_begin_audio_end_pairs_route_and_shortcut(self):
    route = FakeRoute()
    shortcut = FakeShortcut()
    service = VoiceSessionService(route, shortcut)
    await service.handle(begin_packet(session=7, sequence=0))
    await service.handle(audio_packet(session=7, sequence=1, silence_block()))
    await service.handle(end_packet(session=7, sequence=2))
    self.assertEqual(route.calls, ["start", "write:320", "stop"])
    self.assertEqual(shortcut.calls, ["start", "stop"])
    self.assertEqual(service.state, VoiceState.READY)


async def test_disconnect_restores_route_and_stops_typeless(self):
    service = started_service()
    await service.disconnect()
    self.assertFalse(service.shortcut.active)
    self.assertFalse(service.route.active)
```

- [ ] **Step 3: Run the session tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_session.py -v
```

Expected: import failure because `voice_session.py` is absent.

- [ ] **Step 4: Implement bounded session coordination**

Define:

```python
class VoiceState(enum.Enum):
    DRIVER_REQUIRED = "driver_required"
    RESTART_REQUIRED = "restart_required"
    ACCESSIBILITY_REQUIRED = "accessibility_required"
    TYPELESS_NOT_RUNNING = "typeless_not_running"
    READY = "ready"
    RECORDING = "recording"
    ERROR = "error"


class VoiceSessionService:
    async def prepare(self) -> VoiceState: ...
    async def handle(self, packet: Notification) -> None: ...
    async def disconnect(self) -> None: ...
    async def close(self) -> None: ...
```

On `PTT_BEGIN`, start the route before the shortcut. On AUDIO, decode 320
samples and write them. For a forward sequence gap, write one 320-sample
silence block per missing AUDIO sequence, capped at five blocks; larger gaps
abort. On `PTT_END`, validate counters, wait at most 150 ms for the native
queue to drain, stop the shortcut, stop the route, and return to READY.
Sequence regressions, stale sessions, and malformed summaries abort through one
idempotent cleanup method.

- [ ] **Step 5: Run session and probe protocol tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_session.py \
  mac-codex-agent/tests/test_voice_probe_protocol.py -v
```

Expected: all tests pass.

- [ ] **Step 6: Commit the session service**

```bash
git add mac-codex-agent/ima_adpcm.py \
  mac-codex-agent/voice_probe_protocol.py \
  mac-codex-agent/voice_session.py \
  mac-codex-agent/tests/test_voice_probe_protocol.py \
  mac-codex-agent/tests/test_voice_session.py
git commit -m "Add Mac voice session lifecycle"
```

## Task 7: Integrate Voice Into The Existing BLE Agent Runtime

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`

- [ ] **Step 1: Write failing BLE integration tests**

Add tests proving:

```python
async def test_connect_subscribes_before_announcing_voice_ready(self):
    transport = BleStatusTransport(client_factory=lambda *_: client)
    await transport.connect()
    self.assertEqual(
        client.calls[:2],
        [("start_notify", VOICE_TX_UUID), ("write", VOICE_CONTROL_UUID, READY_BYTES)],
    )


async def test_runtime_disconnect_cleans_voice_before_ble_disconnect(self):
    await runtime._run_session()
    self.assertEqual(events[-2:], ["voice.disconnect", "ble.disconnect"])
```

Extend presentation tests so each `VoiceState` maps to one exact menu string.

- [ ] **Step 2: Run integration tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_runtime.py \
  mac-codex-agent/tests/test_app_state.py -v
```

Expected: failures because the transport has no voice subscription or state.

- [ ] **Step 3: Extend `BleStatusTransport`**

Add `VOICE_CONTROL_UUID` and `VOICE_TX_UUID`, a bounded asyncio notification
queue, and:

```python
async def enable_voice(self, ready_payload: bytes, callback) -> None:
    await self._client.start_notify(VOICE_TX_UUID, callback)
    await self._client.write_gatt_char(
        VOICE_CONTROL_UUID, ready_payload, response=True
    )


async def disable_voice(self, unavailable_payload: bytes) -> None:
    if self.connected:
        await self._client.write_gatt_char(
            VOICE_CONTROL_UUID, unavailable_payload, response=True
        )
        await self._client.stop_notify(VOICE_TX_UUID)
```

The Bleak callback must only copy bytes and schedule queue insertion on the
event loop. Decoding and Core Audio work occur in an asyncio consumer task.

- [ ] **Step 4: Extend runtime ownership and snapshots**

Create one `VoiceSessionService` per BLE session. Call `prepare()` before
writing `READY`; send `UNAVAILABLE` with a compact reason for all non-ready
states. Always invoke voice cleanup before BLE disconnect. Preserve status
collection when voice is unavailable.

Add `voice_state` and `voice_detail` to `AgentSnapshot` and `PresentableState`.
Do not alter `connection_enabled`, login behavior, or retry delays.

- [ ] **Step 5: Run integration tests and verify GREEN**

Run the Step 2 command.

Expected: all tests pass, including existing status and time-sync tests.

- [ ] **Step 6: Commit runtime integration**

```bash
git add mac-codex-agent/agent.py mac-codex-agent/runtime.py \
  mac-codex-agent/app_state.py mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_runtime.py \
  mac-codex-agent/tests/test_app_state.py
git commit -m "Integrate voice with the Codex Agent runtime"
```

## Task 8: Integrate Driver Setup And Voice Status Into The Menu

**Files:**
- Modify: `mac-codex-agent/voice_driver.py`
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_voice_driver.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

- [ ] **Step 1: Write failing installer and menu tests**

```python
def test_installer_uses_osascript_argv_without_shell_interpolation(self):
    installer.install(package)
    self.assertEqual(command[:3], ["/usr/bin/osascript", "-e", INSTALL_SCRIPT])
    self.assertEqual(command[-1], str(package))


def test_menu_shows_setup_only_when_driver_is_missing(self):
    delegate.refresh_(None)
    self.assertEqual(delegate.voice_label.stringValue(), "Voice Input: Driver Required")
    self.assertFalse(delegate.setup_voice_item.isHidden())
    self.assertTrue(delegate.accessibility_item.isHidden())
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_driver.py \
  mac-codex-agent/tests/test_menu_app.py -v
```

Expected: missing installer and menu voice controls.

- [ ] **Step 3: Implement authorized installation**

Use a fixed AppleScript passed through `osascript -e`, with the package path as
an argv value:

```applescript
on run argv
    set packagePath to item 1 of argv
    do shell script "/usr/sbin/installer -pkg " & quoted form of packagePath & " -target /" with administrator privileges
end run
```

Before invoking it, verify the package hash. After success, verify
`pkgutil --pkg-info audio.existential.BlackHole2ch`, then poll Core Audio for
up to five seconds. Return READY if visible, otherwise RESTART_REQUIRED. Never
restart, kill, or relaunch Core Audio automatically.

- [ ] **Step 4: Add menu rows and actions**

Add one informational row below Device and conditionally visible actions:

```text
Voice Input: Driver Required
Set Up Watch Voice Input...
Open Accessibility Settings...
```

Run installation on a worker thread so AppKit remains responsive. Disable setup
while installation is active. The Accessibility action opens:

```text
x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility
```

Do not add a Dock icon, second status item, login item, or subprocess that
survives the Agent.

- [ ] **Step 5: Run menu and driver tests**

Run the Step 2 command.

Expected: all menu visibility, action, and installation tests pass.

- [ ] **Step 6: Commit setup UI**

```bash
git add mac-codex-agent/voice_driver.py mac-codex-agent/menu_app.py \
  mac-codex-agent/runtime.py \
  mac-codex-agent/tests/test_voice_driver.py \
  mac-codex-agent/tests/test_menu_app.py
git commit -m "Add integrated watch voice setup"
```

## Task 9: Package The Pinned Driver And Voice Modules

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`

- [ ] **Step 1: Write failing packaging contracts**

Assert the installer script contains:

```python
for module in (
    "voice_driver.py",
    "voice_shortcut.py",
    "voice_audio.py",
    "voice_input_protocol.py",
    "voice_session.py",
    "ima_adpcm.py",
):
    self.assertIn(module, script)

self.assertIn(BLACKHOLE_URL, script)
self.assertIn(BLACKHOLE_SHA256, script)
self.assertIn('Contents/Resources/Drivers', script)
self.assertNotIn("brew install blackhole", script)
```

- [ ] **Step 2: Run installer tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py -v
```

Expected: failures because the driver and modules are not packaged.

- [ ] **Step 3: Add deterministic download and packaging**

Use:

```bash
DRIVER_CACHE="$ROOT_DIR/.driver-cache"
DRIVER_PACKAGE="$DRIVER_CACHE/BlackHole2ch-0.7.1.pkg"
mkdir -p "$DRIVER_CACHE" "$APP_RESOURCES/Drivers"
if [[ ! -f "$DRIVER_PACKAGE" ]]; then
    curl --fail --location --proto '=https' --tlsv1.2 \
        --output "$DRIVER_PACKAGE.tmp" \
        "https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg"
    mv "$DRIVER_PACKAGE.tmp" "$DRIVER_PACKAGE"
fi
printf '%s  %s\n' \
  '57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf' \
  "$DRIVER_PACKAGE" | shasum -a 256 --check
cp "$DRIVER_PACKAGE" "$APP_RESOURCES/Drivers/"
```

Copy all voice Python modules and the native dylib into the generated app.
Extend `Info.plist` with an Accessibility usage explanation only if the
framework requires one; do not add microphone permission to the Agent because
the Agent receives BLE data and does not open a physical Mac microphone.

- [ ] **Step 4: Build the local app and inspect its contents**

Run:

```bash
./mac-codex-agent/install_launch_agent.sh
shasum -a 256 \
  "$HOME/Applications/StopWatch Codex Agent.app/Contents/Resources/Drivers/BlackHole2ch-0.7.1.pkg"
find "$HOME/Applications/StopWatch Codex Agent.app/Contents/Resources" \
  -maxdepth 3 -type f | sort
```

Expected: package hash equals the pinned hash; all voice modules and the dylib
are present; installation does not start the Agent.

- [ ] **Step 5: Run installer tests and commit**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py -v
git add mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py
git commit -m "Package the local voice input runtime"
```

## Task 10: Add The Firmware Production Protocol And State Machine

**Files:**
- Create: `main/apps/app_codex_status/voice_input_protocol.h`
- Create: `main/apps/app_codex_status/voice_input_protocol.cpp`
- Create: `main/apps/app_codex_status/voice_input_session.h`
- Create: `main/apps/app_codex_status/voice_input_session.cpp`
- Create: `tests/voice_input_protocol_test.cpp`
- Create: `tests/voice_input_session_test.cpp`

- [ ] **Step 1: Write the failing protocol host test**

```cpp
int main()
{
    const uint8_t ready[] = {0x02, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00};
    VoiceReadyControl decoded = {};
    assert(voice_input::decodeReady(ready, sizeof(ready), decoded));
    assert(decoded.target_bps == 8192);

    VoiceNotification begin = {};
    begin.kind = VoiceKind::PttBegin;
    begin.session = 7;
    begin.sequence = 0;
    uint8_t bytes[32] = {};
    assert(voice_input::encodeNotification(begin, bytes, sizeof(bytes)) == 14);
    assert(bytes[0] == 2 && bytes[1] == 1);
    return 0;
}
```

- [ ] **Step 2: Compile the protocol test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/voice_input_protocol_test.cpp \
  main/apps/app_codex_status/voice_input_protocol.cpp \
  -o /tmp/voice_input_protocol_test
```

Expected: missing source/header failure.

- [ ] **Step 3: Implement byte-identical firmware framing**

Mirror the Python constants and layouts from Task 5. Reject a readiness bitrate
other than 8192, zero sessions, payloads larger than the negotiated
notification capacity, and non-164-byte AUDIO payloads. Keep all structs
logical; serialize fields explicitly in little-endian order rather than
`memcpy` of packed C++ structs.

- [ ] **Step 4: Run the protocol host test**

Run the Step 2 compile command followed by:

```bash
/tmp/voice_input_protocol_test
```

Expected: exit code 0.

- [ ] **Step 5: Write and implement the pure session state test**

Test exact transitions:

```cpp
VoiceInputSession session;
assert(session.state() == VoiceInputState::Unavailable);
session.setReady(true);
assert(session.press(100));
assert(session.state() == VoiceInputState::Starting);
session.started(7, 110);
assert(session.state() == VoiceInputState::Streaming);
assert(session.release(900));
session.stopped();
assert(session.state() == VoiceInputState::Ready);
session.disconnect();
assert(session.state() == VoiceInputState::Unavailable);
```

Also test duplicate press/release, zero session rejection, monotonic sequence
allocation, home cancellation, and counter snapshots.

- [ ] **Step 6: Compile and run both firmware host tests**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/voice_input_session_test.cpp \
  main/apps/app_codex_status/voice_input_session.cpp \
  -o /tmp/voice_input_session_test
/tmp/voice_input_session_test
/tmp/voice_input_protocol_test
```

Expected: both exit 0.

- [ ] **Step 7: Commit firmware protocol and state**

```bash
git add main/apps/app_codex_status/voice_input_protocol.* \
  main/apps/app_codex_status/voice_input_session.* \
  tests/voice_input_protocol_test.cpp tests/voice_input_session_test.cpp
git commit -m "Add watch push-to-talk protocol"
```

## Task 11: Extract The Shared Firmware Capture Pipeline

**Files:**
- Create: `main/apps/app_codex_status/voice_capture_pipeline.h`
- Create: `main/apps/app_codex_status/voice_capture_pipeline.cpp`
- Create: `tests/voice_capture_pipeline_contract_test.py`
- Modify: `main/apps/app_codex_status/ble_voice_probe_controller.h`
- Modify: `main/apps/app_codex_status/ble_voice_probe_controller.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`

- [ ] **Step 1: Write failing extraction contracts**

Assert the new pipeline owns:

```python
self.assertIn("GetResourceSupervisor().acquireCapture(RuntimeOwner::Codex)", pipeline)
self.assertIn("GetStopWatchAudioBackend().openRecorder(44100, 16, 1)", pipeline)
self.assertIn("esp_ae_rate_cvt_open", pipeline)
self.assertIn("ima_adpcm_encode_block", pipeline)
self.assertNotIn("notify_voice(", pipeline)
self.assertNotIn("ble_gatts_notify", pipeline)
```

Assert the probe controller constructs the pipeline and no longer directly
opens the recorder.

- [ ] **Step 2: Run contracts and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  tests/voice_capture_pipeline_contract_test.py \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
```

Expected: missing pipeline source.

- [ ] **Step 3: Extract without changing probe behavior**

Define:

```cpp
class VoiceCapturePipeline {
public:
    bool open(VoiceCaptureError& error);
    VoiceCaptureResult captureBlock(
        std::array<uint8_t, 164>& adpcm,
        uint32_t& captured_samples,
        VoiceCaptureError& error);
    void requestStop();
    void close();
};
```

Move recorder ownership, 44.1-to-16 kHz resampling, the off-stack 1764-byte
capture buffer, ADPCM encoding, and cleanup into this class. Keep BLE queueing,
probe duration, synthetic generation, and probe summaries in
`BleVoiceProbeController`. Add `voice_capture_pipeline.cpp` to the component
sources in `main/CMakeLists.txt` in the same change so this commit remains
linkable.

- [ ] **Step 4: Run all existing voice host and contract tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  tests/voice_capture_pipeline_contract_test.py \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/ble_voice_probe_session_test.cpp \
  main/apps/app_codex_status/ble_voice_probe_session.cpp \
  -o /tmp/ble_voice_probe_session_test
/tmp/ble_voice_probe_session_test
source "${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh"
idf.py -B build-unified build
```

Expected: all tests and the firmware link pass with no probe protocol changes.

- [ ] **Step 5: Commit the shared capture path**

```bash
git add main/apps/app_codex_status/voice_capture_pipeline.* \
  main/apps/app_codex_status/ble_voice_probe_controller.* \
  main/CMakeLists.txt \
  tests/voice_capture_pipeline_contract_test.py \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py
git commit -m "Extract the validated watch voice capture path"
```

## Task 12: Implement Firmware Push To Talk And Restore Production BLE Identity

**Files:**
- Create: `main/apps/app_codex_status/voice_input_controller.h`
- Create: `main/apps/app_codex_status/voice_input_controller.cpp`
- Modify: `main/apps/app_codex_status/codex_ble_transport.h`
- Modify: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`

- [ ] **Step 1: Write failing firmware integration contracts**

Add assertions that:

```python
self.assertIn("VoiceInputController", app_source)
self.assertIn("handle_button", app_source)
self.assertIn("btnB.isPressed()", app_source)
self.assertIn("cancel_for_home", app_source)
self.assertNotIn("kVoiceProbeIdentityXor", transport)
self.assertIn("voice_input::kProtocolVersion", transport)
```

Assert voice characteristics remain unencrypted while status keeps
`BLE_GATT_CHR_F_WRITE_ENC`.

- [ ] **Step 2: Run contracts and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py -v
```

Expected: missing production controller and retained probe identity XOR.

- [ ] **Step 3: Implement the production controller**

`VoiceInputController` owns:

```cpp
void set_ready(bool ready, uint16_t negotiated_mtu);
void handle_button(bool pressed, uint32_t now_ms);
void cancel_for_home(uint32_t now_ms);
void handle_disconnect();
void poll(uint32_t now_ms);
bool take_notification(voice_input::Notification& output);
VoiceInputUiState ui_state() const;
```

On the down edge, open `VoiceCapturePipeline`, allocate a nonzero session,
enqueue PTT_BEGIN, and start an 8 KB producer task. Keep at most 300 ms of
pre-roll in the existing bounded outgoing queue. On the up edge, request the
pipeline stop, encode the final complete block, enqueue PTT_END after AUDIO, and
release resources. Use the tested eight-kilobyte producer stack.

- [ ] **Step 4: Route both protocol versions in BLE transport**

Keep version-1 probe controls for the standalone diagnostic. Decode version-2
READY/UNAVAILABLE into a separate queue consumed by
`VoiceInputController`. Restore the normal NimBLE identity path by removing the
diagnostic XOR address and using `ble_hs_util_ensure_addr(0)` plus
`ble_hs_id_infer_auto(0, ...)`. Keep one central connection and existing status
encryption.

- [ ] **Step 5: Integrate right-button edges without breaking wake/home**

In `AppCodexStatus::onRunning()`:

1. call `update_screen_off_control()` first;
2. if that call wakes the screen, do not forward the current button press;
3. forward `btnB.isPressed()` edge changes to the voice controller;
4. if both buttons enter the existing GoHome gesture, cancel voice before
   closing;
5. poll voice and probe controllers, but allow only one microphone owner;
6. render `VOICE`, `VOICE OFF`, or `VOICE ERR` through the existing status
   label without adding a new page.

- [ ] **Step 6: Build focused firmware modules**

Run:

```bash
source "${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh"
idf.py -B build-unified reconfigure
idf.py -B build-unified build
```

Expected: build succeeds with no duplicate symbols or component-cycle errors.

- [ ] **Step 7: Run firmware contracts and commit**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py \
  tests/voice_capture_pipeline_contract_test.py -v
git add main/apps/app_codex_status/voice_input_controller.* \
  main/apps/app_codex_status/codex_ble_transport.* \
  main/apps/app_codex_status/app_codex_status.* main/CMakeLists.txt \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py
git commit -m "Connect watch push to talk to BLE voice"
```

## Task 13: Extend Unified Validation And Documentation

**Files:**
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/README.md`
- Modify: `docs/ble-voice-link-probe.md`
- Modify: `mac-codex-agent/tests/test_install_script.py`

- [ ] **Step 1: Write failing validation-stage contracts**

Extend `test_install_script.py` or a focused validation contract to assert the
runner compiles and executes:

```text
voice_input_protocol_test
voice_input_session_test
voice_audio_ring_test
```

and that the final stage count is internally consistent.

- [ ] **Step 2: Run the contract and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py -v
```

Expected: missing validation stages.

- [ ] **Step 3: Add exact validation commands**

Insert host build/run stages using the same `-std=c++17 -Wall -Wextra -Werror`
policy as existing tests. Keep the isolated clean firmware build and
`git diff --check` as the final two stages.

- [ ] **Step 4: Update user and diagnostic documentation**

Document:

- setup happens from the existing menu;
- one administrator authorization and a restart may be required;
- Typeless must already be running with Fn configured;
- right-button press/release behavior;
- previous default input restoration;
- no login startup;
- unencrypted personal-use boundary;
- how to remove BlackHole with its official package receipt;
- the standalone probe remains diagnostic and requires the Agent to be quit.

- [ ] **Step 5: Run the full offline gate**

Run:

```bash
PYTHON="$PWD/mac-codex-agent/.venv/bin/python" \
  ./scripts/run_unified_offline_validation.sh
```

Expected: all Python tests, C++ host tests, native ring test, isolated clean
ESP-IDF build, capacity gate, and whitespace check pass.

- [ ] **Step 6: Commit validation and docs**

```bash
git add scripts/run_unified_offline_validation.sh \
  mac-codex-agent/README.md docs/ble-voice-link-probe.md \
  mac-codex-agent/tests/test_install_script.py
git commit -m "Validate and document watch voice input"
```

## Task 14: Install, Flash, And Run Physical Typeless Acceptance

**Files:**
- Generated only: `build-unified/StopWatch-UserDemo.bin`
- Generated only: `$HOME/Applications/StopWatch Codex Agent.app`
- Evidence only: `/tmp/stopwatch-typeless-voice/<timestamp>/`

- [ ] **Step 1: Record exact source and artifact identities**

Run:

```bash
EVIDENCE="/tmp/stopwatch-typeless-voice/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$EVIDENCE"
git rev-parse HEAD | tee "$EVIDENCE/git-revision.txt"
shasum -a 256 build-unified/StopWatch-UserDemo.bin \
  | tee "$EVIDENCE/firmware.sha256"
```

- [ ] **Step 2: Build the Agent without starting it**

Run:

```bash
./mac-codex-agent/install_launch_agent.sh \
  > "$EVIDENCE/agent-build.log" 2>&1
pgrep -fl 'StopWatch Codex Agent|StopWatchCodexAgent' \
  > "$EVIDENCE/agent-process-before.txt" || true
```

Expected: app exists and no Agent process was started by installation.

- [ ] **Step 3: Use the Agent menu to install BlackHole**

Open the Agent manually, select `Set Up Watch Voice Input...`, approve the
standard administrator prompt, and preserve the Agent log. If the menu reports
`Restart Required`, restart the Mac once, manually reopen Typeless and the
Agent, and verify `Voice Input: Ready`.

- [ ] **Step 4: Verify Fn synthesis before flashing**

Focus a disposable plain-text field. Use a temporary development action or
focused harness that calls `FnShortcutController.start()` and `.stop()` once.
Verify Typeless visibly starts and stops. Record:

```text
Fn start observed: PASS
Fn stop observed: PASS
Accessibility authorized: PASS
```

Do not proceed if either tap is ignored.

- [ ] **Step 5: Flash the exact firmware**

Back up flash if the existing backup policy requires a new backup for this
device state. Re-detect the connected serial port immediately before flashing,
then run:

```bash
PORT="$(python3 -c 'from scripts.watch_debug import discover_port; print(discover_port())')"
printf '%s\n' "$PORT" | tee "$EVIDENCE/serial-port.txt"
idf.py -B build-unified -p "$PORT" flash \
  | tee "$EVIDENCE/flash.log"
python3 scripts/watch_debug.py \
  --port "$PORT" --timeout 15 app list \
  | tee "$EVIDENCE/boot-app-list.txt"
```

Expected: flash hashes verify and the app list includes `codex` and
`assistant`.

- [ ] **Step 6: Run the 30-second Typeless test**

Record the original default input UID. Open Codex on the watch, wait for BLE and
voice ready, focus a plain-text field, hold the right button, speak continuous
Mandarin for at least 30 seconds, and release. Capture:

- packet and summary counters from Agent and serial logs;
- right-button-to-Fn-start latency;
- release-to-Fn-stop latency;
- default input UID during and after the session;
- resulting Typeless text;
- watch uptime before and after.

Acceptance: intelligible text, start latency at most 250 ms, stop latency at
most 350 ms, zero encode failures/queue overflows, and exact input restoration.

- [ ] **Step 7: Run interruption recovery cases**

Repeat short sessions and interrupt separately by:

1. closing the watch Codex app;
2. using the two-button home gesture;
3. disabling watch connection from the menu;
4. quitting the Agent.

After every case, verify Typeless is not still recording, the original input
UID is restored, Assistant can acquire the watch microphone, and the Agent
does not restart after Quit.

- [ ] **Step 8: Record the physical decision**

Write `$EVIDENCE/result.txt` with exact PASS/FAIL rows for BlackHole setup, Fn
start/stop, 30-second transcription, latency, packet counters, input
restoration, each interruption case, Assistant recovery, and no-autostart.
Keep all evidence outside Git.

- [ ] **Step 9: Commit only fixes proven necessary by acceptance**

If physical testing requires code changes, return to the failing automated
test for that behavior, commit each fix separately, rebuild, reflash, and rerun
the affected physical case. End only when `git status --short` is clean and the
evidence identifies the final tested commit and binary hashes.

## Final Verification Checklist

- [ ] `git status --short --branch` is clean in the voice worktree.
- [ ] The main worktree's unrelated `.superpowers/` and `hardware-evidence/`
      files remain untouched.
- [ ] The generated Agent contains the pinned BlackHole package, voice modules,
      and native dylib, but Git contains none of those generated binaries.
- [ ] Installation still does not start the Agent or add a LaunchAgent.
- [ ] Full offline validation passes from a clean candidate build.
- [ ] Physical evidence proves Typeless Fn response, intelligible watch audio,
      latency limits, input restoration, interruption cleanup, Assistant
      recovery, and no watch reboot.
- [ ] The final report distinguishes automated, flash, boot, and manual
      Typeless evidence.

## Task 15: Measure AES-CCM Voice Performance Before Production Integration

**Files:**
- Create: `main/apps/app_codex_status/voice_probe_crypto.h`
- Create: `main/apps/app_codex_status/voice_probe_crypto.cpp`
- Create: `tests/voice_probe_crypto_test.cpp`
- Create: `mac-codex-agent/voice_probe_crypto.py`
- Create: `mac-codex-agent/tests/test_voice_probe_crypto.py`
- Modify: `mac-codex-agent/requirements.txt`
- Modify: `mac-codex-agent/tests/test_dependency_bootstrap.py`
- Modify: `main/apps/app_codex_status/ble_voice_probe_protocol.h`
- Modify: `main/apps/app_codex_status/ble_voice_probe_protocol.cpp`
- Modify: `main/apps/app_codex_status/ble_voice_probe_controller.h`
- Modify: `main/apps/app_codex_status/ble_voice_probe_controller.cpp`
- Modify: `mac-codex-agent/voice_probe_protocol.py`
- Modify: `mac-codex-agent/ble_voice_probe.py`
- Modify: `mac-codex-agent/tests/test_voice_probe_protocol.py`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe.py`
- Modify: `main/CMakeLists.txt`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `docs/ble-voice-link-probe.md`

- [ ] **Step 1: Write failing cross-language crypto vectors**

Use one fixed diagnostic vector in both C++ and Python:

```text
key:       000102030405060708090a0b0c0d0e0f
nonce:     101112131415161718191a1b1c
aad:       020207000100000064000000ac00
plaintext: 164 zero bytes
tag size:  8 bytes
```

Tests must assert byte-identical ciphertext/tag, successful decryption, and
rejection after changing one ciphertext, tag, nonce, or AAD byte. Generate the
expected ciphertext once using Python's `AESCCM`, record it as a literal vector
in both tests, and never calculate the expected value through the function
under test.

- [ ] **Step 2: Run focused crypto tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_crypto.py -v
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/voice_probe_crypto_test.cpp \
  main/apps/app_codex_status/voice_probe_crypto.cpp \
  -lmbedtls -lmbedcrypto -lmbedx509 \
  -o /tmp/voice_probe_crypto_test
```

Expected: missing Python and firmware crypto modules.

- [ ] **Step 3: Implement diagnostic AES-128-CCM**

Python uses `cryptography.hazmat.primitives.ciphers.aead.AESCCM` with an
8-byte tag. Firmware uses `mbedtls_ccm_setkey`,
`mbedtls_ccm_encrypt_and_tag`, and `mbedtls_ccm_auth_decrypt`. Expose:

```cpp
bool encrypt(const uint8_t* aad, size_t aad_len,
             const uint8_t* plaintext, size_t plaintext_len,
             const Nonce& nonce, Ciphertext& output,
             uint32_t& elapsed_us);
bool decrypt(const uint8_t* aad, size_t aad_len,
             const uint8_t* ciphertext, size_t ciphertext_len,
             const Nonce& nonce, Plaintext& output);
```

The diagnostic key is confined to `voice_probe_crypto.*` and its Python
counterpart. Derive the 13-byte benchmark nonce from a constant three-byte
domain, session, sequence, and watch timestamp. Reject nonce/session zero,
plaintext lengths other than 164 for microphone mode, and ciphertext lengths
other than 172.

Add `cryptography>=45,<47` to `requirements.txt` and extend the dependency
bootstrap test so a fresh Agent environment must import `AESCCM`; do not rely
on a one-off package installed only in the current virtual environment.

- [ ] **Step 4: Add encrypted probe mode and timing counters**

Add `MICROPHONE_ADPCM_AES_CCM = 3`. For this mode, encrypt each encoded
164-byte ADPCM block, place 172 ciphertext/tag bytes in the existing
notification payload, and authenticate the 14-byte header as AAD. Extend the
stop summary and JSON report with:

```text
crypto_packets
crypto_failures
crypto_total_us
crypto_max_us
crypto_p99_us
invalid_tags
encrypted_characteristic_value_bytes
```

The Mac decrypts before ADPCM decoding. Invalid tags never reach the decoder
and make the run fail closed. Keep existing unencrypted modes byte-compatible.

- [ ] **Step 5: Run focused protocol, client, and firmware tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_voice_probe_crypto.py \
  mac-codex-agent/tests/test_voice_probe_protocol.py \
  mac-codex-agent/tests/test_ble_voice_probe.py -v
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/voice_probe_crypto_test.cpp \
  main/apps/app_codex_status/voice_probe_crypto.cpp \
  -o /tmp/voice_probe_crypto_test
/tmp/voice_probe_crypto_test
```

Use the ESP-IDF component libraries or an isolated ESP-IDF hostable build for
the final C++ command if the system linker does not expose mbedTLS. Record the
exact successful command in the task report.

- [ ] **Step 6: Run the full offline validation**

Run:

```bash
PYTHON="$PWD/mac-codex-agent/.venv/bin/python" \
  ./scripts/run_unified_offline_validation.sh
```

Expected: all existing tests, new crypto vectors, clean unified firmware build,
capacity gate, and whitespace check pass.

- [ ] **Step 7: Commit the diagnostic encryption probe**

```bash
git add main/apps/app_codex_status/voice_probe_crypto.* \
  main/apps/app_codex_status/ble_voice_probe_protocol.* \
  main/apps/app_codex_status/ble_voice_probe_controller.* \
  tests/voice_probe_crypto_test.cpp \
  mac-codex-agent/voice_probe_crypto.py \
  mac-codex-agent/voice_probe_protocol.py \
  mac-codex-agent/ble_voice_probe.py \
  mac-codex-agent/tests/test_voice_probe_crypto.py \
  mac-codex-agent/tests/test_voice_probe_protocol.py \
  mac-codex-agent/tests/test_ble_voice_probe.py \
  mac-codex-agent/requirements.txt \
  mac-codex-agent/tests/test_dependency_bootstrap.py \
  main/CMakeLists.txt scripts/run_unified_offline_validation.sh \
  docs/ble-voice-link-probe.md
git commit -m "Add encrypted BLE voice performance probe"
```

- [ ] **Step 8: Flash the exact diagnostic artifact**

Record the commit and binary hash, auto-detect the connected watch port, flash,
and verify boot:

```bash
EVIDENCE="/tmp/stopwatch-ble-voice-probe/$(date +%Y%m%d)-aes-ccm-gate"
mkdir -p "$EVIDENCE"
git rev-parse HEAD | tee "$EVIDENCE/firmware-git-revision.txt"
shasum -a 256 build-unified/StopWatch-UserDemo.bin \
  | tee "$EVIDENCE/firmware-app.sha256"
PORT="$(python3 -c 'from scripts.watch_debug import discover_port; print(discover_port())')"
idf.py -B build-unified -p "$PORT" flash | tee "$EVIDENCE/flash.log"
python3 scripts/watch_debug.py --port "$PORT" --timeout 15 app list \
  | tee "$EVIDENCE/boot-app-list.txt"
```

- [ ] **Step 9: Run alternating physical profiles**

With the installed Agent fully quit, run three adjacent baseline/encrypted
8,192 B/s 60-second synthetic pairs and three adjacent baseline/encrypted
30-second microphone pairs. Run one encrypted 16,384 B/s synthetic profile for
capacity evidence. Preserve JSON, WAV, command logs, watch serial log, and a
manual Mandarin listening result under the evidence directory.

- [ ] **Step 10: Apply the decision rule**

Classify encryption as small only if every encrypted 8 KiB/s and microphone run
has zero transport/crypto failures, p95 at most 40 ms, max gap at most 100 ms,
encrypted p95 no more than 5 ms above its adjacent baseline, p99 encryption at
most 1 ms, exact 186-byte encrypted characteristic values, WAV duration error
at most 1 percent, and intelligible Mandarin.

If all pass, mark the gate complete and amend the production tasks for
AES-CCM. If a crypto-specific measurement misses, stop and ask the user before
adding encryption.
