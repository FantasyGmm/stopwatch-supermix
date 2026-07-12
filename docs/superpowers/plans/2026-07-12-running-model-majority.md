# Running Model Majority Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Select and persist the most common real `(model, effort)` pair among conversations currently in Running state.

**Architecture:** After obtaining the latest conversation-state snapshot, the Mac collector joins Running thread IDs to `state_5.sqlite`, ranks complete model/effort pairs by count and latest Running timestamp, and atomically caches the winner. Existing menu and BLE output fields remain unchanged.

**Tech Stack:** Python 3.9, `unittest`, SQLite, JSON, atomic file replacement, existing Mac menu agent.

---

### Task 1: Specify Pair Ranking And Persistence

**Files:**
- Modify: `mac-codex-agent/tests/test_codex_adapter.py`

- [ ] **Step 1: Add a state-database fixture**

Create a temporary SQLite database with the fields used in production:

```python
def create_thread_state_db(path, rows):
    connection = sqlite3.connect(path)
    connection.execute(
        "CREATE TABLE threads ("
        "id TEXT PRIMARY KEY, model TEXT, reasoning_effort TEXT)"
    )
    connection.executemany("INSERT INTO threads VALUES (?, ?, ?)", rows)
    connection.commit()
    connection.close()
```

- [ ] **Step 2: Add a failing majority test**

Return four Running alerts from a fake tracker. Map three threads to
`gpt-5.6-sol/xhigh` and one to `gpt-5.6-sol/medium`. Assert collector output is:

```python
self.assertEqual(snapshot["codex_model"], "gpt-5.6-sol")
self.assertEqual(snapshot["codex_effort"], "xhigh")
```

- [ ] **Step 3: Add a failing tie-break test**

Use one Running conversation per pair. Give the medium conversation the later
`progress_at` timestamp and assert the selected pair is
`gpt-5.6-sol/medium`.

- [ ] **Step 4: Add failing persistence and fallback tests**

Assert the winner is written as JSON to the configured cache. Construct a new
collector with no Running alerts and assert it restores the persisted pair.
Also cover a corrupt cache and missing state database: collection must continue
using the existing live/config fallback without overwriting a valid in-memory
selection.

- [ ] **Step 5: Run focused tests and observe failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_adapter.py -v
```

Expected: Running thread metadata and cache paths are not yet supported, so the
collector continues returning the newest global log result.

### Task 2: Implement Running Pair Selection

**Files:**
- Modify: `mac-codex-agent/codex_adapter.py`

- [ ] **Step 1: Add testable paths**

Extend `CodexPaths` with defaults:

```python
state_db: pathlib.Path = pathlib.Path.home() / ".codex" / "state_5.sqlite"
model_selection_cache: pathlib.Path = (
    pathlib.Path.home() / ".codex" / ".stopwatch_model_selection.json"
)
```

- [ ] **Step 2: Implement a pure pair ranker**

Rank `(model, effort, started_at)` candidates as a real pair:

```python
def _majority_running_pair(candidates):
    counts = {}
    latest = {}
    for model, effort, started_at in candidates:
        model = model.strip() if isinstance(model, str) else ""
        effort = effort.strip().lower() if isinstance(effort, str) else ""
        if not model or not effort:
            continue
        pair = (model, effort)
        counts[pair] = counts.get(pair, 0) + 1
        latest[pair] = max(latest.get(pair, ""), started_at)
    if not counts:
        return None
    return max(counts, key=lambda pair: (counts[pair], latest[pair]))
```

This count-first/latest-second ordering implements the confirmed tie rule and
cannot create a synthetic model/effort combination.

- [ ] **Step 3: Join the Running snapshot to the state database**

Collect only `alert.state == "running"`. Query matching IDs with a parameterized
`IN` clause for `id`, `model`, and `reasoning_effort`. Combine each row with the
alert's `progress_at` and pass candidates to the pure ranker. Missing rows or
incomplete pairs do not vote.

- [ ] **Step 4: Load and atomically save the winner**

At collector initialization, read a cache object containing non-empty `model`
and `effort`. On a changed valid winner, write a temporary sibling file and
replace the cache with `os.replace()`. Ignore missing/corrupt cache files.

- [ ] **Step 5: Apply the selection after alert refresh**

In `get_state()`:

```python
status = config_and_live_status()
alerts = tracker.refresh()
winner = self._read_running_model_pair(alerts)
if winner is not None:
    self._remember_model_pair(winner)
if self._remembered_model_pair is not None:
    status["model"], status["effort"] = self._remembered_model_pair
```

With no valid winner, the remembered pair remains unchanged. When no remembered
pair exists, retain the previous live/config result already in `status`.

- [ ] **Step 6: Run tests and commit**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_adapter.py -v
git add mac-codex-agent/codex_adapter.py \
  mac-codex-agent/tests/test_codex_adapter.py
git commit -m "Select majority model from running conversations"
```

### Task 3: Reinstall And Verify The Mac Agent

**Files:**
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Document selection behavior**

Document pair-based voting, latest-Running tie breaking, persisted no-Running
fallback, and the first-install live/config fallback.

- [ ] **Step 2: Run complete checks**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
bash -n mac-codex-agent/install_launch_agent.sh
git diff --check
```

- [ ] **Step 3: Reinstall without enabling autostart**

Stop the running menu process, run `mac-codex-agent/install_launch_agent.sh`,
and manually open `~/Applications/StopWatch Codex Agent.app`. Confirm the
LaunchAgent plist remains absent and BLE reconnects.

- [ ] **Step 4: Verify current Running votes**

Read current Running thread IDs and their state-database rows. Confirm menu and
watch show the winning real pair. When the Running set becomes empty, verify the
same pair remains in `~/.codex/.stopwatch_model_selection.json` and on screen.

- [ ] **Step 5: Commit documentation**

Commit only `mac-codex-agent/README.md` with message
`Document running model majority selection`.
