# Codex Pending Soft Glow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the approved A1 red soft-breathing edge glow to the latest Codex status page only while its highest-priority display state is Pending.

**Architecture:** Keep the current `d8fdb8c` UI geometry immutable. Add a pure transition helper, then a Codex-specific LVGL component that owns three fixed concentric rings and one opacity animation; `AppCodexStatus` only forwards view lifecycle, screen-off state, the existing heading label, and the carousel display state.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.5.4, native `assert` tests, Python `unittest` source-contract tests.

---

### Task 1: Lock Pending-only and idempotent transition behavior

**Files:**
- Create: `main/apps/app_codex_status/codex_pending_glow_logic.h`
- Create: `tests/codex_pending_glow_logic_test.cpp`

- [ ] **Step 1: Write the failing native test**

Create `tests/codex_pending_glow_logic_test.cpp`:

```cpp
#include "apps/app_codex_status/codex_pending_glow_logic.h"

#include <cassert>

using codex_pending_glow::Transition;

int main()
{
    using codex_pending_glow::desired_active;
    using codex_pending_glow::transition_for;

    assert(desired_active(CodexTaskState::Pending, false));
    assert(!desired_active(CodexTaskState::Pending, true));
    assert(!desired_active(CodexTaskState::Running, false));
    assert(!desired_active(CodexTaskState::Completed, false));

    assert(transition_for(false, false) == Transition::None);
    assert(transition_for(false, true) == Transition::Start);
    assert(transition_for(true, true) == Transition::None);
    assert(transition_for(true, false) == Transition::Stop);
    return 0;
}
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test
```

Expected: compilation fails because `codex_pending_glow_logic.h` does not exist.

- [ ] **Step 3: Add the minimal pure transition helper**

Create `main/apps/app_codex_status/codex_pending_glow_logic.h`:

```cpp
#pragma once

#include "codex_status_model.h"

namespace codex_pending_glow {

enum class Transition {
    None,
    Start,
    Stop,
};

constexpr bool desired_active(CodexTaskState state, bool screen_off)
{
    return state == CodexTaskState::Pending && !screen_off;
}

constexpr Transition transition_for(bool active, bool requested_active)
{
    if (active == requested_active) {
        return Transition::None;
    }
    return requested_active ? Transition::Start : Transition::Stop;
}

}  // namespace codex_pending_glow
```

- [ ] **Step 4: Run the native test and verify GREEN**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test && \
  /tmp/codex_pending_glow_logic_test
```

Expected: exit code 0 with no output.

### Task 2: Build the three-ring LVGL A1 component

**Files:**
- Create: `main/apps/app_codex_status/codex_pending_soft_glow.h`
- Create: `main/apps/app_codex_status/codex_pending_soft_glow.cpp`
- Create: `mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py`

- [ ] **Step 1: Write the failing firmware source contract**

Create `mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py`:

```python
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CodexPendingGlowFirmwareContractTests(unittest.TestCase):
    def test_soft_glow_component_uses_three_fixed_rings_and_one_opacity_animation(self):
        header = (ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.h").read_text()
        source = (ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp").read_text()

        self.assertIn("std::array<lv_obj_t*, 3> _rings", header)
        self.assertIn("void set_heading(lv_obj_t* label)", header)
        self.assertIn("void set_active(bool active)", header)
        self.assertIn("void shutdown()", header)
        self.assertIn("_rings[0] = make_ring(parent, 1, 1, 464, 2)", source)
        self.assertIn("_rings[1] = make_ring(parent, 5, 5, 456, 4)", source)
        self.assertIn("_rings[2] = make_ring(parent, 11, 11, 444, 6)", source)
        self.assertIn("lv_anim_set_duration(&animation, 900)", source)
        self.assertIn("lv_anim_set_playback_duration(&animation, 900)", source)
        self.assertIn("lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE)", source)
        self.assertIn("lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out)", source)
        self.assertIn("lv_obj_set_style_border_opa", source)
        phase_body = source.split("void CodexPendingSoftGlow::apply_phase", 1)[1]
        phase_body = phase_body.split("void CodexPendingSoftGlow::start", 1)[0]
        self.assertNotIn("lv_obj_set_size", phase_body)
        self.assertNotIn("lv_obj_set_pos", phase_body)

    def test_component_stops_animation_and_restores_heading_on_deactivation(self):
        source = (ROOT / "main/apps/app_codex_status/codex_pending_soft_glow.cpp").read_text()

        self.assertIn("lv_anim_delete(this, animation_cb)", source)
        self.assertIn("lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN)", source)
        self.assertIn("lv_obj_set_style_opa(_heading, LV_OPA_COVER, 0)", source)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the contract and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py
```

Expected: errors because the component files do not exist.

- [ ] **Step 3: Add the component interface**

Create `main/apps/app_codex_status/codex_pending_soft_glow.h`:

```cpp
#pragma once

#include <array>
#include <lvgl.h>

class CodexPendingSoftGlow {
public:
    void init(lv_obj_t* parent);
    void set_heading(lv_obj_t* label);
    void set_active(bool active);
    void shutdown();
    bool active() const;

private:
    std::array<lv_obj_t*, 3> _rings{};
    lv_obj_t* _heading = nullptr;
    bool _active = false;

    void apply_phase(int32_t phase);
    void start();
    void stop();
    static void animation_cb(void* target, int32_t phase);
};
```

- [ ] **Step 4: Implement fixed geometry and opacity-only animation**

Create `main/apps/app_codex_status/codex_pending_soft_glow.cpp` with a private file-local `make_ring(parent, x, y, size, border_width)` helper. It returns a circular, non-clickable, non-scrollable LVGL object with transparent background, `kAlertVermilion` border, and hidden initial state. Initialize the three fixed rings with:

```cpp
_rings[0] = make_ring(parent, 1, 1, 464, 2);
_rings[1] = make_ring(parent, 5, 5, 456, 4);
_rings[2] = make_ring(parent, 11, 11, 444, 6);
```

Use this animation setup:

```cpp
lv_anim_t animation;
lv_anim_init(&animation);
lv_anim_set_var(&animation, this);
lv_anim_set_exec_cb(&animation, animation_cb);
lv_anim_set_values(&animation, 0, 255);
lv_anim_set_duration(&animation, 900);
lv_anim_set_playback_duration(&animation, 900);
lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
lv_anim_start(&animation);
```

Map phase `0..255` to these opacity ranges in `apply_phase(...)`:

```cpp
const lv_opa_t core = static_cast<lv_opa_t>(LV_OPA_40 + phase * (LV_OPA_COVER - LV_OPA_40) / 255);
const lv_opa_t middle = static_cast<lv_opa_t>(LV_OPA_20 + phase * (LV_OPA_60 - LV_OPA_20) / 255);
const lv_opa_t inner = static_cast<lv_opa_t>(LV_OPA_10 + phase * (LV_OPA_30 - LV_OPA_10) / 255);
const lv_opa_t heading = static_cast<lv_opa_t>(LV_OPA_70 + phase * (LV_OPA_COVER - LV_OPA_70) / 255);
```

`set_active(...)` must use `codex_pending_glow::transition_for(...)`; `start()` unhides all rings and starts one animation, while `stop()` calls `lv_anim_delete(this, animation_cb)`, hides every ring with `lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN)`, and restores heading opacity with `lv_obj_set_style_opa(_heading, LV_OPA_COVER, 0)`. `shutdown()` calls `stop()`, deletes each non-null ring, nulls all references, and leaves `_active` false.

- [ ] **Step 5: Run component contract and native logic tests**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test && \
  /tmp/codex_pending_glow_logic_test
```

Expected: Python reports `Ran 2 tests ... OK`; native test exits 0.

### Task 3: Integrate the effect without changing the latest layout

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py`
- Verify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Verify: `tests/codex_status_ui_layout_test.cpp`

- [ ] **Step 1: Extend the source contract for layer order and lifecycle**

Add a test that reads `app_codex_status.cpp`, `app_codex_status.h`, and `codex_status_ui_layout.h`, then asserts:

```python
self.assertIn("std::unique_ptr<CodexPendingSoftGlow> _pending_glow", header)
self.assertIn("void sync_pending_glow()", header)
self.assertLess(app.index("_pending_glow->init(_panel)"), app.index("lv_image_create(_panel)"))
self.assertLess(app.index("_pending_glow->set_heading(_activity_title)"), app.index("_edge_controls->init(_panel)"))
self.assertIn("codex_pending_glow::desired_active(", app)
self.assertIn("_alert_carousel.display_state()", app)
self.assertIn("_pending_glow->set_active(active)", app)
self.assertIn("_pending_glow->shutdown()", app)
self.assertIn("constexpr uint32_t kInstrumentBlack = 0x000000", layout)
self.assertIn("constexpr int kPetX = 73", layout)
self.assertIn("constexpr int kPetY = 88", layout)
self.assertIn("constexpr int kPetScale = 293", layout)
self.assertIn("constexpr Rect kBeaconSpine{194, 96, 2, 104}", layout)
self.assertNotIn("kHostY", layout)
```

- [ ] **Step 2: Run the contract and verify RED**

Run the Task 2 Python command.

Expected: the new integration test fails because `AppCodexStatus` does not own or call the effect.

- [ ] **Step 3: Own and create the effect below current content**

Include `codex_pending_soft_glow.h` in `app_codex_status.h`, add:

```cpp
std::unique_ptr<CodexPendingSoftGlow> _pending_glow;
void sync_pending_glow();
```

In `create_view()`, immediately after setting the panel's black background and before creating the logo, add:

```cpp
_pending_glow = std::make_unique<CodexPendingSoftGlow>();
_pending_glow->init(_panel);
```

After `_activity_title` is created and configured, add:

```cpp
_pending_glow->set_heading(_activity_title);
```

Because all existing content and `EdgeControlOverlay` are created afterward, they remain above the glow without changing any current coordinates.

- [ ] **Step 4: Gate the effect from the existing priority state**

Add:

```cpp
void AppCodexStatus::sync_pending_glow()
{
    if (_pending_glow == nullptr) {
        return;
    }
    const bool active = codex_pending_glow::desired_active(
        _alert_carousel.display_state(),
        _screen_off
    );
    _pending_glow->set_active(active);
}
```

Call `sync_pending_glow()` at the end of both branches of `render_alert()`, after setting text and colors. `render_alert()` is already called with the LVGL lock held.

In `enter_screen_off()`, after setting `_screen_off = true`, synchronize under its own short lock because this path currently runs outside the LVGL critical section:

```cpp
{
    LvglLockGuard lock;
    sync_pending_glow();
}
```

Do the same in `wake_screen()` immediately after setting `_screen_off = false`. Do not acquire a lock inside `sync_pending_glow()` or the glow component; this avoids recursively locking when `render_alert()` calls it.

- [ ] **Step 5: Stop the effect before deleting the page**

At the start of `destroy_view()`, before shutting down edge controls or deleting `_panel`, add:

```cpp
if (_pending_glow) {
    _pending_glow->shutdown();
    _pending_glow.reset();
}
```

- [ ] **Step 6: Run focused tests and verify GREEN**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py \
  mac-codex-agent/tests/test_firmware_font_contract.py

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test && \
  /tmp/codex_pending_glow_logic_test

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test
```

Expected: both Python suites report `OK`; both native binaries exit 0.

### Task 4: Build, inspect, and prepare hardware validation

**Files:**
- Verify all files changed in Tasks 1-3
- Do not stage unrelated working-tree changes

- [ ] **Step 1: Run the broader Mac Agent suite**

Run:

```bash
PYTHONPATH=mac-codex-agent mac-codex-agent/.venv/bin/python -m unittest \
  discover -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 2: Build the unified firmware and capacity gate**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
./scripts/build_unified_candidate.sh
```

Expected: exit code 0, `capacity_gate_passed` is `true`, and the app image remains below the configured app partition size.

- [ ] **Step 3: Review only feature files**

Run:

```bash
git diff --check -- \
  main/apps/app_codex_status/codex_pending_glow_logic.h \
  main/apps/app_codex_status/codex_pending_soft_glow.h \
  main/apps/app_codex_status/codex_pending_soft_glow.cpp \
  main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_pending_glow_logic_test.cpp \
  mac-codex-agent/tests/test_codex_pending_glow_firmware_contract.py
```

Expected: no whitespace errors. Confirm the diff does not change any current layout constant.

- [ ] **Step 4: Commit the isolated implementation**

Stage only the seven feature files listed in Step 3 and commit:

```bash
git commit -m "Add Codex pending soft glow"
```

- [ ] **Step 5: Flash and inspect when the device is connected**

Discover the current `/dev/cu.usbmodem*` port, flash the verified build with the repository's unified flash workflow, and confirm on-device:

- Pending shows a smooth red A1 breathing edge and synchronized heading brightness;
- Running, Completed, empty, and screen-off states show no glow;
- waking while Pending resumes the glow;
- the enlarged pet, hidden host row, quota geometry, battery, and edge controls remain unchanged;
- no repeated sound or vibration is introduced.
