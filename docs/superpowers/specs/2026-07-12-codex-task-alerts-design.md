# Codex Task Alerts Design

## Goal

Replace the unused today/week activity counters with a persistent task-attention view. The monitor must tell the user which Codex conversation has new progress, whether that progress has been handled, and when the transition happened.

A task alert is tracked per Codex conversation and grouped by project. Replying in one conversation handles only that conversation's pending alerts. Other conversations in the same project remain pending.

## User Model

Each alert contains:

- conversation/thread ID;
- project name;
- conversation title;
- progress time;
- handled time, when present;
- state: `pending` or `handled`;
- watch notification state.

Project name is the final path component of the thread working directory. For example, `/Users/bytedance/Projects.localized/Codex/StopWatch` becomes `StopWatch`. Conversation title comes from the Codex `threads.title` column.

## Event Semantics

The tracker reads structured rollout events rather than parsing assistant text:

- `event_msg/task_complete` creates a new pending alert. Codex emits this boundary when an agent turn stops, covering both completed work and a turn that has stopped for user interaction.
- `event_msg/user_message` marks all earlier pending alerts in the same conversation as handled at the user-message timestamp.
- A later `task_complete` in that conversation creates a new pending alert.
- Duplicate events are ignored using an alert ID derived from thread ID and event timestamp.

Opening a conversation without replying does not handle it because there is no reliable local structured event for "viewed". The initial implementation does not add a manual acknowledgement button.

## Architecture

```text
Codex state_5.sqlite        rollout JSONL files
        |                           |
        +------ TaskAlertTracker ---+
                       |
             persistent daily ledger
                       |
          CodexStateCollector snapshot
                /              \
       Mac menu alert list      BLE status payload
                                      |
                               StopWatch carousel
```

`TaskAlertTracker` is a focused Mac-side component. It owns thread discovery, incremental rollout reads, event reduction, persistence, and watch-notification acknowledgement. The existing collector calls it once per two-second status sample and adds its snapshot to the common status dictionary.

The AppKit menu and watch consume the same alert snapshot. Neither UI reads Codex databases or rollout files directly.

## Persistence

The tracker stores an atomic JSON ledger at:

```text
~/.codex/.stopwatch_alerts.json
```

The file contains:

```json
{
  "version": 1,
  "initialized_at": "2026-07-12T00:00:00Z",
  "last_scan_at": "2026-07-12T00:15:00Z",
  "cursors": {
    "thread-id": {
      "path": "/path/to/rollout.jsonl",
      "offset": 123456
    }
  },
  "alerts": [
    {
      "id": "thread-id:2026-07-12T00:14:55Z",
      "thread_id": "thread-id",
      "project": "StopWatch",
      "title": "Codex status monitor",
      "progress_at": "2026-07-12T00:14:55Z",
      "handled_at": null,
      "state": "pending",
      "watch_notified": false
    }
  ]
}
```

Writes use a temporary file plus atomic replacement. Malformed state is moved aside and rebuilt without terminating the agent. Records older than seven local calendar days are pruned; the menu and watch display only the current local day.

## Initialization And Restart

First-ever startup must not replay historical completions:

1. Query existing non-subagent OpenAI threads.
2. Record each rollout's current end offset.
3. Set `initialized_at` and `last_scan_at` to now.
4. Begin processing only later appended events.

On subsequent startup, known threads resume from saved offsets, including events appended while the menu application was closed. A newly discovered thread is scanned for events newer than the saved global `last_scan_at`, allowing conversations created during downtime to be recovered without replaying older history.

If a rollout shrinks or its path changes, scanning restarts at offset zero but ignores events at or before `last_scan_at` for that thread.

## Refresh And Ordering

The tracker refreshes every two seconds as part of the existing collector loop. It queries at most the 50 most recently updated visible, non-subagent OpenAI threads plus any threads already represented in the current-day ledger.

Current-day records are ordered:

1. pending before handled;
2. newest progress first within each state.

The Mac menu receives the full current-day list. The watch payload contains complete pending/handled counts plus the first eight ordered records. This bounds the compact JSON below the firmware's 2048-byte assembled-message limit.

Project names are limited to 32 Unicode characters and conversation titles to 48 Unicode characters before transmission. The full strings remain in the local ledger.

## Notification Delivery

Each alert starts with `watch_notified=false`.

While the watch is connected, the BLE sender transmits status first, then emits one chime event for a new pending alert. The tracker marks that alert notified only after the chime write succeeds. A handled alert is never chimed later.

When several unnotified alerts accumulated while disconnected, reconnect sends one summary chime and marks the current pending backlog notified. The visual pending count and carousel preserve the individual records without producing a burst of sounds.

A new completion after that point creates another unnotified alert and produces one new chime. The same alert never chimes twice across reconnects or Mac-agent restarts.

## Mac Menu

The Codex section removes `Today` and `This Week`:

```text
Codex Status
    Model: gpt-5.6-sol
    Effort: Xhigh
    5H Remaining: 85%
    7D Remaining: 98%
```

A `Task Alerts` item opens a submenu containing the complete current-day ledger:

```text
Task Alerts · 2 Pending / 4 Handled  >
    Pending
        StopWatch · Codex status monitor · 00:15
        webshot · UI review · 00:11
    Handled
        StopWatch · Bluetooth setup · 00:08
```

Alert rows are custom read-only menu views, so they remain legible without selection highlighting. Pending rows use the system orange label color; handled rows use the normal secondary label color. The submenu is rebuilt only when the alert revision changes.

## BLE Payload

The common status JSON removes `codex_today_min` and `codex_week_min` and adds:

```json
{
  "codex_alert_pending": 2,
  "codex_alert_handled": 4,
  "codex_alert_revision": 17,
  "codex_alerts": [
    {
      "project": "StopWatch",
      "title": "Codex status monitor",
      "time": "00:15",
      "state": "pending"
    }
  ]
}
```

Internal thread IDs, rollout paths, and handled timestamps are not sent to the watch. The chime event keeps `type=chime` and `agent=codex`; it may add an alert revision for diagnostics without exposing local paths.

## StopWatch Activity Area

The existing Activity heading and two value labels become an alert carousel:

```text
PENDING 1/2
StopWatch
00:15 · Codex status monitor
```

Rules:

- rotate every three seconds;
- pending records always precede handled records;
- show the record's project on the first value line;
- show time and truncated conversation title on the second line;
- when pending count is zero, heading reads `ALL HANDLED · N` and starts with the most recently handled record;
- when there are no current-day records, heading reads `NO ALERTS`, project is `--`, and detail is empty;
- receiving a new revision resets the carousel to the first pending record.

The project and detail labels have fixed widths and trailing-ellipsis long mode so long names cannot resize or overlap the 466-pixel display. Existing battery, BLE connection, model, effort, and quota UI remain unchanged.

## Removal Of Activity Counters

The Mac collector stops recording frontmost-Codex day/week time and no longer starts the activity polling thread. The status model, Mac menu, BLE JSON, firmware parser, and watch UI remove today/week minute fields.

The old `~/.codex/.stopwatch_usage.json` file is left untouched for backward compatibility but is no longer read or written. No migration or deletion is required.

## Error Handling

- Missing or locked state database: preserve the last alert snapshot and retry.
- Missing rollout: preserve its ledger entries and retry thread discovery.
- Partial trailing JSONL line: do not advance beyond that line; retry on the next scan.
- Invalid JSON line: skip it and continue to the next complete line.
- Corrupt persistence file: move it to a timestamped `.corrupt` path and initialize a baseline without replaying history.
- Oversized or malformed alert payload on the watch: reject only the alerts array and retain the prior valid carousel.
- Date rollover: current-day counts reset naturally while retained records remain available in the seven-day ledger.

## Testing

Mac automated tests cover:

- first-run baseline without historical replay;
- incremental completion creation and deduplication;
- user reply handling only the matching conversation;
- multiple conversations in one project remaining independent;
- restart cursor recovery and new-thread downtime recovery;
- atomic persistence, corruption recovery, and seven-day pruning;
- pending/handled ordering and local-day filtering;
- one live chime per new alert and one summary chime for reconnect backlog;
- no chime for an alert handled before connection;
- removal of today/week fields and activity polling;
- dynamic menu summary and full read-only submenu.

Firmware tests cover:

- parsing counts and up to eight alerts;
- rejecting malformed entries without corrupting prior state;
- pending-first carousel order and three-second rotation;
- long project/title truncation without overlap;
- `NO ALERTS` and `ALL HANDLED` empty/pending-free states.

Live verification covers:

1. Build and flash launcher firmware without changing assistant OTA or NVS.
2. Start the Mac menu application and confirm no historical chime.
3. Complete a Codex turn and confirm one sound plus pending project/title/time.
4. Reply in that conversation and confirm it becomes handled within two seconds.
5. Complete two conversations under one project and confirm independent records.
6. Restart the Mac agent and confirm state and notification flags persist.
7. Leave the watch disconnected for multiple completions, reconnect, and confirm one summary chime with the correct pending count.
8. Verify two-button long press still returns to Launcher and Assistant startup remains unaffected.

## Acceptance Criteria

The feature is complete when:

1. Today/week activity counters are absent from Mac and watch displays.
2. Every post-baseline `task_complete` creates a persistent conversation-level pending alert.
3. A later user message handles only that conversation's earlier pending alerts.
4. Mac and watch update within one two-second collection interval.
5. New progress sounds once, persists visually, and does not sound again after reply.
6. Mac menu shows the full current-day handled/pending ledger.
7. Watch rotates the first eight ordered records and shows complete counts.
8. Restart, reconnect, firmware navigation, and assistant OTA behavior remain stable.
