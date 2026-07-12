# Codex Conversation State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Track each Codex conversation as Pending, Running, or Completed and render the watch with strict `Pending > Running > Completed` priority.

**Architecture:** Extend the JSONL reducer to consume `task_started` and classify the single `task_complete` event into waiting or completed. Persist transitions in the existing ledger, expose only the latest current-day record per conversation, propagate three-state counts through the Mac/BLE contract, and make the firmware carousel rotate only the highest-priority active state.

**Tech Stack:** Python 3.9, `unittest`, JSONL/SQLite, PyObjC/AppKit, BLE JSON framing, C++17, cJSON, LVGL 9, ESP-IDF 5.5.4.

---

### Task 1: Build The Conversation State Reducer

**Files:**
- Modify: `mac-codex-agent/task_alerts.py`
- Modify: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Add failing state-transition tests**

Extend the fixture to emit `task_started`. Cover these exact transitions:

```python
append_event(rollout, "task_started", "2026-07-12T00:01:00Z")
append_event(rollout, "user_message", "2026-07-12T00:01:01Z")
assert tracker.refresh().alerts[0].state == "running"

append_event(rollout, "task_complete", "2026-07-12T00:02:00Z",
             last_agent_message="请确认是否继续？")
assert tracker.refresh().alerts[0].state == "pending"

append_event(rollout, "task_complete", "2026-07-12T00:03:00Z",
             last_agent_message="Build and tests passed.")
assert tracker.refresh().alerts[0].state == "completed"
assert tracker.pending_watch_notification_ids() == ()
```

- [ ] **Step 2: Add failing latest-state and migration tests**

Create Pending, Running, and Completed conversations and assert priority order
and independent counts. Create two completed turns in one conversation and
assert only its latest record is exposed. Load a version-one ledger containing
`state=handled` and assert migration to version two `state=completed` preserves
timestamps and notification flags.

- [ ] **Step 3: Run tests and observe failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_task_alerts.py -v
```

Expected: Running and Completed counts are missing and every `task_complete`
still creates Pending.

- [ ] **Step 4: Implement the event reducer**

Set `LEDGER_VERSION = 2`. Add `running_count` and `completed_count` to
`TaskAlertSnapshot`. Consume all three lifecycle events:

```python
if event_type == "task_started":
    alerts.append(new_record(thread, timestamp_text, "running", "running"))
elif event_type == "task_complete":
    outcome = classify_outcome(payload.get("last_agent_message"))
    state = "pending" if outcome == "waiting" else "completed"
    record = latest_running_record(thread.id, timestamp)
    transition_or_create(record, thread, timestamp_text, state, outcome)
else:  # user_message
    complete_older_pending_records(thread.id, timestamp_text)
```

`user_message` must not modify Running because real rollouts emit
`task_started` before `user_message`. Only a Pending transition sets
`watch_notified=False`.

- [ ] **Step 5: Expose one latest record per conversation**

Filter to today, retain the newest `progress_at` per `thread_id`, then sort with:

```python
priority = {"pending": 0, "running": 1, "completed": 2}
alerts.sort(key=lambda alert: (priority[alert.state], -timestamp(alert)))
```

Pending notification IDs continue selecting Pending only.

- [ ] **Step 6: Migrate and persist old ledgers**

Accept versions one and two. Convert `handled` to `completed`, normalize
outcomes to `running`, `waiting`, or `completed`, write version two, and preserve
`handled_at` and `watch_notified`.

- [ ] **Step 7: Run tests and commit**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_task_alerts.py -v
git add mac-codex-agent/task_alerts.py \
  mac-codex-agent/tests/test_task_alerts.py
git commit -m "Track Codex conversation states"
```

### Task 2: Propagate States Through The Mac Agent

**Files:**
- Modify: `mac-codex-agent/codex_adapter.py`
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/tests/test_codex_adapter.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

- [ ] **Step 1: Add failing contract and menu tests**

Assert collector output includes `codex_alert_pending`, `codex_alert_running`,
and `codex_alert_completed`. Assert the bounded watch payload accepts only
`pending`, `running`, and `completed`. Expect menu summary:

```text
Task Alerts · 1 Pending / 1 Running / 1 Completed
```

Expect orange Pending, blue Running, and dim Completed groups in that order.

- [ ] **Step 2: Run focused tests and observe failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_adapter.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_app_state.py \
  mac-codex-agent/tests/test_menu_app.py -v
```

- [ ] **Step 3: Extend the shared status model**

Emit Running and Completed counts from `CodexStateCollector`. Replace handled
presentation fields with Running and Completed in `CodexSnapshot`. Validate the
new three states and matching outcomes in `_alert_views()`.

- [ ] **Step 4: Update menu and BLE payload**

Use:

```python
groups = (
    ("Pending", "pending", NSColor.systemOrangeColor()),
    ("Running", "running", NSColor.systemBlueColor()),
    ("Completed", "completed", NSColor.secondaryLabelColor()),
)
```

Keep eight-record and 2048-byte limits, title truncation, paced BLE writes, and
Pending-only chimes.

- [ ] **Step 5: Run tests and commit**

Run the four focused modules, then commit the eight implementation/test files
with message `Show Codex running conversations`.

### Task 3: Implement Firmware Priority Rotation

**Files:**
- Modify: `main/apps/app_codex_status/codex_status_model.h`
- Modify: `main/apps/app_codex_status/codex_status_model.cpp`
- Modify: `main/apps/app_codex_status/codex_alert_carousel.h`
- Modify: `main/apps/app_codex_status/codex_alert_carousel.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/codex_alert_carousel_test.cpp`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`

- [ ] **Step 1: Add failing parser and carousel tests**

Introduce test fixtures for:

```cpp
enum class CodexTaskState { Pending, Running, Completed };
```

Assert mixed states rotate Pending only, Running rotates only when Pending is
absent, Completed never rotates, and headings are `PENDING X/N`, `RUNNING X/N`,
and `COMPLETED`.

- [ ] **Step 2: Run focused tests and observe failure**

```bash
clang++ -std=c++17 -Imain/apps/app_codex_status \
  tests/codex_alert_carousel_test.cpp \
  main/apps/app_codex_status/codex_alert_carousel.cpp \
  -o /tmp/codex_alert_carousel_test && /tmp/codex_alert_carousel_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py -v
```

- [ ] **Step 3: Parse the new model**

Replace alert booleans with `CodexTaskState state`; add Running and Completed
counts. Parse only `pending`, `running`, and `completed`, rejecting other values.

- [ ] **Step 4: Select and rotate by priority**

```cpp
CodexTaskState CodexAlertCarousel::display_state() const
{
    if (_pending_count > 0) return CodexTaskState::Pending;
    if (_running_count > 0) return CodexTaskState::Running;
    return CodexTaskState::Completed;
}
```

Select the first matching record on apply. Rotate only records matching the
selected active state. Return false for Completed or fewer than two matches.

- [ ] **Step 5: Render state-specific labels and colors**

```text
Pending   -> 等待指令 / red
Running   -> 正在处理 / blue
Completed -> 已完成   / green
```

Retain `NO ALERTS` and the bundled Chinese font.

- [ ] **Step 6: Run tests and commit**

Run both focused commands from Step 2. Commit the seven firmware/test files
with message `Prioritize Codex pending and running states`.

### Task 4: Install, Build, Flash, And Verify

**Files:**
- Modify: `README.md`
- Modify: `mac-codex-agent/README.md`
- Verify: `build/StopWatch-UserDemo.bin`

- [ ] **Step 1: Update documentation**

Document event mapping, local waiting classification, latest state per
conversation, display priority, menu groups, and Pending-only chimes.

- [ ] **Step 2: Run all checks and build**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
clang++ -std=c++17 -Imain/apps/app_codex_status \
  tests/codex_alert_carousel_test.cpp \
  main/apps/app_codex_status/codex_alert_carousel.cpp \
  -o /tmp/codex_alert_carousel_test && /tmp/codex_alert_carousel_test
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

- [ ] **Step 3: Reinstall the Mac agent**

Stop the menu process, run `mac-codex-agent/install_launch_agent.sh`, and open
`~/Applications/StopWatch Codex Agent.app`. Confirm no LaunchAgent plist exists.

- [ ] **Step 4: Flash launcher only**

```bash
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x20000 build/StopWatch-UserDemo.bin
```

Do not write NVS, assets, or assistant `ota_1`.

- [ ] **Step 5: Verify real behavior**

Confirm blue Running during work, red Pending plus one chime for a waiting
response, no Pending for normal completion, Pending suppressing Running,
Running returning after Pending acknowledgement, and static green Completed
when neither active state exists.

- [ ] **Step 6: Commit documentation**

Commit only `README.md` and `mac-codex-agent/README.md` with message
`Document Codex conversation states`.
