# Codex Agent Menu Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the invisible, auto-restarting LaunchAgent with a manually launched macOS menu bar application that displays BLE state and provides persistent connection controls.

**Architecture:** AppKit runs on the main thread and renders immutable agent snapshots. An `AgentController` owns a worker thread whose asyncio runtime serializes BLE scan, connection, retry, reconnect, disable, and stop commands. The existing collector, transport, framing, and status sender remain reusable and independent of AppKit.

**Tech Stack:** Python 3.9, PyObjC/AppKit 11.x, asyncio, Bleak/CoreBluetooth, `NSUserDefaults`, unittest, shell installer, ad-hoc macOS code signing.

---

## File Structure

- Create `mac-codex-agent/app_state.py`: connection-state enum, immutable snapshots, menu presentation, and preference abstraction.
- Create `mac-codex-agent/runtime.py`: serialized async command loop and thread-owning controller.
- Create `mac-codex-agent/menu_app.py`: AppKit status item, menu actions, log setup, and process lifecycle.
- Create `mac-codex-agent/sitecustomize.py`: bundle-only bootstrap that starts `menu_app` when the copied Python executable is launched without a script argument.
- Create `mac-codex-agent/tests/test_app_state.py`: state/menu/persistence tests.
- Create `mac-codex-agent/tests/test_runtime.py`: async command and shutdown tests using fake transports.
- Create `mac-codex-agent/tests/test_install_script.py`: migration and no-autostart installer assertions.
- Modify `mac-codex-agent/agent.py`: expose state-aware BLE discovery and remove the standalone infinite-loop entry point.
- Modify `mac-codex-agent/install_launch_agent.sh`: package a manually opened app bundle and remove the legacy LaunchAgent.
- Modify `mac-codex-agent/requirements.txt`: make the AppKit dependency explicit.
- Modify `mac-codex-agent/README.md`: document menu controls, installation, manual launch, and removal.

### Task 1: State Model, Preferences, And Menu Presentation

**Files:**
- Create: `mac-codex-agent/app_state.py`
- Create: `mac-codex-agent/tests/test_app_state.py`

- [ ] **Step 1: Write failing state and preference tests**

Add tests that define the public model:

```python
from app_state import AgentSnapshot, ConnectionState, MemoryPreferences, present


def test_connected_presentation_is_checked_and_reconnectable():
    view = present(AgentSnapshot(ConnectionState.CONNECTED, True))
    assert view.bluetooth_text == "Bluetooth: Connected"
    assert view.connection_checked is True
    assert view.reconnect_enabled is True
    assert view.symbol_name == "bolt.horizontal.circle.fill"


def test_connection_preference_round_trips():
    preferences = MemoryPreferences()
    assert preferences.connection_enabled() is True
    preferences.set_connection_enabled(False)
    assert preferences.connection_enabled() is False
```

Use `unittest.TestCase` in the actual file to match the repository's existing test suite. Cover every design state and verify `disabled` is unchecked while permission, unavailable, and error states remain checked when the user's preference is on.

- [ ] **Step 2: Run the focused tests and verify failure**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_app_state.py -v
```

Expected: `ModuleNotFoundError: No module named 'app_state'`.

- [ ] **Step 3: Implement the state model and pure presentation mapping**

Create these stable interfaces:

```python
class ConnectionState(enum.Enum):
    DISABLED = "disabled"
    SCANNING = "scanning"
    WAITING = "waiting"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    DISCONNECTING = "disconnecting"
    UNAUTHORIZED = "unauthorized"
    UNAVAILABLE = "unavailable"
    ERROR = "error"


@dataclasses.dataclass(frozen=True)
class AgentSnapshot:
    state: ConnectionState
    connection_enabled: bool
    detail: str = ""
    changed_at: float = dataclasses.field(default_factory=time.time)


@dataclasses.dataclass(frozen=True)
class MenuPresentation:
    bluetooth_text: str
    connection_checked: bool
    reconnect_enabled: bool
    symbol_name: str
```

Implement `present(snapshot)` as a total mapping over `ConnectionState`. Add `Preferences` with `connection_enabled()` and `set_connection_enabled(value)`, a production `UserDefaultsPreferences` backed by `Foundation.NSUserDefaults.standardUserDefaults()`, and `MemoryPreferences` for tests. Use key `connectionEnabled`; register `True` as the default so first launch scans immediately.

- [ ] **Step 4: Run the focused tests**

Run the Step 2 command. Expected: all state tests pass.

- [ ] **Step 5: Commit the state model**

```bash
git add mac-codex-agent/app_state.py mac-codex-agent/tests/test_app_state.py
git commit -m "Add Codex agent menu state model"
```

### Task 2: Interruptible BLE Runtime And Controller

**Files:**
- Create: `mac-codex-agent/runtime.py`
- Create: `mac-codex-agent/tests/test_runtime.py`
- Modify: `mac-codex-agent/agent.py`

- [ ] **Step 1: Write failing controller and runtime tests**

Use injected `transport_factory`, `collector`, and zero-duration retry sleep. Define fakes with `connect`, `disconnect`, `connected`, and `send_json`. Tests must prove:

```python
async def disable_while_connected(runtime, transport):
    task = asyncio.create_task(runtime.run())
    await transport.connected_event.wait()
    runtime.submit(AgentCommand.DISABLE)
    await transport.disconnected_event.wait()
    assert runtime.snapshot().state is ConnectionState.DISABLED
    runtime.submit(AgentCommand.STOP)
    await task


async def reconnect_starts_a_new_transport(runtime, factory):
    task = asyncio.create_task(runtime.run())
    await factory.wait_for_connections(1)
    runtime.submit(AgentCommand.RECONNECT)
    await factory.wait_for_connections(2)
    assert factory.instances[0].disconnect_count == 1
    runtime.submit(AgentCommand.STOP)
    await task
```

Also cover initial disabled state, not-found transition to `waiting`, unexpected exception to `error`, and stop during scanning. Wrap each coroutine with `asyncio.run()` in `unittest` methods.

- [ ] **Step 2: Run runtime tests and verify failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_runtime.py -v
```

Expected: import failure for `runtime`.

- [ ] **Step 3: Separate discovery state from connection in the BLE transport**

Change `BleStatusTransport.connect` to accept an optional callback and report the point at which a matching device has been discovered:

```python
async def connect(self, on_device_found=None) -> bool:
    device = await BleakScanner.find_device_by_filter(matches, timeout=8.0)
    if device is None:
        return False
    if on_device_found:
        on_device_found()
    client = BleakClient(device, disconnected_callback=on_disconnect)
    await client.connect()
    self._client = client
    return client.is_connected
```

Keep `CodexStatusAgent` and framing behavior unchanged. Remove `run_agent()` and `main()` from `agent.py`; `runtime.py` becomes the only lifecycle owner.

- [ ] **Step 4: Implement the command runtime**

Define:

```python
class AgentCommand(enum.Enum):
    ENABLE = "enable"
    DISABLE = "disable"
    RECONNECT = "reconnect"
    STOP = "stop"


class AgentRuntime:
    def __init__(self, collector, transport_factory, connection_enabled=True):
        self._commands = queue.Queue()
        self._snapshot = AgentSnapshot(
            ConnectionState.SCANNING if connection_enabled else ConnectionState.DISABLED,
            connection_enabled,
        )

    def submit(self, command):
        self._commands.put(command)

    def snapshot(self):
        with self._state_lock:
            return self._snapshot
```

`run()` must race each active BLE session against `asyncio.to_thread(self._commands.get)` using `asyncio.wait(..., FIRST_COMPLETED)`. A disable, reconnect, or stop command cancels and awaits the session task, then calls `transport.disconnect()` in `finally`. Retry waits must also race against commands so no command waits for backoff expiry. Map Bleak permission and powered-off errors by inspecting `BleakError` text in one helper; retain the diagnostic in `AgentSnapshot.detail` and log the full exception.

- [ ] **Step 5: Implement the thread-owning controller**

Define `AgentController(preferences, runtime_factory)` with:

```python
def start(self):
    self._thread = threading.Thread(target=self._thread_main, name="codex-agent", daemon=False)
    self._thread.start()

def set_connection_enabled(self, enabled):
    self._preferences.set_connection_enabled(enabled)
    self._runtime.submit(AgentCommand.ENABLE if enabled else AgentCommand.DISABLE)

def reconnect(self):
    self._runtime.submit(AgentCommand.RECONNECT)

def stop(self, timeout=5.0):
    self._runtime.submit(AgentCommand.STOP)
    self._thread.join(timeout)
    return not self._thread.is_alive()
```

The worker thread creates its own asyncio event loop with `asyncio.run(runtime.run())`. Start `activity_loop` as its existing daemon thread only once per process.

- [ ] **Step 6: Run runtime and legacy tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -v
```

Expected: all tests pass, including the original status/chime test.

- [ ] **Step 7: Commit the runtime**

```bash
git add mac-codex-agent/agent.py mac-codex-agent/runtime.py mac-codex-agent/tests/test_runtime.py
git commit -m "Add controllable Codex BLE runtime"
```

### Task 3: AppKit Menu Bar Application

**Files:**
- Create: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`

- [ ] **Step 1: Extend presentation tests for every menu property**

Verify the static agent and device labels, checked state, reconnect enabled state, and SF Symbol selection:

```python
view = present(AgentSnapshot(ConnectionState.DISABLED, False))
self.assertEqual(view.agent_text, "Agent: Running")
self.assertEqual(view.device_text, "Device: StopWatch-Codex")
self.assertFalse(view.connection_checked)
self.assertFalse(view.reconnect_enabled)
self.assertEqual(view.symbol_name, "bolt.horizontal.circle")
```

Run the focused state tests and expect failure because the two new label fields do not exist.

- [ ] **Step 2: Add the missing presentation fields and make tests pass**

Add `agent_text` and `device_text` to `MenuPresentation`, with constants shown in Step 1. Run the focused state tests and expect all pass.

- [ ] **Step 3: Implement the AppKit delegate**

Create `MenuBarApplication(NSObject)` that retains `NSStatusItem`, `NSMenu`, menu items, timer, and controller. On launch:

```python
self.status_item = NSStatusBar.systemStatusBar().statusItemWithLength_(NSVariableStatusItemLength)
self.menu = NSMenu.alloc().init()
self.agent_item = self._informational_item("Agent: Running")
self.bluetooth_item = self._informational_item("Bluetooth: Scanning")
self.device_item = self._informational_item("Device: StopWatch-Codex")
self.connection_item = self._action_item("Connect to StopWatch", "toggleConnection:")
self.reconnect_item = self._action_item("Reconnect Now", "reconnect:")
self.open_log_item = self._action_item("Open Log", "openLog:")
self.quit_item = self._action_item("Quit StopWatch Codex Agent", "quit:")
self.status_item.setMenu_(self.menu)
self.controller.start()
```

Refresh from `controller.snapshot()` every 0.5 seconds on the main thread. Set menu item state with `NSControlStateValueOn`/`NSControlStateValueOff`, enable Reconnect from `MenuPresentation`, and use `NSImage.imageWithSystemSymbolName_accessibilityDescription_` for the icon.

Actions call controller methods only. `openLog_` uses `NSWorkspace.sharedWorkspace().openURL_(NSURL.fileURLWithPath_(...))`. `quit_` disables menu actions, calls `controller.stop(timeout=5.0)`, logs a timeout if necessary, and calls `NSApp.terminate_(None)`. `applicationWillTerminate_` calls stop idempotently.

- [ ] **Step 4: Add file logging and the entry point**

Configure one rotating file handler before controller startup:

```python
LOG_PATH = pathlib.Path.home() / "Library" / "Logs" / "stopwatch-codex-agent.log"


def configure_logging():
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    handler = logging.handlers.RotatingFileHandler(
        LOG_PATH, maxBytes=1_000_000, backupCount=2, encoding="utf-8"
    )
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        handlers=[handler],
        force=True,
    )
```

`main()` sets `NSApplicationActivationPolicyAccessory`, installs the delegate, and calls `app.run()`. Keep a module-level delegate reference because AppKit delegates are not retained reliably by Python local scope alone.

- [ ] **Step 5: Run the complete Python suite**

Run unittest discovery. Expected: all tests pass with no UI process opened by imports.

- [ ] **Step 6: Commit the menu application**

```bash
git add mac-codex-agent/app_state.py mac-codex-agent/menu_app.py mac-codex-agent/tests/test_app_state.py
git commit -m "Add Codex agent menu bar UI"
```

### Task 4: Manual-Launch Bundle And Legacy Migration

**Files:**
- Create: `mac-codex-agent/sitecustomize.py`
- Create: `mac-codex-agent/tests/test_install_script.py`
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/requirements.txt`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Write a failing installer contract test**

Read the installer as text and assert:

```python
self.assertIn('launchctl bootout "gui/$UID/$LABEL"', script)
self.assertIn('rm -f "$PLIST"', script)
self.assertNotIn("<key>KeepAlive</key>", script)
self.assertNotIn("<key>RunAtLoad</key>", script)
self.assertNotIn("launchctl bootstrap", script)
self.assertNotIn("launchctl kickstart", script)
self.assertIn('cp "$ROOT_DIR/menu_app.py"', script)
```

Run the test and expect failures against the current LaunchAgent installer.

- [ ] **Step 2: Add the bundle bootstrap**

Create `sitecustomize.py`:

```python
import os
import sys


if os.environ.get("STOPWATCH_CODEX_APP") == "1" and len(sys.argv) == 1:
    from menu_app import main

    main()
    raise SystemExit(0)
```

This module has no effect during tests or normal Python commands because the environment marker is set only in the app bundle.

- [ ] **Step 3: Rewrite installation as package-and-migrate**

Retain venv creation, dependency installation, copied Python executable/runtime, bundle identifier, `LSUIElement`, `PYTHONHOME`, Bluetooth usage description, and ad-hoc signing. Add `STOPWATCH_CODEX_APP=1` and set `PYTHONPATH` to `Contents/Resources/app:$SITE_PACKAGES`. Copy these modules into `Contents/Resources/app`: `sitecustomize.py`, `menu_app.py`, `runtime.py`, `app_state.py`, `agent.py`, `codex_adapter.py`, `completion.py`, and `protocol.py`.

Before replacing the app, migrate the old service exactly once:

```bash
launchctl bootout "gui/$UID/$LABEL" >/dev/null 2>&1 || true
rm -f "$PLIST"
```

Do not write a plist, call `open`, bootstrap a service, or kickstart a process. Finish with:

```bash
echo "Installed $APP_DIR"
echo "Open StopWatch Codex Agent from your Applications folder to start it."
```

- [ ] **Step 4: Make AppKit an explicit dependency**

Add:

```text
pyobjc-framework-Cocoa>=11.1,<12
```

Keep the existing Bleak and certifi bounds.

- [ ] **Step 5: Update user documentation**

Document `./mac-codex-agent/install_launch_agent.sh` as an installer retained under its old filename for compatibility. Explain that installation does not start the app, it never starts at login, Quit is final until manual reopen, the connection toggle persists, and the legacy LaunchAgent is removed. Replace shell-based stop instructions with the menu Quit command; retain explicit removal commands.

- [ ] **Step 6: Run installer and complete test suites**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -v
./mac-codex-agent/install_launch_agent.sh
```

Expected: all tests pass; the installer reports the app path; `launchctl print gui/$UID/com.stopwatch.codex-agent` fails with service not found; no agent process is running yet.

- [ ] **Step 7: Commit packaging and documentation**

```bash
git add mac-codex-agent/sitecustomize.py mac-codex-agent/tests/test_install_script.py \
  mac-codex-agent/install_launch_agent.sh mac-codex-agent/requirements.txt \
  mac-codex-agent/README.md
git commit -m "Package Codex agent as manual menu app"
```

### Task 5: Live macOS And Watch Verification

**Files:**
- Modify only if verification exposes a defect in the files from Tasks 1-4.

- [ ] **Step 1: Verify no automatic process exists**

```bash
launchctl print "gui/$UID/com.stopwatch.codex-agent"
pgrep -fl "StopWatchCodexAgent|menu_app.py"
```

Expected: service not found and no matching process.

- [ ] **Step 2: Manually launch the app bundle**

```bash
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Expected: one `StopWatchCodexAgent` process, one menu bar icon, and no Dock icon. If macOS asks for Bluetooth access, allow it.

- [ ] **Step 3: Verify live BLE state**

Open Codex on the watch. Inspect the menu and log. Expected progression: Scanning, Connecting, Connected. Confirm the watch changes to `BLE LIVE` and receives current Codex fields.

- [ ] **Step 4: Verify connection controls**

Turn `Connect to StopWatch` off. Expected: the watch disconnects, the menu reads Connection Off, and the log stops producing scans. Quit and manually reopen the app; expected: Connection Off remains selected. Turn it on; expected: immediate scan and connection. Choose Reconnect Now; expected: exactly one disconnect/reconnect cycle.

- [ ] **Step 5: Verify final quit semantics**

Choose Quit and wait 30 seconds. Run:

```bash
pgrep -fl "StopWatchCodexAgent|menu_app.py"
launchctl print "gui/$UID/com.stopwatch.codex-agent"
```

Expected: no process and no service. Manually reopen once more to prove the app remains launchable, then Quit again.

- [ ] **Step 6: Run final regression checks**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -v
git diff --check
```

Expected: all tests pass and no whitespace errors. Confirm unrelated firmware build outputs and user-owned icon changes remain unstaged.

- [ ] **Step 7: Commit only verification fixes, if any**

If verification required code changes, stage only the files involved and commit:

```bash
git commit -m "Fix Codex menu agent verification issues"
```

If no fixes were needed, do not create an empty commit.
