# Codex Menu Status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the six watch-visible Codex values to the Mac menu and rename the persistent BLE toggle to `Enable Watch Connection`.

**Architecture:** `AgentRuntime` runs one connection-independent collector task and stores an immutable Codex snapshot alongside Bluetooth state. AppKit renders that combined snapshot, while the BLE sender transmits the same sampled dictionary instead of performing a second collection.

**Tech Stack:** Python 3.9, asyncio, PyObjC/AppKit, Bleak/CoreBluetooth, unittest.

---

### Task 1: Codex Snapshot And Menu Presentation

**Files:**
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`

- [ ] **Step 1: Write failing snapshot and formatting tests**

Add tests for a complete status dictionary, partial values, valid zero values, effort title casing, percentage clamping, and minute clamping:

```python
codex = CodexSnapshot.from_status({
    "codex_model": "gpt-5.3",
    "codex_effort": "high",
    "codex_5h_left_pct": 72,
    "codex_week_left_pct": 64,
    "codex_today_min": 38,
    "codex_week_min": 214,
})
view = present(AgentSnapshot(ConnectionState.CONNECTED, True, codex=codex))
self.assertEqual(view.model_text, "Model: gpt-5.3")
self.assertEqual(view.effort_text, "Effort: High")
self.assertEqual(view.five_hour_text, "5H Remaining: 72%")
self.assertEqual(view.seven_day_text, "7D Remaining: 64%")
self.assertEqual(view.today_text, "Today: 38 min")
self.assertEqual(view.this_week_text, "This Week: 214 min")
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_app_state.py -v`.

Expected: import or attribute failures for `CodexSnapshot` and the six presentation fields.

- [ ] **Step 3: Implement immutable Codex state and formatting**

Add:

```python
def normalized_integer(value, maximum=None):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    result = max(0, int(value))
    return min(maximum, result) if maximum is not None else result


@dataclasses.dataclass(frozen=True)
class CodexSnapshot:
    model: str = ""
    effort: str = ""
    five_hour_left_pct: Optional[int] = None
    week_left_pct: Optional[int] = None
    today_minutes: Optional[int] = None
    week_minutes: Optional[int] = None
    updated_at: float = 0.0

    @classmethod
    def from_status(cls, status, now=time.time):
        return cls(
            model=str(status.get("codex_model") or ""),
            effort=str(status.get("codex_effort") or ""),
            five_hour_left_pct=normalized_integer(
                status.get("codex_5h_left_pct"), 100
            ),
            week_left_pct=normalized_integer(
                status.get("codex_week_left_pct"), 100
            ),
            today_minutes=normalized_integer(status.get("codex_today_min")),
            week_minutes=normalized_integer(status.get("codex_week_min")),
            updated_at=now(),
        )
```

Normalize numeric fields only when they are real numbers but not booleans. Clamp quota to `0..100` and activity to zero or above. Add `codex: CodexSnapshot` to `AgentSnapshot` with a default factory so existing call sites remain compatible. Extend `MenuPresentation` and `present()` with six formatted labels.

- [ ] **Step 4: Run focused and complete tests**

Run focused tests, then unittest discovery. Expected: all tests pass.

- [ ] **Step 5: Commit**

Run `git add mac-codex-agent/app_state.py mac-codex-agent/tests/test_app_state.py && git commit -m "Add Codex status menu presentation"`.

### Task 2: Shared Collector And BLE Payload

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`

- [ ] **Step 1: Write failing shared-snapshot tests**

Add runtime tests proving collection continues while connection is disabled:

```python
runtime = AgentRuntime(
    collector=CountingCollector(status),
    transport_factory=factory,
    connection_enabled=False,
    status_interval=0.01,
)
task = asyncio.create_task(runtime.run())
await wait_for_codex_model(runtime, "gpt-5.3")
self.assertEqual(runtime.snapshot().state, ConnectionState.DISABLED)
runtime.submit(AgentCommand.STOP)
await task
```

Also prove a Bluetooth transition preserves Codex data, collector failure preserves the last successful snapshot, and the exact collected dictionary is passed to the transport sender without a second collector call.

- [ ] **Step 2: Run focused tests and verify failure**

Run `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py -v`.

Expected: failures because collection currently occurs only inside a connected BLE session.

- [ ] **Step 3: Let the status sender accept an existing sample**

Change the sender interface without breaking direct tests:

```python
async def tick(self, status=None):
    if status is None:
        status = await asyncio.to_thread(self.collector.get_state)
    await self.transport.send_json(json.dumps(status, separators=(",", ":")), STATUS_MESSAGE)
    await self._send_new_completion_if_needed()
    return status
```

Keep completion baseline and one-chime semantics unchanged.

- [ ] **Step 4: Add the independent collector task**

At `AgentRuntime.run()` startup, create `_collect_status()` and cancel/await it in `finally`. The loop immediately calls `collector.get_state` through `asyncio.to_thread`, then sleeps for `status_interval`. On success it updates `_latest_status`, sets an initial-status event, and replaces only `AgentSnapshot.codex`. On exception it logs and preserves the previous sample.

`_transition()` must preserve the current Codex snapshot. `_run_session()` waits for the initial-status event and calls `status_agent.tick(dict(self._latest_status))`; it never calls the collector itself.

- [ ] **Step 5: Run complete tests and commit**

Run unittest discovery, then `git add mac-codex-agent/agent.py mac-codex-agent/runtime.py mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py && git commit -m "Share Codex status between menu and watch"`.

### Task 3: Render The Codex Menu Section

**Files:**
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

- [ ] **Step 1: Write a failing AppKit menu test**

Return a complete Codex snapshot from `FakeController.snapshot()` and assert:

```python
self.assertEqual(delegate.menu.itemAtIndex_(0).title(), "Codex Status")
self.assertEqual(delegate.model_item.title(), "Model: gpt-5.3")
self.assertEqual(delegate.this_week_item.title(), "This Week: 214 min")
self.assertEqual(delegate.connection_item.title(), "Enable Watch Connection")
```

Expected: failure because the menu still starts with `Agent: Running` and uses the old toggle label.

- [ ] **Step 2: Build and refresh the top status section**

Create seven disabled rows before the first separator: header plus model, effort, five-hour, seven-day, today, and this-week values. Keep Agent, Bluetooth, and Device below that separator. Rename only the checked toggle item; preserve its selector, preference, and behavior.

In `refresh_()`, update all six rows from `MenuPresentation`. Do not trigger collection or BLE work from the main thread.

- [ ] **Step 3: Run menu and complete tests**

Run the focused menu tests and unittest discovery. Expected: all tests pass.

- [ ] **Step 4: Commit**

Run `git add mac-codex-agent/menu_app.py mac-codex-agent/tests/test_menu_app.py && git commit -m "Show Codex values in menu bar"`.

### Task 4: Documentation, Installation, And Live Verification

**Files:**
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Update documentation**

Document the six top-section rows, shared two-second sampling, `--` behavior, and `Enable Watch Connection` semantics. Keep the manual-only startup and Quit documentation unchanged.

- [ ] **Step 2: Run final static and automated checks**

Run `bash -n mac-codex-agent/install_launch_agent.sh`, unittest discovery, and `git diff --check -- mac-codex-agent docs/superpowers`.

- [ ] **Step 3: Stop, reinstall, and manually relaunch**

Quit the current bundle, run `./mac-codex-agent/install_launch_agent.sh`, and explicitly open `~/Applications/StopWatch Codex Agent.app`. Confirm no LaunchAgent exists.

- [ ] **Step 4: Verify live menu data and BLE independence**

Confirm logs show successful menu startup and collection. With the watch absent or connection disabled, confirm all six values continue updating. Re-enable connection and leave the menu agent running.

- [ ] **Step 5: Commit documentation**

Run `git add mac-codex-agent/README.md && git commit -m "Document Codex menu status"`.

Do not stage unrelated firmware build output, launcher assets, or user-created icon files.
