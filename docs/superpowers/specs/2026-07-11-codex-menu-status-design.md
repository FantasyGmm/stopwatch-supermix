# Codex Menu Status Design

## Goal

Show the same Codex model, reasoning, quota, and activity data available on the StopWatch in the Mac menu bar application. Rename the persistent BLE toggle from `Connect to StopWatch` to `Enable Watch Connection` so it is clearly distinguishable from the one-shot `Reconnect Now` command.

Watch battery telemetry is outside this change because it is produced locally by the watch and the current BLE data flow is Mac to watch.

## Menu Structure

The menu is organized into two information sections followed by commands:

```text
Codex Status
Model: gpt-5.3
Effort: High
5H Remaining: 72%
7D Remaining: 64%
Today: 38 min
This Week: 214 min
--------------------------------
Connection Status
    Agent: Running
    Bluetooth: Connected
    Device: StopWatch-Codex
--------------------------------
Enable Watch Connection
Reconnect Now
--------------------------------
Open Log
--------------------------------
Quit StopWatch Codex Agent
```

`Codex Status` and all status rows are disabled informational menu items. The connection item remains a checked menu item. Missing strings and numeric values display `--`; valid zero values display `0`, `0%`, or `0 min` rather than `--`.

## Shared Status Snapshot

The Mac agent remains the source of truth. `AgentRuntime` owns one independent Codex collection task that runs every two seconds whenever the menu application is running, regardless of whether watch connection is enabled or a watch is present.

The collector publishes an immutable `CodexSnapshot` containing:

- model;
- reasoning effort;
- five-hour quota remaining percentage;
- seven-day quota remaining percentage;
- today's active minutes;
- this week's active minutes;
- collection timestamp.

`AgentSnapshot` combines this Codex snapshot with the existing Bluetooth state. Every update preserves the other half of the combined snapshot: a Bluetooth transition does not clear Codex data, and a Codex refresh does not change Bluetooth state.

The AppKit main thread continues polling `AgentController.snapshot()` every 0.5 seconds. It does no Codex I/O and never calls the collector directly.

## BLE Data Flow

The independent collector supplies the same status dictionary to the BLE session. `CodexStatusAgent` sends a provided status payload rather than collecting a second copy. Completion detection remains in the BLE sender and continues to emit one chime event for each newly observed completion while the watch is connected.

This produces one collector read every two seconds, not one read for the menu plus another for BLE. Menu and watch therefore display the same sampled values.

If the watch is disconnected, the menu continues updating. When a connection is established, the most recent completed snapshot is sent immediately. If collection has not completed yet, the first BLE send waits for the initial snapshot instead of sending an empty object.

## Presentation Rules

- Informational rows use custom read-only menu views rather than enabled menu
  commands. They stay bright but cannot receive selection highlighting.
- `Codex Status` and `Connection Status` use the system semibold menu font.
- Values belonging to each heading are indented 16 points relative to their
  heading, making ownership visible without hiding values in a submenu.
- Model is shown as collected, with `--` for an empty value.
- Effort uses title case, such as `High` or `Medium`; empty values use `--`.
- Quotas are clamped to 0 through 100 and shown with `%`; unavailable values use `--`.
- Activity is clamped at zero and shown with `min`; unavailable values use `--`.
- Menu refresh remains 0.5 seconds; collection and BLE status transmission remain two seconds.
- Existing Bluetooth labels, menu bar icon, `Codex` title, logging, and manual-only application lifecycle remain unchanged.

## Error Handling

- Collector exceptions are logged and do not stop the menu or BLE retry loop.
- A failed refresh preserves the last successful Codex snapshot.
- Before the first successful refresh, every Codex field displays `--`.
- Partial collector responses update available fields and render missing fields as `--`.
- Disabling watch connection stops BLE only; Codex collection and menu status continue.
- Quitting the application stops both the collector task and BLE activity within the existing bounded shutdown.

## Testing

Automated tests cover:

- dictionary-to-`CodexSnapshot` conversion, including zero and missing values;
- menu formatting for all six fields;
- Bluetooth transitions preserving Codex values;
- Codex refreshes preserving Bluetooth state;
- collection while watch connection is disabled;
- one collector loop feeding both menu snapshot and BLE sender;
- failed collection retaining the last successful snapshot;
- `Enable Watch Connection` replacing the ambiguous old label;
- read-only rows remaining bright without becoming selectable;
- section headings using semibold text and child rows using a 16-point indent;
- existing toggle, reconnect, shutdown, framing, and collector tests.

Manual verification covers:

1. Install and manually open the menu application.
2. Confirm all six Codex rows show current values before the watch connects.
3. Open Codex on the watch and confirm its six values match the menu snapshot.
4. Disable `Enable Watch Connection` and confirm menu Codex values keep updating while BLE stops.
5. Re-enable connection and confirm the newest snapshot reaches the watch.
6. Quit and confirm the application remains stopped.

## Acceptance Criteria

The change is complete when:

1. The menu's first section shows model, effort, five-hour remaining, seven-day remaining, today minutes, and week minutes.
2. Menu values remain available without a connected watch.
3. Menu and watch use one shared collected snapshot.
4. Missing and zero values are rendered unambiguously.
5. The connection toggle is labeled `Enable Watch Connection` and retains its current persistence behavior.
6. Existing BLE reconnect, manual startup, and final Quit behavior remain unchanged.
