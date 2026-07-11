# Codex Task Alerts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace unused activity minutes with persistent per-conversation Codex progress alerts that clear on reply and synchronize to the Mac menu and StopWatch.

**Architecture:** A new `TaskAlertTracker` incrementally reduces structured rollout events into an atomic local ledger. The existing two-second collector publishes the alert snapshot to both AppKit and BLE; the watch parses a bounded eight-record payload and rotates records in the former Activity area.

**Tech Stack:** Python 3.9, SQLite, JSONL, PyObjC/AppKit, asyncio/Bleak, ESP-IDF 5.5.4, NimBLE, cJSON, LVGL, unittest.

---

### Task 1: Persistent Task Alert Tracker

**Files:**
- Create: `mac-codex-agent/task_alerts.py`
- Create: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Write failing tracker tests**

Use temporary SQLite, rollout, and ledger files. Cover first-run baseline, one appended completion, reply handling, two threads in one project, duplicate refresh, restart from cursor, a new thread created during downtime, corrupt ledger recovery, and seven-day pruning.

The core assertion shape is:

```python
tracker = TaskAlertTracker(state_db, ledger, now=fixed_now)
self.assertEqual(tracker.refresh().alerts, ())
append_event(rollout, "task_complete", "2026-07-12T00:15:00Z")
snapshot = tracker.refresh()
self.assertEqual(snapshot.pending_count, 1)
self.assertEqual(snapshot.alerts[0].project, "StopWatch")
append_event(rollout, "user_message", "2026-07-12T00:16:00Z")
self.assertEqual(tracker.refresh().alerts[0].state, "handled")
```

- [ ] **Step 2: Run the focused test and verify import failure**

Run `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_task_alerts.py -v` and expect `ModuleNotFoundError`.

- [ ] **Step 3: Implement tracker value types and persistence**

Define immutable `TaskAlert` and `TaskAlertSnapshot` types. `TaskAlertTracker`
exposes `refresh() -> TaskAlertSnapshot`,
`pending_watch_notification_ids()` returning an immutable tuple of strings, and
`mark_watch_notified(alert_ids) -> None`.

Use an `RLock`, atomic temporary-file replacement, local-day filtering, pending-first ordering, and alert IDs formed as `<thread-id>:<timestamp>`. First initialization records EOF offsets without replay. Later refreshes read complete appended lines from saved offsets and do not advance past an incomplete trailing line.

- [ ] **Step 4: Implement thread discovery and event reduction**

Query up to 50 non-subagent OpenAI threads ordered by `updated_at_ms`, selecting `id`, `cwd`, `title`, and `rollout_path`. `task_complete` appends pending; `user_message` handles earlier pending alerts for that thread only. New threads discovered after restart process only events newer than persisted `last_scan_at`.

- [ ] **Step 5: Run tests and commit**

Run the focused test and full Python suite, then commit `task_alerts.py` and its test as `Add persistent Codex task alert tracker`.

### Task 2: Collector And One-Time Watch Notification

**Files:**
- Modify: `mac-codex-agent/codex_adapter.py`
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_codex_adapter.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`
- Delete: `mac-codex-agent/completion.py`
- Delete: `mac-codex-agent/tests/test_completion.py`

- [ ] **Step 1: Write failing collector and chime tests**

Assert status contains `codex_alert_pending`, `codex_alert_handled`, `codex_alert_revision`, and at most eight public alert objects, while `codex_today_min` and `codex_week_min` are absent. Assert one chime marks all currently pending unnotified IDs only after a successful event write; a failed write leaves them unnotified for retry.

- [ ] **Step 2: Remove activity-minute collection**

Remove usage paths, frontmost-app subprocess calls, day/week helpers, `activity_loop`, and its runtime thread. Leave the existing usage JSON file untouched on disk.

- [ ] **Step 3: Integrate `TaskAlertTracker`**

Production `AgentController` constructs one tracker and passes it to `CodexStateCollector`. `get_state()` calls `refresh()` and emits public counts and the first eight ordered records using 32-character project and 48-character title limits.

- [ ] **Step 4: Replace completion polling with tracker notification**

After a successful status write, `CodexStatusAgent.tick()` obtains all pending unnotified IDs. If nonempty, send one `{"type":"chime","agent":"codex"}` event; only after success call `mark_watch_notified(ids)`. Delete the old latest-completion reader and baseline state.

- [ ] **Step 5: Run tests and commit**

Run unittest discovery and commit the collector/agent/runtime changes as `Integrate Codex task alerts with BLE`.

### Task 3: Mac Menu Alert Ledger

**Files:**
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

- [ ] **Step 1: Write failing presentation tests**

Remove today/week assertions. Add immutable `TaskAlertView` values and assert pending/handled counts, pending-first ordering, title/time formatting, and a summary title exactly like `Task Alerts · 2 Pending / 4 Handled`.

- [ ] **Step 2: Extend combined snapshots**

Replace activity fields in `CodexSnapshot` and `MenuPresentation` with alert counts, revision, and alert tuples. Parse only valid `pending`/`handled` entries, preserving valid zero counts.

- [ ] **Step 3: Build a dynamic submenu**

Add a `Task Alerts` parent item below Codex Status. Rebuild its submenu only when revision changes. Use disabled custom views for section headings and rows; pending uses `systemOrangeColor`, handled uses `secondaryLabelColor`. Show every current-day Mac record.

- [ ] **Step 4: Run tests and commit**

Run menu-focused and full tests, then commit as `Show Codex task alert ledger in menu`.

### Task 4: Firmware Alert Payload Model

**Files:**
- Modify: `main/apps/app_codex_status/codex_status_model.h`
- Modify: `main/apps/app_codex_status/codex_status_model.cpp`
- Create: `main/apps/app_codex_status/codex_alert_carousel.h`
- Create: `main/apps/app_codex_status/codex_alert_carousel.cpp`

- [ ] **Step 1: Define bounded alert types**

Replace minute fields with `pending_count`, `handled_count`, `alert_revision`, and a vector capped at eight records. Each record has project, title, time, and a pending boolean.

- [ ] **Step 2: Parse defensively**

Parse counts only as nonnegative integers. Accept an alert only when project/title/time are strings and state is `pending` or `handled`. Reject an invalid alerts array as a unit while retaining scalar model/effort/quota parsing.

- [ ] **Step 3: Add pure carousel state**

`CodexAlertCarousel` stores a revision, ordered records, current index, and last-rotation timestamp. A new revision resets index to zero; `update(now_ms)` advances every 3000 ms; empty, pending, and all-handled headings are returned as pure strings for the LVGL layer.

- [ ] **Step 4: Build firmware**

Run the existing ESP-IDF build and ensure the launcher image stays inside its partition.

- [ ] **Step 5: Commit**

Commit parser and carousel files as `Add StopWatch Codex alert model`.

### Task 5: StopWatch Alert Carousel UI

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] **Step 1: Replace Activity widgets**

Track the heading object and rename member pointers to `_alert_project` and `_alert_detail`. Initialize them as `NO ALERTS`, `--`, and an empty detail. Give both value labels fixed widths, centered alignment, and trailing-dot long mode.

- [ ] **Step 2: Apply parsed alert snapshots**

On valid status, update the carousel only when revision changes and immediately render the first record. In `onRunning()`, rotate every three seconds and render heading, project, and `<time> · <title>` detail.

- [ ] **Step 3: Preserve lifecycle boundaries**

Reset carousel state on open/close, keep completion chime handling unchanged, and preserve two-button long-press home behavior, BLE fallback, battery, quotas, and assistant OTA boundaries.

- [ ] **Step 4: Build and commit**

Build again and commit as `Show Codex task alerts on StopWatch`.

### Task 6: Package, Install, Flash, And Verify

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Package tracker and remove completion module**

Copy `task_alerts.py` into the app bundle and stop copying `completion.py`. Update installer contract tests.

- [ ] **Step 2: Document alerts and acknowledgement semantics**

Document per-conversation grouping, reply handling, persistence path, first-run baseline, summary reconnect chime, menu submenu, and watch eight-record limit.

- [ ] **Step 3: Run final automated checks**

Run shell syntax, Python tests, `git diff --check`, firmware build, binary size check, and code-sign verification.

- [ ] **Step 4: Install and relaunch the Mac agent**

Quit the old bundle, run the installer, manually open the app, confirm the tracker baseline produces no old chime, and confirm no LaunchAgent exists.

- [ ] **Step 5: Flash launcher only**

Flash the launcher application partition on the connected StopWatch without erasing NVS, assets, or assistant OTA. Verify flash hash and boot.

- [ ] **Step 6: Run the real event loop**

Complete a Codex turn, confirm one sound and one pending record, reply in that thread, and confirm handled state within two seconds. Reopen the watch app, rotate records, return home with the two-button long press, and verify assistant startup.

- [ ] **Step 7: Commit packaging/docs fixes if needed**

Commit only scoped source/documentation changes; never stage generated `build/` output or unrelated launcher assets.
