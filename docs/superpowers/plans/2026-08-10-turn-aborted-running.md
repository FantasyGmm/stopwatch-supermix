# Turn-Aborted Running State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire Running alerts when Codex emits `turn_aborted`, including stale alerts already persisted by an older reducer.

**Architecture:** Extend the ordered task event reducer with a turn-aware abort transition. Add a small streaming rollout query for one-time migration of only conversations whose latest alert remains Running, guarded by an independent reducer version and retry list.

**Tech Stack:** Python 3.9, JSONL streaming, `unittest`, dataclasses.

---

### Task 1: Reduce Live `turn_aborted` Events

**Files:**
- Modify: `mac-codex-agent/task_alerts.py:596-725`
- Test: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Write live lifecycle regression tests**

Add tests that drive the real tracker seam:

```python
def test_aborted_turn_retires_running_without_notification(self):
    tracker = self.tracker()
    tracker.refresh()
    append_event(self.rollout, "task_started", "2026-07-12T00:01:00Z", turn_id="turn-a")
    append_event(self.rollout, "turn_aborted", "2026-07-12T00:01:05Z", turn_id="turn-a")

    snapshot = tracker.refresh()

    self.assertEqual(snapshot.running_count, 0)
    self.assertEqual(snapshot.pending_count, 0)
    self.assertEqual(snapshot.completed_count, 1)
    self.assertEqual(tracker.pending_watch_notification_ids(), ())
    saved = json.loads(self.ledger.read_text(encoding="utf-8"))
    self.assertEqual(saved["alerts"][-1]["reason"], "turn_aborted")

def test_late_abort_cannot_clear_a_newer_turn(self):
    tracker = self.tracker()
    tracker.refresh()
    append_event(self.rollout, "task_started", "2026-07-12T00:01:00Z", turn_id="turn-a")
    append_event(self.rollout, "task_started", "2026-07-12T00:01:05Z", turn_id="turn-b")
    append_event(self.rollout, "turn_aborted", "2026-07-12T00:01:06Z", turn_id="turn-a")

    snapshot = tracker.refresh()

    self.assertEqual(snapshot.running_count, 1)
    self.assertEqual(snapshot.alerts[0].progress_at, "2026-07-12T00:01:05Z")
```

Also recreate the tracker after a processed abort and assert its revision and
snapshot remain unchanged.

- [ ] **Step 2: Run the live tests and verify RED**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_task_alerts.TaskAlertTrackerTests.test_aborted_turn_retires_running_without_notification \
  mac-codex-agent.tests.test_task_alerts.TaskAlertTrackerTests.test_late_abort_cannot_clear_a_newer_turn \
  mac-codex-agent.tests.test_task_alerts.TaskAlertTrackerTests.test_aborted_turn_is_idempotent_across_restart
```

Expected: failures because `turn_aborted` is ignored and Running remains.

- [ ] **Step 3: Implement the minimal turn-aware transition**

Persist `turn_id` on newly created Running entries. Accept `turn_aborted` in
the event whitelist and add a helper with this contract:

```python
def _complete_aborted_running(
    self,
    thread: _ThreadInfo,
    timestamp: datetime.datetime,
    turn_id: str,
    *,
    require_active_signal: bool,
) -> bool:
    """Complete only the Running alert owned by the aborted turn."""
```

For live events, require the current `turn_signals[thread.id].turn_id` to match.
Update only the newest compatible Running alert to completed, set both times to
the abort time, set `watch_notified=True`, record `reason="turn_aborted"`, and
remove only the matching signal. Increment the ledger revision once in the
event caller.

- [ ] **Step 4: Run live lifecycle and tracker tests**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_task_alerts.py
```

Expected: all tracker tests pass.

- [ ] **Step 5: Commit the live reducer**

```bash
git add mac-codex-agent/task_alerts.py mac-codex-agent/tests/test_task_alerts.py
git commit -m "Handle aborted Codex turns"
```

### Task 2: Repair Existing Stale Running Alerts

**Files:**
- Modify: `mac-codex-agent/codex_turn_evidence.py`
- Modify: `mac-codex-agent/task_alerts.py`
- Test: `mac-codex-agent/tests/test_codex_turn_evidence.py`
- Test: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Write replay and migration regression tests**

Add a streaming helper contract test:

```python
abort = matching_turn_abort(self.rollout, started_at)
self.assertEqual(abort.turn_id, "turn-a")
self.assertEqual(abort.timestamp, abort_time)
```

Add a tracker test that processes `task_started`, appends a matching abort,
manually advances the saved cursor to EOF to model an old agent, removes the
abort reducer version, and recreates the tracker. Assert that refresh repairs
the latest alert once. Add an incomplete trailing abort case that records the
thread in `abort_retry_threads`, then completes the line and asserts the retry
repairs the alert and clears the retry list.

- [ ] **Step 2: Run replay and migration tests and verify RED**

Run the new methods explicitly. Expected: import or assertion failures because
the streaming abort query and reducer-version migration do not exist.

- [ ] **Step 3: Implement streaming abort lookup**

In `codex_turn_evidence.py`, add:

```python
@dataclasses.dataclass(frozen=True)
class MatchingTurnAbort:
    timestamp: datetime.datetime
    turn_id: str


def matching_turn_abort(
    path: pathlib.Path,
    started_at: datetime.datetime,
) -> Optional[MatchingTurnAbort]:
    """Stream one rollout and return the abort matching that exact start."""
```

Match timestamps as normalized datetimes, require a non-empty turn ID, return
only a later `turn_aborted` with the same ID, treat an incomplete tail and read
errors as retryable, and treat malformed complete JSON as non-retryable.

- [ ] **Step 4: Implement one-time reducer reconciliation**

Add `ABORT_REDUCER_VERSION = 1`, `abort_reducer_version`, and
`abort_retry_threads` to the ledger. During refresh, after normal scanning,
inspect only each conversation's latest Running alert. On a version change or
retry, call `matching_turn_abort`; apply `_complete_aborted_running` with
`require_active_signal=False`. Advance the version globally, retain retryable
thread IDs, preserve the alert on every replay failure, and increment revision
only when persisted state changes.

- [ ] **Step 5: Run migration and integration tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_codex_adapter.py
```

Expected: all tests pass, including notification idempotence.

- [ ] **Step 6: Commit migration**

```bash
git add mac-codex-agent/codex_turn_evidence.py \
  mac-codex-agent/task_alerts.py \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py
git commit -m "Repair stale aborted task alerts"
```

### Task 3: Verify the Live Reproduction and Package Contract

**Files:**
- No source changes expected.

- [ ] **Step 1: Run focused verification**

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_completion_classifier.py \
  mac-codex-agent/tests/test_codex_turn_evidence.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_agent.py \
  mac-codex-agent/tests/test_codex_adapter.py \
  mac-codex-agent/tests/test_install_script.py
python3 -m py_compile mac-codex-agent/codex_turn_evidence.py \
  mac-codex-agent/task_alerts.py
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check
```

- [ ] **Step 2: Replay the captured live ledger in a temporary copy**

Copy the ledger to a temporary directory, run one refresh against the real
read-only state database and rollouts, and assert the stale `UI设计` alert is
completed while the three currently active conversations remain Running. Do
not mutate the user's live ledger during this verification.

- [ ] **Step 3: Review scope and commit any documentation-only correction**

Confirm only the lifecycle reducer, replay helper, tests, and the approved
design/plan changed. No wording classifier, firmware, BLE, or unrelated UI
files may enter the diff.
