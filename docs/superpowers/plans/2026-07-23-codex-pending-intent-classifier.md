# Codex Pending Intent Classifier Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Classify completed Codex turns that still require user action as Pending by using structured rollout evidence first and general response structure second.

**Architecture:** Add a focused completion-classifier module that returns an outcome and an explainable reason. Extend `TaskAlertTracker` to persist per-turn plan/input signals from `response_item` records and consume them at `task_complete`; keep the BLE payload and firmware unchanged.

**Tech Stack:** Python 3.9, dataclasses, JSONL rollout events, unittest.

---

### Task 1: Completion Classifier

**Files:**
- Create: `mac-codex-agent/completion_classifier.py`
- Test: `mac-codex-agent/tests/test_completion_classifier.py`

- [ ] **Step 1: Write failing classification tests**

Cover structured unfinished plans, unresolved input, a question followed by
guidance, A/B/C options, URL query strings, fenced code, historical questions,
and ordinary completion:

```python
def test_structured_plan_has_priority():
    result = classify_completion("Artifacts are ready.", unfinished_plan=True)
    self.assertEqual((result.outcome, result.reason),
                     ("waiting", "structured_unfinished_plan"))

def test_choice_group_requires_user_action():
    message = "- A: balanced\\n- B: compact\\n- C: prominent\\n\\nChoose A, B, or C."
    self.assertEqual(classify_completion(message).reason, "text_choice")

def test_url_question_mark_is_not_a_question():
    result = classify_completion("Opened https://example.test/?view=done. Work is complete.")
    self.assertEqual(result.outcome, "completed")
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
./mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_completion_classifier.py
```

Expected: import failure because `completion_classifier.py` does not exist.

- [ ] **Step 3: Implement the minimal classifier**

Define:

```python
@dataclasses.dataclass(frozen=True)
class CompletionClassification:
    outcome: str
    reason: str

def classify_completion(
    message: Any,
    *,
    unfinished_plan: bool = False,
    unresolved_input: bool = False,
) -> CompletionClassification:
    ...
```

Priority must be unresolved input, unfinished plan, final prose question,
choice-group handoff, required-action fallback, then completion.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the command from Step 2. Expected: all classifier tests pass.

- [ ] **Step 5: Commit**

```bash
git add mac-codex-agent/completion_classifier.py \
  mac-codex-agent/tests/test_completion_classifier.py
git commit -m "Add structured Codex completion classifier"
```

### Task 2: Persist Turn Evidence

**Files:**
- Modify: `mac-codex-agent/task_alerts.py`
- Modify: `mac-codex-agent/tests/test_task_alerts.py`

- [ ] **Step 1: Write failing tracker tests**

Add a helper that appends `response_item` function calls with turn IDs. Test:

```python
append_event(self.rollout, "task_started", "...", turn_id="turn-a")
append_plan(self.rollout, "turn-a", [
    {"step": "Show designs", "status": "completed"},
    {"step": "Wait for selection", "status": "in_progress"},
])
append_event(self.rollout, "task_complete", "...",
             last_agent_message="A/B/C designs are ready.",
             turn_id="turn-a")
```

Assert Pending with ledger reason `structured_unfinished_plan`. Also verify a
later all-completed plan produces Completed, turn evidence survives restart,
resolved input calls do not remain Pending, and schema versions 1/2 migrate to
version 3 with `turn_signals`.

- [ ] **Step 2: Run tracker tests and verify RED**

```bash
./mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_task_alerts.py
```

Expected: new structured-signal assertions fail.

- [ ] **Step 3: Extend the tracker**

Set `LEDGER_VERSION = 3`, persist `turn_signals`, process `response_item`
records, and call:

```python
classification = classify_completion(
    payload.get("last_agent_message"),
    unfinished_plan=signals.get("unfinished_plan", False),
    unresolved_input=bool(signals.get("input_calls")),
)
```

Store `classification.reason` on the alert and log the outcome/reason. Remove
turn evidence after `task_complete`; replace stale evidence on `task_started`.

- [ ] **Step 4: Run tracker and classifier tests**

Run both focused test modules. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add mac-codex-agent/task_alerts.py \
  mac-codex-agent/tests/test_task_alerts.py
git commit -m "Use Codex turn signals for pending alerts"
```

### Task 3: Package and Runtime Verification

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Add a failing packaging contract**

Assert the installer copies `completion_classifier.py` into the application
bundle.

- [ ] **Step 2: Run the packaging test and verify RED**

```bash
./mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_install_script.py
```

Expected: missing module assertion fails.

- [ ] **Step 3: Package and document the classifier**

Add `completion_classifier.py` to the installer module list. Update the Task
Alerts documentation to state that structured plan/input evidence is primary,
text structure is fallback, and classification reasons remain local.

- [ ] **Step 4: Run all Mac-agent tests**

```bash
./mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 5: Install and verify**

Quit the current menu app, run:

```bash
./mac-codex-agent/install_launch_agent.sh
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Verify code-signing, installed/source hashes, Agent process, live ledger schema,
classification log reason, and Bluetooth state.

- [ ] **Step 6: Commit**

```bash
git add mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py \
  mac-codex-agent/README.md
git commit -m "Package structured pending classifier"
```
