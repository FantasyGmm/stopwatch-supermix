# Real-Time Pending Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wake the Mac-to-watch status path immediately when a Codex completion becomes Pending while guaranteeing that the displayed status and chime use one snapshot.

**Architecture:** Add a standard-library macOS vnode wake source behind the existing collector interface, publish collected snapshots with a monotonic runtime generation, and bind unnotified IDs to that snapshot. Keep the two-second heartbeat as recovery and use the negotiated BLE write length to deliver the existing full status efficiently.

**Tech Stack:** Python 3.9 `asyncio`, `select.kqueue`, existing JSON/BLE framing, Python `unittest`, ESP-IDF firmware contract tests.

---

### Task 1: Bind notification IDs to the collected snapshot

**Files:**
- Modify: `mac-codex-agent/codex_adapter.py`
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/tests/test_codex_adapter.py`
- Modify: `mac-codex-agent/tests/test_agent.py`

- [ ] **Step 1: Write failing snapshot tests**

Add an adapter assertion that `get_state()` includes a private tuple of IDs for
Pending rows whose `watch_notified` value is false. Add an agent race test that
passes a status with no private IDs, mutates the collector while the status is
being written, and asserts that no chime is emitted from the newer live ledger.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_codex_adapter \
  mac-codex-agent.tests.test_agent.CodexStatusAgentTests
```

Expected: failures show that the private snapshot IDs are absent and `tick()`
still calls `pending_watch_notification_ids()` after sending the status.

- [ ] **Step 3: Implement minimal snapshot ownership**

Build `_codex_watch_notification_ids` directly from the `TaskAlertSnapshot` in
`CodexStateCollector.get_state()`. In `CodexStatusAgent.tick()`, pop or read only
that private key from the provided status, let `_watch_codex_status()` continue
to filter it from transmission, and acknowledge exactly those IDs after status
and chime writes succeed.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the command from Step 2. Expected: all selected tests pass.

- [ ] **Step 5: Commit the atomic snapshot slice**

```bash
git add mac-codex-agent/codex_adapter.py mac-codex-agent/agent.py \
  mac-codex-agent/tests/test_codex_adapter.py mac-codex-agent/tests/test_agent.py
git commit -m "fix: bind pending chimes to status snapshots"
```

### Task 2: Wake collection on rollout changes

**Files:**
- Create: `mac-codex-agent/rollout_change_watcher.py`
- Create: `mac-codex-agent/tests/test_rollout_change_watcher.py`
- Modify: `mac-codex-agent/task_alerts.py`
- Modify: `mac-codex-agent/codex_adapter.py`
- Modify: `mac-codex-agent/combined_collector.py`
- Modify: `mac-codex-agent/tests/test_task_alerts.py`
- Modify: `mac-codex-agent/tests/test_codex_adapter.py`
- Modify: `mac-codex-agent/tests/test_combined_collector.py`

- [ ] **Step 1: Write failing watcher and delegation tests**

Cover a temporary file append waking before a one-second timeout, a no-change
timeout, a pre-registration signature change returning immediately, and the
collector delegation chain. Assert that tracker watch paths contain its state
database plus every current cursor rollout.

- [ ] **Step 2: Run watcher tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_rollout_change_watcher \
  mac-codex-agent.tests.test_task_alerts \
  mac-codex-agent.tests.test_codex_adapter \
  mac-codex-agent.tests.test_combined_collector
```

Expected: import or attribute failures identify the missing watcher and
`wait_for_change()` delegation.

- [ ] **Step 3: Implement bounded vnode waiting**

Create a small watcher module that opens only existing regular files, captures
`st_size` and `st_mtime_ns`, registers `KQ_FILTER_VNODE` with write/extend/delete/
rename/revoke flags, rechecks signatures after registration, and returns `True`
on change or `False` on timeout. Close every descriptor and kqueue in `finally`.
If `select.kqueue` is unavailable, block only for the supplied timeout and
return `False`.

Expose the tracker's current database, WAL, and cursor paths through
`wait_for_change(timeout)`, then delegate through `CodexStateCollector` and
`CompositeStateCollector`.

- [ ] **Step 4: Run watcher tests and verify GREEN**

Run the command from Step 2. Expected: all selected tests pass without leaked
threads or descriptors.

- [ ] **Step 5: Commit the change watcher slice**

```bash
git add mac-codex-agent/rollout_change_watcher.py \
  mac-codex-agent/tests/test_rollout_change_watcher.py \
  mac-codex-agent/task_alerts.py mac-codex-agent/codex_adapter.py \
  mac-codex-agent/combined_collector.py \
  mac-codex-agent/tests/test_task_alerts.py \
  mac-codex-agent/tests/test_codex_adapter.py \
  mac-codex-agent/tests/test_combined_collector.py
git commit -m "feat: wake task collection on rollout changes"
```

### Task 3: Wake the serialized BLE publisher immediately

**Files:**
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`

- [ ] **Step 1: Write failing runtime timing tests**

Use a fake collector whose `wait_for_change()` is released by the test. Set a
30-second heartbeat, publish an initial state, release one change, and assert a
second status reaches the fake transport within one second. Add a rapid-change
case asserting serialized writes and eventual delivery of the latest revision.

- [ ] **Step 2: Run runtime tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_runtime.AgentRuntimeTests
```

Expected: the second status times out because collection and transmission still
sleep for the heartbeat interval.

- [ ] **Step 3: Implement generation-based wakeup**

Add an `asyncio.Event` and integer status generation owned by `AgentRuntime`.
After every successful collection, publish the status, increment the generation,
and set the event. Replace collector sleep with its optional bounded
`wait_for_change()`. Replace the BLE interval wait with a race-safe loop that
clears the event, rechecks the generation, and waits for generation change,
heartbeat timeout, disconnect, or voice failure. Keep one `_run_session()`
writer so BLE frames never interleave.

- [ ] **Step 4: Run runtime tests and verify GREEN**

Run the command from Step 2. Expected: all runtime tests pass and the timing
test completes well below the configured 30-second heartbeat.

- [ ] **Step 5: Commit the runtime wakeup slice**

```bash
git add mac-codex-agent/runtime.py mac-codex-agent/tests/test_runtime.py
git commit -m "feat: publish Codex status on change"
```

### Task 4: Use negotiated BLE status frame sizes

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Test: `tests/test_codex_ble_framing.cpp`

- [ ] **Step 1: Write failing negotiated-size tests**

Create a fake characteristic reporting a 244-byte maximum write length and
assert a 1000-byte status uses frames no larger than 244 bytes and far fewer
writes than the 20-byte fallback. Add invalid, absent, and greater-than-256
values that must use 20, 20, and 256 bytes respectively.

- [ ] **Step 2: Run transport tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_agent.CodexStatusAgentTests
```

Expected: the negotiated-size assertion fails because `SAFE_WRITE_SIZE` is
always 20.

- [ ] **Step 3: Implement bounded negotiated framing**

Resolve the status characteristic from the connected Bleak service collection,
read its maximum write-without-response length as a conservative negotiated
capacity signal, clamp it to `[20, 256]`, and pass it to `encode_message()` while
continuing reliable `response=True` writes. Preserve burst yielding whenever a
fallback payload still exceeds the watch's queue depth.

- [ ] **Step 4: Run Python and firmware framing tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_agent
./scripts/run_host_tests.sh --filter codex_ble_framing
```

Expected: both suites pass.

- [ ] **Step 5: Commit the transport slice**

```bash
git add mac-codex-agent/agent.py mac-codex-agent/tests/test_agent.py
git commit -m "perf: use negotiated BLE status frames"
```

### Task 5: Documentation, full verification, installation, and physical check

**Files:**
- Modify: `mac-codex-agent/README.md`
- Modify: `PRODUCT.md`

- [ ] **Step 1: Update behavior documentation**

Document event-driven Codex task updates, the two-second recovery heartbeat,
status-before-chime ordering, disconnected backlog behavior, and the absence of
raw rollout content on the wire.

- [ ] **Step 2: Run full relevant verification**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests
./scripts/run_host_tests.sh
git diff --check
```

Expected: all Python and host tests pass and diff-check emits no output.

- [ ] **Step 3: Install the updated menu application**

Run:

```bash
./mac-codex-agent/install_launch_agent.sh
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Expected: installation succeeds, the application opens manually, and BLE
returns to Connected without creating a LaunchAgent.

- [ ] **Step 4: Perform physical latency validation**

Start a Codex turn that ends with a direct question. Record the completion log
timestamp, the first status write completion, and the chime write completion.
Confirm on the watch that the Pending count changes no later than the sound and
that normal-path completion-to-visible latency is below 500 ms when the link is
already connected. Disconnect and reconnect once to confirm retained summary
notification behavior.

- [ ] **Step 5: Commit documentation and final plan status**

```bash
git add mac-codex-agent/README.md PRODUCT.md \
  docs/superpowers/specs/2026-08-11-realtime-pending-sync-design.md \
  docs/superpowers/plans/2026-08-11-realtime-pending-sync.md
git commit -m "docs: describe real-time Pending sync"
```
