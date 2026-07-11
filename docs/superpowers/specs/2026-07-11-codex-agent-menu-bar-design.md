# Codex Agent Menu Bar Design

## Goal

Turn the existing invisible StopWatch Codex Mac agent into a menu bar application that exposes its runtime and Bluetooth state. The user can stop and resume watch connections without terminating the application, and can explicitly quit the background application when no synchronization is wanted.

The application must never start automatically. After the user chooses Quit, it remains stopped until the user manually opens `StopWatch Codex Agent.app` again.

## Scope

This change includes:

- a menu bar icon with a compact status menu;
- visible agent, Bluetooth, and device state;
- a persistent `Connect to StopWatch` toggle;
- an immediate reconnect command;
- an Open Log command;
- explicit application termination;
- removal of LaunchAgent registration and `KeepAlive` behavior;
- packaging that can be opened manually from the user's Applications directory.

This change does not alter the StopWatch firmware, BLE service UUIDs, Codex status payload, framing protocol, or watch user interface.

## Architecture

The agent remains one process. AppKit owns the main thread and menu bar UI. The existing asyncio-based BLE and Codex status loop runs on one worker thread. A thread-safe controller is the only communication boundary between them.

```text
macOS menu bar / AppKit main thread
              |
              v
      AgentController commands
              |
              v
 asyncio BLE worker thread ----> StopWatch-Codex
              |
              v
      AgentState snapshots
              |
              v
  periodic menu refresh on main thread
```

The UI never calls Bleak directly. The worker never mutates AppKit objects. Commands cross into the worker through thread-safe asyncio scheduling, while the UI reads immutable state snapshots under a lock.

## Components

### Menu Bar Application

The application uses PyObjC/AppKit directly, avoiding a second native helper process. It is an accessory application (`LSUIElement=true`), so it has a menu bar icon but no Dock icon or normal application window.

The menu contains:

- `Agent: Running`, informational and disabled;
- `Bluetooth: <state>`, informational and disabled;
- `Device: StopWatch-Codex`, informational and disabled;
- `Connect to StopWatch`, a checked menu item that controls BLE scanning and connection;
- `Reconnect Now`, enabled only when connection is on;
- `Open Log`;
- `Quit StopWatch Codex Agent`.

The menu bar icon changes between a neutral disconnected symbol and an active connected symbol. Text in the menu remains the authoritative status so state is understandable without relying on icon color.

### Agent Controller

`AgentController` owns worker startup, shutdown, commands, and the latest `AgentState`. It exposes these operations:

- `start()` starts the asyncio worker thread;
- `set_connection_enabled(bool)` persists the preference and starts or stops BLE activity;
- `reconnect()` cancels the current scan/session and immediately begins a new scan;
- `snapshot()` returns the latest immutable status;
- `stop()` disconnects cleanly and joins the worker before application termination.

### BLE Worker

The worker reuses `BleStatusTransport`, `CodexStatusAgent`, and the existing collector. Its retry loop becomes interruptible so disabling connections or requesting a reconnect does not wait for an eight-second scan timeout or a thirty-second backoff.

Only one BLE scan or connection attempt may exist at a time. Disabling connection cancels the active attempt, disconnects the current client, and enters `disabled`. Re-enabling it begins scanning immediately.

## State Model

The menu represents Bluetooth with one of these states:

| State | Menu text | Meaning |
| --- | --- | --- |
| `disabled` | Connection Off | The persisted connection toggle is off |
| `scanning` | Scanning | Looking for the watch |
| `waiting` | Waiting for Watch | The watch was not found; retry is scheduled |
| `connecting` | Connecting | A matching BLE peripheral was found |
| `connected` | Connected | Status synchronization is active |
| `disconnecting` | Disconnecting | A user command is closing the BLE session |
| `unauthorized` | Bluetooth Permission Required | macOS denied Bluetooth access |
| `unavailable` | Bluetooth Unavailable | Bluetooth is powered off or unavailable |
| `error` | Connection Error | An unexpected BLE session error occurred |

The state snapshot also stores a short diagnostic message and the last transition time. Diagnostics go to the log; the menu shows only concise user-facing state.

## Preferences And Lifecycle

The connection toggle is stored in `NSUserDefaults` under the application bundle identifier. Its initial value is on. Every later launch restores the last explicit selection:

- if the user disabled connection, manually reopening the app leaves it disabled;
- if the user left connection enabled, manually reopening begins scanning immediately.

The installer removes any existing `com.stopwatch.codex-agent` LaunchAgent before installing the new bundle and does not create another one. The app bundle includes the Python entry point and support modules so double-clicking the app is sufficient to start it.

Choosing Quit performs an orderly BLE disconnect, stops the collector worker, closes the log, and terminates the process. There is no watchdog, login item, `RunAtLoad`, or `KeepAlive` mechanism that can restart it. The user starts it again only by opening the application manually.

## Logging

The existing log location remains:

```text
~/Library/Logs/stopwatch-codex-agent.log
```

Each connection-state transition is written as one structured, human-readable line. `Open Log` opens the file in the default macOS log/text viewer. The installer creates the file if it does not exist, and application startup appends rather than truncates it.

## Error Handling

- Bluetooth permission denial produces `unauthorized` and does not terminate the menu application.
- Powered-off Bluetooth produces `unavailable` and periodically checks for recovery while connection remains enabled.
- Failure to find the watch produces `waiting` and uses the existing bounded exponential backoff.
- A dropped active connection returns to scanning automatically when connection remains enabled.
- Disabling connection during scan, backoff, connection, or status transmission converges to `disabled` without reporting a spurious error.
- Reconnect resets backoff and starts a fresh scan after the current BLE operation is cancelled.
- Quit has a bounded shutdown timeout so a CoreBluetooth operation cannot leave the process hanging.

## Packaging And Migration

`install_launch_agent.sh` is retained as the compatibility entry point but changes behavior: it builds/updates the application bundle, removes the legacy LaunchAgent if present, and does not launch or register a background service. Documentation will direct the user to open `~/Applications/StopWatch Codex Agent.app` manually.

The existing locally signed bundle identifier and Bluetooth usage description remain unchanged so macOS Bluetooth privacy permission continues to belong to the same application identity.

## Testing

Automated tests cover:

- state transitions for startup, connect, disconnect, retry, reconnect, and disable;
- persistence and restoration of the connection toggle;
- disabling during scan and during an active connection;
- reconnect resetting retry backoff;
- quit requesting disconnect and worker shutdown;
- menu labels and enabled/checked states derived from each snapshot;
- installer output containing no LaunchAgent, `RunAtLoad`, or `KeepAlive` registration.

Manual verification covers:

1. Install and manually open the app; verify one menu bar icon and no Dock icon.
2. Open the watch Codex application and verify the menu changes to Connected.
3. Turn connection off and verify the watch disconnects and scanning stops.
4. Quit and manually reopen the Mac app; verify the off preference remains off.
5. Turn connection on and verify immediate reconnection.
6. Choose Reconnect Now and verify one clean disconnect/reconnect cycle.
7. Quit the app and verify the process and menu icon disappear and do not return after waiting.
8. Log out or restart macOS and verify the app does not start automatically.
9. Manually open the app and verify synchronization resumes according to the saved connection preference.

## Acceptance Criteria

The feature is complete when:

1. The agent is visible and controllable from one macOS menu bar icon.
2. Bluetooth scanning, waiting, connecting, connected, disabled, permission, unavailable, and error states are distinguishable.
3. The connection toggle immediately controls BLE activity and survives process and computer restarts.
4. Reconnect begins a fresh connection attempt without starting a second worker.
5. Quit completely stops the agent and no automatic mechanism restarts it.
6. The application starts only when manually opened.
7. Existing watch status synchronization and completion events continue to work unchanged.
