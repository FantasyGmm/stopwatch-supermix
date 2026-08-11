# Codex Agent Focus Rail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Codex pull-down Agent card stack with the approved Focus Rail while preserving all existing multi-Agent switching, pairing, deletion, reset, and voice-cleanup behavior.

**Architecture:** Keep `AgentPanelModel` and the secure runtime as policy owners. Add one dependency-free layout contract, use an LVGL vertical snap container for the 0–8 Agent rail, keep per-row delete controls hidden behind the row until a left gesture, and restyle existing request/confirmation states rather than adding a second workflow. Gesture ownership changes only enough to reserve the title region for panel closing and the rail region for list scrolling.

**Tech Stack:** ESP-IDF 5.5.4, LVGL 9, C++17, native assertion tests, Python firmware-contract tests, unified offline validation, ESP32-S3 unified build.

---

## File Map

- Create `main/apps/app_codex_status/agent_panel_ui_layout.h`: authoritative Focus Rail palette, geometry, scroll, and timing constants.
- Create `tests/agent_panel_ui_layout_test.cpp`: compile-time round-screen and touch-target bounds.
- Modify `main/apps/app_codex_status/agent_panel_model.h`: store visible-panel header gesture ownership.
- Modify `main/apps/app_codex_status/agent_panel_model.cpp`: close a visible panel only from the title region so rail scrolling cannot close it.
- Modify `tests/agent_panel_model_test.cpp`: lock header-only close and rail gesture isolation.
- Modify `main/apps/app_codex_status/app_codex_status.h`: own rail container, row wrappers, focus state, delete reveal, reset hold, and transient notice state.
- Modify `main/apps/app_codex_status/app_codex_status.cpp`: render the rail and all secondary states; route scroll, gesture, click, and hold events to existing actions.
- Modify `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`: lock the approved structure, copy, fonts, hidden destructive controls, and preserved secure flows.
- Modify `scripts/run_unified_offline_validation.sh`: compile and run the new native layout test.

### Task 1: Lock Focus Rail Geometry

**Files:**
- Create: `main/apps/app_codex_status/agent_panel_ui_layout.h`
- Create: `tests/agent_panel_ui_layout_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`

- [ ] **Step 1: Write the failing native and firmware contracts**

Create a native test that includes the not-yet-existing layout header and checks
the approved values:

```cpp
#include "main/apps/app_codex_status/agent_panel_ui_layout.h"

#include <cassert>

int main()
{
    using namespace agent_panel_ui;
    static_assert(kScreenSize == 466);
    static_assert(kRailViewport.x == 32);
    static_assert(kRailViewport.y == 78);
    static_assert(kRailViewport.w == 402);
    static_assert(kRailViewport.h == 300);
    static_assert(kFocusBand.x == 39);
    static_assert(kFocusBand.y == 195);
    static_assert(kFocusBand.w == 388);
    static_assert(kFocusBand.h == 60);
    static_assert(kRailRowHeight == 58);
    static_assert(kMinimumTouchHeight >= 44);
    static_assert(kPanelHeaderBottomY == 78);
    static_assert(kDeleteRevealWidth == 76);
    static_assert(kResetHoldMs == 1200);
    assert(kResetRect.x >= 0);
    assert(kResetRect.x + kResetRect.w <= kScreenSize);
    assert(kCloseHint.y + kCloseHint.h <= kScreenSize);
}
```

Extend the Python contract with one test that expects the new layout include,
snap container API, approved labels, and no permanently visible delete button:

```python
def test_focus_rail_uses_approved_structure_and_copy(self):
    header = (ROOT / "main/apps/app_codex_status/app_codex_status.h").read_text()
    source = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()
    self.assertIn('#include "agent_panel_ui_layout.h"', header)
    self.assertIn("_agent_rail", header)
    self.assertIn("lv_obj_set_scroll_snap_y", source)
    self.assertIn('"Switch request"', source)
    self.assertIn('"%u PAIRED"', source)
    self.assertIn('"Hold to reset"', source)
    self.assertIn('"PAIRING REQUEST"', source)
    self.assertIn('"Allow + Switch"', source)
    self.assertIn('"No paired Agents"', source)
    self.assertIn("LV_OBJ_FLAG_HIDDEN", source)
```

- [ ] **Step 2: Run the contracts and verify RED**

Run:

```bash
clang++ -std=c++17 -I. tests/agent_panel_ui_layout_test.cpp \
  -o /tmp/agent_panel_ui_layout_test
python3 -m unittest \
  mac-codex-agent.tests.test_agent_panel_firmware_contract
```

Expected: the native compile fails because the layout header is absent and the
Python contract fails because the current renderer is still the card stack.

- [ ] **Step 3: Add the minimal layout contract**

Create `agent_panel_ui_layout.h` with a dependency-free rectangle type and the
exact approved constants:

```cpp
#pragma once

#include <cstdint>

namespace agent_panel_ui {

struct Rect { int x; int y; int w; int h; };

constexpr int kScreenSize = 466;
constexpr uint32_t kBackground = 0x080B10;
constexpr uint32_t kPrimaryText = 0xF2F6F8;
constexpr uint32_t kSecondaryText = 0x788391;
constexpr uint32_t kSelection = 0x49C9EE;
constexpr uint32_t kConnected = 0x53D7A4;
constexpr uint32_t kRequesting = 0xFFB454;
constexpr uint32_t kOffline = 0x667180;
constexpr uint32_t kDestructive = 0xFF6248;
constexpr uint32_t kPairing = 0x7C9DFF;

constexpr Rect kDragHandle{209, 12, 48, 4};
constexpr Rect kRailViewport{32, 78, 402, 300};
constexpr Rect kFocusBand{39, 195, 388, 60};
constexpr Rect kFocusMarker{42, 211, 3, 28};
constexpr Rect kResetRect{139, 390, 188, 44};
constexpr Rect kCloseHint{193, 438, 80, 4};
constexpr int kPanelHeaderBottomY = 78;
constexpr int kRailRowHeight = 58;
constexpr int kMinimumTouchHeight = 44;
constexpr int kDeleteRevealWidth = 76;
constexpr uint32_t kResetHoldMs = 1200;
constexpr uint32_t kNoticeDurationMs = 2000;

}  // namespace agent_panel_ui
```

Register `agent_panel_ui_layout_test` beside `agent_panel_model_test` in the
offline validation script.

- [ ] **Step 4: Run the native layout test**

Run:

```bash
clang++ -std=c++17 -I. tests/agent_panel_ui_layout_test.cpp \
  -o /tmp/agent_panel_ui_layout_test && /tmp/agent_panel_ui_layout_test
```

Expected: exit 0.

- [ ] **Step 5: Commit the geometry contract**

```bash
git add main/apps/app_codex_status/agent_panel_ui_layout.h \
  tests/agent_panel_ui_layout_test.cpp \
  scripts/run_unified_offline_validation.sh \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py
git commit -m "test: lock Agent focus rail contract"
```

### Task 2: Isolate Rail Scrolling From Panel Closing

**Files:**
- Modify: `main/apps/app_codex_status/agent_panel_model.cpp`
- Modify: `tests/agent_panel_model_test.cpp`

- [ ] **Step 1: Write failing gesture tests**

Add native assertions that a visible panel closes only when the gesture starts
inside the title region, while the same upward drag inside the rail remains
open:

```cpp
model.set_visible(true);
model.touch_press(220, 220);
model.touch_move(220, 140);
assert(model.touch_release() == AgentPanelAction::None);
assert(model.snapshot().visible);

model.touch_press(220, 54);
model.touch_move(220, 0);
assert(model.touch_release() == AgentPanelAction::Close);
assert(!model.snapshot().visible);
```

Also assert that `vertical_drag_offset()` is zero for a visible-panel gesture
that begins below `kPanelHeaderBottomY`.

- [ ] **Step 2: Run the native model test and verify RED**

Run the existing host-test compile command from
`scripts/run_unified_offline_validation.sh` for `agent_panel_model_test`.

Expected: the rail-origin gesture currently closes the panel and returns a
nonzero drag offset.

- [ ] **Step 3: Add header ownership to the existing model**

Include `agent_panel_ui_layout.h`, store whether a visible-panel touch started
inside the header, and gate close/drag behavior with it:

```cpp
_panel_drag_active = !_snapshot.visible ||
    y <= agent_panel_ui::kPanelHeaderBottomY;
```

`touch_release()` may emit `Close` only when `_panel_drag_active` is true.
`vertical_drag_offset()` returns zero when it is false. `cancel_touch()` and
`set_visible()` clear the flag.

- [ ] **Step 4: Run the native model test and verify GREEN**

Expected: exit 0 with both existing pull-down behavior and the new rail
isolation assertions passing.

- [ ] **Step 5: Commit gesture isolation**

```bash
git add main/apps/app_codex_status/agent_panel_model.cpp \
  main/apps/app_codex_status/agent_panel_model.h \
  tests/agent_panel_model_test.cpp
git commit -m "feat: isolate Agent rail gestures"
```

### Task 3: Render The Focus Rail

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`

- [ ] **Step 1: Finish the failing firmware contract**

Extend the contract to require:

```python
self.assertIn("_agent_row_wrappers", header)
self.assertIn("agent_panel_scroll_event_cb", header)
self.assertIn("agent_panel_row_gesture_cb", header)
self.assertIn("lv_obj_scroll_to_view", source)
self.assertIn("lv_obj_set_flex_flow", source)
self.assertIn("lv_obj_set_style_opa", source)
self.assertNotIn('make_button("×"', source)
```

Run the single Python module and verify these new assertions fail.

- [ ] **Step 2: Add LVGL rail ownership**

Replace the permanent card/delete layout with these stored objects:

```cpp
lv_obj_t* _agent_rail = nullptr;
lv_obj_t* _agent_focus_band_left = nullptr;
lv_obj_t* _agent_focus_band_right = nullptr;
std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_wrappers = {};
std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_row_buttons = {};
std::array<lv_obj_t*, secure_pairing::kMaxAgents> _agent_delete_buttons = {};
int _agent_revealed_delete_index = -1;
```

The rail is a vertical flex container in `kRailViewport`, with vertical scroll
snap set to center and enough transparent top/bottom padding for the first and
last records to reach the focus center. Each wrapper is 402 × 58. Its delete
button sits behind the main row, hidden until a left gesture moves the row to
`x=-76`.

- [ ] **Step 3: Apply focus styling from the scroll position**

Add `sync_agent_rail_focus()` and call it on `LV_EVENT_SCROLL` and after layout.
For each visible row, compare its center with the viewport center:

```cpp
const int distance = std::abs(row_center - viewport_center);
if (distance <= 18) {
    font = &lv_font_montserrat_22;
    opacity = LV_OPA_COVER;
} else if (distance <= kRailRowHeight + 18) {
    font = &lv_font_montserrat_18;
    opacity = 184;
} else {
    font = &lv_font_montserrat_16;
    opacity = 96;
}
```

Use a mint filled status dot for Connected, amber filled for Switch request,
and an outlined gray dot for Offline. Keep the state label right aligned and
the name in one-line ellipsis mode.

- [ ] **Step 4: Route row selection and delete reveal**

Keep `LV_EVENT_CLICKED` mapped to the existing `SwitchAgent` decision. Before
queuing it, call `lv_obj_scroll_to_view(wrapper, LV_ANIM_ON)`. On a left
`LV_EVENT_GESTURE`, cancel any earlier reveal and move only that row button to
`x=-76`; unhide its delete action. Any scroll event, panel close, or different
row click restores all rows to `x=0` and hides every delete action.

Delete clicks continue to dispatch `Delete`, so the existing
`remove_and_rotate` and confirmation path remains untouched.

- [ ] **Step 5: Run firmware contracts and compile the host route**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_agent_panel_firmware_contract
scripts/run_unified_offline_validation.sh
```

Expected: the Python module passes and the offline route exits 0.

- [ ] **Step 6: Commit the rail renderer**

```bash
git add main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py
git commit -m "feat: render Codex Agent focus rail"
```

### Task 4: Restyle Pairing, Confirmation, Empty, Busy, And Error States

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`

- [ ] **Step 1: Add failing state-copy and interaction contracts**

Require the source to contain the exact copy and timing constants:

```python
for label in (
    "PAIRING REQUEST", "Allow + Switch", "Allow", "Reject",
    "Delete Agent?", "Reset all Agents?", "Hold to reset",
    "No paired Agents", "Switching...", "Couldn't switch",
    "Couldn't delete", "Couldn't reset",
):
    self.assertIn(f'"{label}"', source)
self.assertIn("deadline_ms()", source)
self.assertIn("kResetHoldMs", source)
self.assertIn("_agent_reset_hold_started_ms", header)
self.assertIn("_agent_panel_notice_until_ms", header)
```

Run the focused module and verify RED on missing copy and state.

- [ ] **Step 2: Restyle pairing and confirmations**

Use the exact rectangles from the design spec. Format the verification code as
`NNN NNN`, render seconds remaining from
`SecurePairingSession::deadline_ms()`, and leave Allow, Allow + Switch, and
Reject wired to their existing decisions.

Delete confirmation remains a normal Confirm/Cancel state. Reset confirmation
uses `LV_EVENT_PRESSED`, `LV_EVENT_RELEASED`, and `LV_EVENT_PRESS_LOST` to track
an uninterrupted hold. In `onRunning()`, once the hold reaches
`kResetHoldMs`, queue `confirm_destructive(true)` exactly once.

- [ ] **Step 3: Add truthful empty, busy, and transient error presentation**

Render the specified empty state when `snapshot.count == 0`. While
`_agent_switch_pending` is true, show `Switching...` on the target row and ignore
additional switch clicks. Add a centered notice label with a 2 s deadline;
dispatch failures set one of the exact error messages without changing row
truth or registry data.

- [ ] **Step 4: Run focused and full host verification**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_agent_panel_firmware_contract
scripts/run_unified_offline_validation.sh
```

Expected: both commands exit 0.

- [ ] **Step 5: Commit secondary states**

```bash
git add main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py
git commit -m "feat: complete Agent focus rail states"
```

### Task 5: Build And Visual Fidelity Verification

**Files:**
- Verify: `docs/superpowers/specs/2026-08-11-codex-agent-focus-rail-design.md`
- Verify: `main/apps/app_codex_status/agent_panel_ui_layout.h`
- Verify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] **Step 1: Run the complete Python contract suite in the project Agent environment**

Use the Python environment that supplies Pillow, cryptography, and macOS
ApplicationServices; do not use bare system Python when those dependencies are
absent.

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: no failures or errors.

- [ ] **Step 2: Run the official offline validation route**

Source the configured ESP-IDF environment path first, then run:

```bash
scripts/run_unified_offline_validation.sh
```

Expected: every Python and native stage exits 0.

- [ ] **Step 3: Build a fresh unified candidate**

```bash
source /Users/luke/esp/esp-idf/export.sh
STOPWATCH_BUILD_DIR="$PWD/build-agent-focus-rail" \
  IDF_PATH=/Users/luke/esp/esp-idf \
  scripts/build_unified_candidate.sh
```

Expected: ESP32-S3 image generation and app/assets capacity gates pass.

- [ ] **Step 4: Perform the 466 × 466 visual comparison**

Open the Agent panel with 0, 1, 5, and 8 stored records and capture default,
requesting-switch, pairing-code, delete-confirmation, reset-confirmation, and
switching states. Compare geometry, font scale, opacity, status colors, touch
targets, and round-screen clipping against the approved spec. If automated
watch capture remains unavailable, obtain user physical-screen acceptance and
report that the comparison was manual rather than pixel-diff based.

- [ ] **Step 5: Final repository checks**

```bash
git diff --check
git status --short --branch
git log -6 --oneline
```

Expected: no tracked changes remain after the implementation commit; only
ignored or intentionally untracked build/brainstorm artifacts may remain.
