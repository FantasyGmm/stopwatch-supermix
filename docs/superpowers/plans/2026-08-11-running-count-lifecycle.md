# Running Count Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep active Codex Running tasks counted across local midnight while retaining today's-only Completed history.

**Architecture:** Change only the alert snapshot's date predicate. Prove the lifecycle behavior through the real `TaskAlertTracker` and rollout event stream.

**Tech Stack:** Python 3, `unittest`, JSONL Codex rollout fixtures.

---

### Task 1: Preserve Running across midnight

**Files:**
- Modify: `mac-codex-agent/task_alerts.py:906-920`
- Test: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Write the failing regression test**

Add a test that starts a turn before local midnight, advances the injected clock past midnight, and asserts `running_count == 1`.

- [ ] **Step 2: Verify RED**

Run: `python3 -m unittest mac-codex-agent/tests/test_task_alerts.py -k running_survives_midnight`

Expected: FAIL because the snapshot filters yesterday's Running record.

- [ ] **Step 3: Implement the minimal predicate change**

Change the date condition so only `completed` is excluded when it is not from today:

```python
if state == "completed" and not is_today:
    continue
```

- [ ] **Step 4: Verify GREEN and adjacent lifecycle behavior**

Run the focused test, then the complete task-alert suite. Confirm Pending survives midnight, Completed does not, and abort/completion retire Running normally.

- [ ] **Step 5: Verify repository quality gates**

Run Python compilation and `git diff --check`, then inspect the final diff for unrelated changes.

## Self-review

The plan covers every design requirement with one production-line change and one regression test. It introduces no new APIs, types, or placeholders.
