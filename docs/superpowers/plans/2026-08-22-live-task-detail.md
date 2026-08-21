# Live Task Detail Implementation Plan

**Goal:** Add the Agent-style drag handle to Codex task details and stream the
latest complete Agent progress message through the existing secure detail path.

**Architecture:** Extend the existing rollout reducer for active-turn
`agent_message` events; reuse the existing revision, fingerprint, encryption,
route, and detail-store pipeline. Add only state-aware visual elements to the
existing task-focus view.

### Task 1: Lock the behavior with focused failing tests

**Files:**
- Modify: `mac-codex-agent/tests/test_task_alerts.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: the existing task-focus firmware contract/host test

- Add running-message, stale-turn, empty-message, changed-detail, no-resend,
  handle geometry, and state-aware label assertions.
- Run only those focused tests and confirm they fail for the missing behavior.

### Task 2: Implement the smallest cross-layer change

**Files:**
- Modify: `mac-codex-agent/task_alerts.py`
- Modify: `main/apps/app_codex_status/codex_task_focus_view.h`
- Modify: `main/apps/app_codex_status/codex_task_focus_view.cpp`
- Modify: `main/apps/app_codex_status/codex_task_focus_layout.h`

- Reduce complete active-turn `agent_message` records into the running alert's
  `last_reply` and revision.
- Add the Agent-style handle and running-only live label/dot to the detail view.
- Keep the existing secure transport and final-reply path unchanged.

### Task 3: Focused verification and delivery

- Run the focused Python reducer/Agent suites.
- Run the task-focus host/contract tests and `git diff --check`.
- Compare the implemented constants and states against browser option A.
- Commit only the task files and these two design artifacts; leave
  `.superpowers/` untouched.
- Treat the watch UI as compiled until a later flash and physical AMOLED check.
