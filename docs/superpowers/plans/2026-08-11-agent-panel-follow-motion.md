# Agent Panel Follow Motion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Agent panel follow vertical drags and settle smoothly without rebuilding its contents during open or close transitions.

**Architecture:** `AgentPanelModel` exposes only a dead-zone-adjusted vertical drag offset. `AppCodexStatus` maps that offset to clamped LVGL Y coordinates and owns a single interruptible ease-out animation; content rendering remains separate.

**Tech Stack:** C++17, LVGL 9 animation API, native `assert` host tests, Python `unittest` firmware source contracts.

---

### Task 1: Add a UI-independent drag offset

**Files:**
- Modify: `main/apps/app_codex_status/agent_panel_model.h`
- Modify: `main/apps/app_codex_status/agent_panel_model.cpp`
- Test: `tests/agent_panel_model_test.cpp`

- [x] **Step 1: Write the failing native tests**

Add assertions after the existing gesture tests:

```cpp
model.set_visible(false);
model.touch_press(220, 30);
model.touch_move(260, 35);
assert(model.vertical_drag_offset() == 0);
model.touch_move(220, 35);
assert(model.vertical_drag_offset() == 0);
model.touch_move(220, 56);
assert(model.vertical_drag_offset() == 20);
model.cancel_touch();

model.set_visible(true);
model.touch_press(220, 250);
model.touch_move(220, 190);
assert(model.vertical_drag_offset() == -54);
model.cancel_touch();
```

- [x] **Step 2: Run the focused native test and verify RED**

Run the `agent_panel_model_test` command from `scripts/run_unified_offline_validation.sh`.

Expected: compilation fails because `vertical_drag_offset()` does not exist.

- [x] **Step 3: Implement the minimal model method**

Declare `int vertical_drag_offset() const;` and implement:

```cpp
int AgentPanelModel::vertical_drag_offset() const
{
    constexpr int kDragDeadZone = 6;
    if (!_touch_active) {
        return 0;
    }
    const int dx = _touch_x - _touch_start_x;
    const int dy = _touch_y - _touch_start_y;
    if (std::abs(dy) <= std::abs(dx) ||
        std::abs(dy) <= kDragDeadZone) {
        return 0;
    }
    return dy > 0 ? dy - kDragDeadZone : dy + kDragDeadZone;
}
```

- [x] **Step 4: Run the focused native test and verify GREEN**

Expected: `agent_panel_model_test` exits successfully.

### Task 2: Add interruptible LVGL follow and settle motion

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Test: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`

- [x] **Step 1: Write failing firmware contracts**

Add a test that extracts `destroy_view()`, `render_agent_panel()`, `update_agent_panel_touch()`, and the new settle helper, then asserts:

```python
self.assertIn("lv_anim_set_duration(&animation, 180)", settle)
self.assertIn("lv_anim_path_ease_out", settle)
self.assertIn("agent_panel_animation_cb", settle)
self.assertIn("lv_anim_delete(this, agent_panel_animation_cb)", destroy)
self.assertIn("_agent_panel_model.vertical_drag_offset()", touch)
self.assertIn("std::clamp", touch)
self.assertIn("settle_agent_panel(true)", touch)
self.assertNotIn("render_agent_panel();", touch)
```

- [x] **Step 2: Run the contract and verify RED**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py \
  -k panel_follows_drag_and_settles_without_rebuild
```

Expected: FAIL because the settle helper and callback do not exist.

- [x] **Step 3: Implement panel position ownership**

Add `_agent_panel_drag_start_y`, `settle_agent_panel(bool)`, and `agent_panel_animation_cb`. The helper deletes any previous panel animation, synchronizes global edge-control enablement, and either sets the endpoint immediately or starts a 180 ms ease-out animation from the current Y coordinate.

On press, delete the current animation and capture `lv_obj_get_y(_agent_panel_root)`. On move, set Y to:

```cpp
std::clamp(
    _agent_panel_drag_start_y +
        _agent_panel_model.vertical_drag_offset(),
    kAgentPanelHiddenY,
    kAgentPanelVisibleY)
```

On release or input cancellation, call `settle_agent_panel(true)` instead of `render_agent_panel()`. At the end of `render_agent_panel()`, call the same settle helper so automatic pairing visibility uses the motion path. Delete the panel animation in `destroy_view()`.

- [x] **Step 4: Run focused GREEN tests**

Run the firmware contract and native `agent_panel_model_test`.

Expected: both pass.

- [x] **Step 5: Run adjacent and full validation**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py \
  mac-codex-agent/tests/test_edge_control_firmware_contract.py
IDF_PATH=/Users/luke/esp/esp-idf \
  scripts/run_unified_offline_validation.sh --host-tests-only
git diff --check
```

Expected: all tests and checks pass.

- [x] **Step 6: Build, commit, flash, and smoke-test**

Build `build-unified-latest`, commit only the plan, source, and tests, flash the exact ESP32-S3 app image to the uniquely detected watch port without erasing NVS, and confirm the committed version reaches Launcher without panic or a reboot loop.

## Self-review

The plan covers every design requirement: direct manipulation, dead zone, clamping, interruption, ease-out settling, content/motion separation, automatic visibility, lifecycle cleanup, regression tests, and hardware smoke. Method names and types are consistent and there are no placeholders.
