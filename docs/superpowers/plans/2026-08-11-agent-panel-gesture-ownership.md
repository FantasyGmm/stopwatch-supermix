# Agent Panel Gesture Ownership Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow the visible Agent panel to close by upward swipe without global brightness or volume controls stealing the gesture.

**Architecture:** Reuse `GlobalEdgeControlService::setEnabled()` as the ownership boundary. Rendering a visible panel disables global edge controls; rendering a hidden panel and closing the app restore them.

**Tech Stack:** C++17, LVGL, Python `unittest` firmware source contracts, native host tests.

---

### Task 1: Lock gesture ownership to the visible panel

**Files:**
- Modify: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

- [x] **Step 1: Write the failing source-contract test**

Add a test that extracts `render_agent_panel()` and `onClose()` and asserts:

```python
self.assertIn(
    "GetGlobalEdgeControlService().setEnabled(!snapshot.visible)",
    render,
)
self.assertIn("GetGlobalEdgeControlService().setEnabled(true)", close)
```

- [x] **Step 2: Run the contract test and verify RED**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py \
  -k panel_exclusively_owns_vertical_gestures_while_visible
```

Expected: FAIL because neither lifecycle synchronization call exists.

- [x] **Step 3: Implement the minimal lifecycle synchronization**

In `render_agent_panel()`, after reading the snapshot, add:

```cpp
GetGlobalEdgeControlService().setEnabled(!snapshot.visible);
```

Inside the existing `LvglLockGuard` scope in `onClose()`, add:

```cpp
GetGlobalEdgeControlService().setEnabled(true);
```

- [x] **Step 4: Run focused GREEN tests**

Run the new Python contract and the native `agent_panel_model_test`. Confirm both pass.

- [x] **Step 5: Run adjacent regression tests**

Run `test_agent_panel_firmware_contract.py`, `test_edge_control_firmware_contract.py`, `test_unified_voice_validation.py`, the native `edge_control_logic_test`, and the unified offline validation available in this worktree.

- [x] **Step 6: Verify and commit**

Run `git diff --check`, inspect the final diff, and commit only the source, test, design, and plan files.

## Self-review

The plan covers visible, hidden, and app-close ownership transitions. Function names and test seams match the existing code, and no placeholder or unrelated refactor remains.
