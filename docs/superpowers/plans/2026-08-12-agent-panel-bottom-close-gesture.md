# Agent Panel Bottom Close Gesture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore a finger-following bottom-up close gesture while keeping Agent list scrolling inside the focus rail.

**Architecture:** Keep gesture ownership in `AgentPanelModel` and reuse the existing LVGL follow/settle path. A press in the rail viewport remains rail-owned; a press in the header or bottom action area may drag the panel.

**Tech Stack:** C++17, LVGL 9, native `assert` host tests, Python `unittest` firmware contracts.

---

### Task 1: Lock the intended gesture regions with native tests

**Files:**
- Modify: `tests/agent_panel_model_test.cpp`

- [x] **Step 1: Add the failing bottom-close assertions**

After the existing rail-origin no-op assertions, set the model visible, press
at `y = 440`, move to `y = 360`, assert a `-74` follow offset, release, and
assert `AgentPanelAction::Close` with `visible == false`.

- [x] **Step 2: Add the settle-back guard**

Set the model visible, press at `y = 440`, move to `y = 420`, assert a `-14`
follow offset, release, and assert `AgentPanelAction::None` with the panel still
visible.

- [x] **Step 3: Run the focused native test and verify RED**

Run only `agent_panel_model_test` through the repository's unified host runner.
Expected: the first follow-offset assertion fails because footer presses do
not currently activate panel dragging.

### Task 2: Restore footer-origin panel dragging

**Files:**
- Modify: `main/apps/app_codex_status/agent_panel_model.cpp`
- Test: `tests/agent_panel_model_test.cpp`

- [x] **Step 1: Implement the minimal region predicate**

Compute the rail bottom from `kRailViewport.y + kRailViewport.h`. When the
panel is visible, set `_panel_drag_active` for presses at or above
`kPanelHeaderBottomY` or at or below the rail bottom. Keep hidden-panel pull
down behavior unchanged.

- [x] **Step 2: Run the focused native test and verify GREEN**

Run `agent_panel_model_test`. Expected: rail-origin, header-origin,
footer-origin, short-drag, vertical-dominance, and horizontal-dominance
assertions all pass.

### Task 3: Verify adjacent firmware behavior

**Files:**
- Verify: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`
- Verify: `main/apps/app_codex_status/app_codex_status.cpp`

- [x] **Step 1: Run Agent firmware contracts**

Run `test_agent_panel_firmware_contract.py`. Expected: the focus rail remains
vertically scrollable and the panel follow/settle animation contract passes.

- [x] **Step 2: Run the unified host suite**

Run `scripts/run_unified_offline_validation.sh --host-tests-only`. Expected:
all host tests pass without warnings.

- [x] **Step 3: Run source hygiene checks**

Run `git diff --check` and inspect `git status --short`. Expected: no whitespace
errors and no unrelated tracked files changed.

## Self-review

Every interaction in the design maps to a concrete native assertion. The
implementation changes one ownership predicate and reuses the already-tested
animation path, avoiding a second gesture state machine. Names, coordinates,
and expected offsets match the current layout and six-pixel dead zone.
