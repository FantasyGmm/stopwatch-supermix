# Codex Shared Edge Controls and Pending Sound Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reuse Launcher's complete brightness/volume edge-control experience in the Codex monitor and play the complete startup sound for Pending alerts.

**Architecture:** A pure C++ gesture state machine produces testable control actions. A shared LVGL EdgeControlOverlay reads touch input, renders the existing arcs, applies values through HAL, and is instantiated by both Launcher and Codex. The existing BLE chime event remains unchanged and the Codex firmware maps it to the embedded boot sound.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.5.4, Smooth UI Toolkit, Python 3.9 unittest source contracts.

---

### Task 1: Build a Pure Edge-Control Gesture State Machine

**Files:**
- Create: main/apps/common/edge_control/edge_control_logic.h
- Create: main/apps/common/edge_control/edge_control_logic.cpp
- Create: tests/edge_control_logic_test.cpp

- [ ] **Step 1: Write the failing host test**

Create a table-style test covering left/right selection, activation direction, horizontal cancellation, mapping, persistence, and delayed hide:

~~~cpp
#include "main/apps/common/edge_control/edge_control_logic.h"

#include <cassert>

int main()
{
    EdgeControlLogic logic;

    logic.press(80, 300, 50, 60, 1000);
    auto update = logic.move(82, 250, 1040);
    assert(update.side == EdgeControlSide::Brightness);
    assert(update.activated);
    assert(update.value > 50);
    assert(update.apply);
    assert(!update.persist);

    update = logic.release(1100);
    assert(update.consumed);
    assert(update.apply);
    assert(update.persist);
    assert(!logic.tick(1799).hide);
    assert(logic.tick(1800).hide);

    logic.reset();
    logic.press(380, 300, 50, 60, 2000);
    update = logic.move(382, 350, 2040);
    assert(update.side == EdgeControlSide::Volume);
    assert(update.activated);
    assert(update.value < 60);
    assert(update.value % 5 == 0);

    logic.reset();
    logic.press(80, 300, 50, 60, 3000);
    update = logic.move(130, 302, 3040);
    assert(update.cancelled);
    assert(!update.consumed);
    assert(!logic.release(3060).consumed);

    logic.reset();
    logic.press(80, 20, 50, 60, 4000);
    assert(!logic.move(80, 100, 4040).activated);

    logic.reset();
    logic.press(80, 300, 10, 100, 5000);
    assert(logic.move(80, 466, 5040).value == 10);
    logic.reset();
    logic.press(380, 300, 10, 100, 6000);
    assert(logic.move(380, 0, 6040).value == 100);
}
~~~

- [ ] **Step 2: Compile and verify RED**

~~~bash
clang++ -std=c++17 -Wall -Wextra -Werror -I.   tests/edge_control_logic_test.cpp   main/apps/common/edge_control/edge_control_logic.cpp   -o /tmp/edge_control_logic_test
~~~

Expected: compilation fails because the logic files do not exist.

- [ ] **Step 3: Define the public state-machine interface**

Create edge_control_logic.h:

~~~cpp
#pragma once

#include <cstdint>

enum class EdgeControlSide : uint8_t {
    None,
    Brightness,
    Volume,
};

struct EdgeControlUpdate {
    EdgeControlSide side = EdgeControlSide::None;
    int value = 0;
    bool activated = false;
    bool apply = false;
    bool persist = false;
    bool consumed = false;
    bool cancelled = false;
    bool hide = false;
};

class EdgeControlLogic {
public:
    void press(int x, int y, int brightness, int volume, uint32_t now_ms);
    EdgeControlUpdate move(int x, int y, uint32_t now_ms);
    EdgeControlUpdate release(uint32_t now_ms);
    EdgeControlUpdate tick(uint32_t now_ms);
    void reset();

    bool active() const;
    int current_value() const;
    EdgeControlSide visible_side() const;

private:
    static int normalize(EdgeControlSide side, int value);

    EdgeControlSide _candidate = EdgeControlSide::None;
    EdgeControlSide _active = EdgeControlSide::None;
    EdgeControlSide _visible = EdgeControlSide::None;
    int _start_x = 0;
    int _start_y = 0;
    int _start_value = 0;
    int _current_value = 0;
    int _applied_value = -1;
    uint32_t _last_apply_ms = 0;
    uint32_t _hide_at_ms = 0;
};
~~~

- [ ] **Step 4: Implement the exact gesture contract**

Use constants: top exclusion 30px, half width 233px, activation distance 24px, drag range 210px, live apply period 40ms, hide delay 700ms. Brightness clamps to 10-100. Volume clamps to 0-100 and rounds to the nearest 5 using ((value + 2) / 5) * 5.

A vertical candidate activates only when abs(dy) > 24 and abs(dy) > abs(dx). A horizontal move under the inverse condition cancels without consuming the gesture. Upward delta is start_y - y. A live apply is emitted only after 40ms and only if the value differs from the last applied value. Release of an active control always emits apply=true, persist=true, consumed=true and starts the 700ms hide timer.

- [ ] **Step 5: Compile, run GREEN, and commit**

~~~bash
clang++ -std=c++17 -Wall -Wextra -Werror -I.   tests/edge_control_logic_test.cpp   main/apps/common/edge_control/edge_control_logic.cpp   -o /tmp/edge_control_logic_test
/tmp/edge_control_logic_test
git add main/apps/common/edge_control/edge_control_logic.h   main/apps/common/edge_control/edge_control_logic.cpp   tests/edge_control_logic_test.cpp
git commit -m "Add shared edge control gesture logic"
~~~

### Task 2: Create the Shared LVGL Overlay

**Files:**
- Create: main/apps/common/edge_control/edge_control_overlay.h
- Create: main/apps/common/edge_control/edge_control_overlay.cpp
- Create: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Write a failing source contract**

The Python test reads the shared header/source and asserts:

~~~python
self.assertIn("class EdgeControlOverlay", header)
self.assertIn("EdgeControlLogic _logic", header)
self.assertIn("void init(lv_obj_t* parent", header)
self.assertIn("void update()", header)
self.assertIn("void shutdown()", header)
self.assertIn("GetHAL().setBackLightBrightness", source)
self.assertIn("GetHAL().setSpeakerVolume", source)
self.assertIn("LV_EVENT_DRAW_MAIN_BEGIN", source)
for token in ("draw_sun_icon", "draw_speaker_icon", "draw_percent_label"):
    self.assertIn(token, source)
~~~

- [ ] **Step 2: Run and verify RED**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent/tests/test_edge_control_firmware_contract.py -v
~~~

Expected: failure because the shared overlay does not exist.

- [ ] **Step 3: Define the overlay lifecycle**

Create this public boundary:

~~~cpp
#pragma once

#include "edge_control_logic.h"

#include <functional>
#include <lvgl.h>
#include <memory>

namespace uitk::lvgl_cpp {
class Container;
}

class EdgeControlOverlay {
public:
    using ActiveChanged = std::function<void(bool)>;

    EdgeControlOverlay() = default;
    ~EdgeControlOverlay();

    void init(lv_obj_t* parent, ActiveChanged on_active_changed = {});
    void update();
    void shutdown();
    bool last_release_consumed() const;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _overlay;
    EdgeControlLogic _logic;
    ActiveChanged _on_active_changed;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    bool _last_release_consumed = false;

    void apply(const EdgeControlUpdate& update);
    void invalidate();
    static void draw_event_cb(lv_event_t* event);
};
~~~

- [ ] **Step 4: Move the existing Launcher visuals without changing them**

Move the control dimensions, colors, point_on_arc, draw_arc, draw_circle, draw_line, draw_percent_label, draw_sun_icon, draw_speaker_icon, and the overlay draw callback from Launcher view.cpp into edge_control_overlay.cpp.

The overlay is 466x466, transparent, floating, non-clickable, non-scrollable, and hidden until activation. On activation it becomes visible and foreground. On update.apply, call the matching HAL setter with update.persist. On update.hide, hide it. Use the component pointer as LVGL event user data.

On shutdown, if logic is active, call release with current millis and apply the persisted result, invoke on_active_changed(false), remove the draw callback with the overlay object, then destroy it.

- [ ] **Step 5: Implement touch-to-logic translation**

Read GetHAL().lvTouchpad once per update. On press transition, call logic.press with current point and current HAL brightness/volume. During press, call logic.move. On release transition, call logic.release and retain update.consumed in _last_release_consumed. When released without a new edge, call logic.tick for delayed hide.

Invoke on_active_changed(true) only on activation and false only after persistent release or shutdown.

- [ ] **Step 6: Run contract, build the component, and commit**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent/tests/test_edge_control_firmware_contract.py -v
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
git add main/apps/common/edge_control   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Add shared edge control overlay"
~~~

Expected: contract and firmware build pass.

### Task 3: Migrate Launcher to the Shared Component

**Files:**
- Modify: main/apps/app_launcher/view/view.h
- Modify: main/apps/app_launcher/view/view.cpp
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Extend the contract and verify RED**

Require LauncherView to include and own EdgeControlOverlay, and reject old private control implementation:

~~~python
self.assertIn('#include <apps/common/edge_control/edge_control_overlay.h>', launcher_h)
self.assertIn("std::unique_ptr<EdgeControlOverlay> _edge_controls", launcher_h)
for legacy in (
    "_control_overlay",
    "_control_candidate",
    "_active_control",
    "activate_control(",
    "update_active_control(",
    "apply_control_value(",
    "launcher_control_overlay_draw_event_cb",
):
    self.assertNotIn(legacy, launcher_h + launcher_cpp)
~~~

Expected: failures against the current private implementation.

- [ ] **Step 2: Replace private control ownership**

Delete only control-specific enums, constants, draw helpers, fields, and methods. Keep Launcher horizontal swipe fields and handle_horizontal_swipe_release.

Create the component after _panel:

~~~cpp
_edge_controls = std::make_unique<EdgeControlOverlay>();
_edge_controls->init(_panel->get(), [this](bool active) {
    if (active) {
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        set_lr_indicators_hidden(true);
    } else {
        _panel->addFlag(LV_OBJ_FLAG_SCROLLABLE);
        set_lr_indicators_hidden(false);
        _suppress_click_until_ms = GetHAL().millis() + 350;
    }
});
~~~

Destroy _edge_controls before _panel.

- [ ] **Step 3: Preserve horizontal carousel gestures**

Keep Launcher-owned press point, scroll start, and start time. During each normal update, update the shared component first. On touch release, call handle_horizontal_swipe_release only when _edge_controls->last_release_consumed() is false.

A vertical edge activation must suppress carousel movement; a horizontal cancellation must still resolve through the existing fast/drag swipe thresholds. Do not change hold-scroll, icon looping, click callbacks, or page indicators outside the active-control callback.

- [ ] **Step 4: Run focused tests and build**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent/tests/test_edge_control_firmware_contract.py -v
/tmp/edge_control_logic_test
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
~~~

Expected: shared contract, logic test, and firmware build pass with Launcher behavior preserved.

- [ ] **Step 5: Commit**

~~~bash
git add main/apps/app_launcher/view/view.h   main/apps/app_launcher/view/view.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Use shared edge controls in Launcher"
~~~

### Task 4: Add Edge Controls and Startup Sound to Codex

**Files:**
- Modify: main/apps/app_codex_status/app_codex_status.h
- Modify: main/apps/app_codex_status/app_codex_status.cpp
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py
- Modify: README.md

- [ ] **Step 1: Extend the contract and verify RED**

~~~python
self.assertIn('#include <apps/common/edge_control/edge_control_overlay.h>', codex_h)
self.assertIn("std::unique_ptr<EdgeControlOverlay> _edge_controls", codex_h)
self.assertIn("_edge_controls->init(_panel)", codex_cpp)
self.assertIn("_edge_controls->update()", codex_cpp)
self.assertIn("_edge_controls->shutdown()", codex_cpp)
self.assertIn("GetHAL().playBootSfx()", codex_cpp)
self.assertNotIn("audio::play_tone(660", codex_cpp)
~~~

Expected: failures before Codex integration.

- [ ] **Step 2: Add Codex lifecycle ownership**

Create _edge_controls after the root _panel is fully initialized:

~~~cpp
_edge_controls = std::make_unique<EdgeControlOverlay>();
_edge_controls->init(_panel);
~~~

During onRunning, under LvglLockGuard, call update only when !_screen_off. In destroy_view, call shutdown and reset the component before deleting _panel. This prevents callbacks from referring to deleted LVGL objects.

- [ ] **Step 3: Replace the Pending tone**

Change only the Chime branch:

~~~cpp
if (kind == CodexMessageKind::Chime) {
    GetHAL().playBootSfx();
    return true;
}
~~~

Do not change Agent notification acknowledgement, BLE JSON, Running, or Completed behavior.

- [ ] **Step 4: Document the shared controls and sound**

Document left brightness, right volume, global persistence, and the complete startup sound for Pending. State that zero volume keeps Pending silent.

- [ ] **Step 5: Run focused and complete verification**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent/tests/test_edge_control_firmware_contract.py -v
mac-codex-agent/.venv/bin/python -m unittest discover   -s mac-codex-agent/tests -p 'test_*.py'
/tmp/edge_control_logic_test
git diff --check
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
~~~

Expected: all tests pass, firmware fits ota_0, and no 660 Hz Pending tone remains.

- [ ] **Step 6: Commit**

~~~bash
git add main/apps/app_codex_status/app_codex_status.h   main/apps/app_codex_status/app_codex_status.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py README.md
git commit -m "Add shared edge controls to Codex"
~~~

### Task 5: Install, Flash, and Validate on Device

**Files:**
- No source changes expected.

- [ ] **Step 1: Confirm final repository state**

Run all host tests again and record build/StopWatch-UserDemo.bin size and SHA-256. Confirm only unrelated pre-existing untracked paths remain.

- [ ] **Step 2: Flash only ota_0**

Stop the Mac menu agent while flashing. Detect the current /dev/cu.usbmodem* port and write build/StopWatch-UserDemo.bin only at 0x20000. Do not erase NVS, resources, or assistant ota_1.

- [ ] **Step 3: Verify flash by readback**

Read back exactly the built binary size from 0x20000 at a stable baud. Compare SHA-256 and byte equality with the build image.

- [ ] **Step 4: Restart Agent and validate Launcher**

In Launcher:
1. Swipe vertically on the left and verify the blue brightness arc and percentage.
2. Swipe vertically on the right and verify the cyan volume arc and percentage.
3. Verify horizontal app switching still works.
4. Leave values at visible test settings.

- [ ] **Step 5: Validate Codex**

Open Codex and wait for BLE connection:
1. Confirm it starts with the Launcher-adjusted global values.
2. Adjust brightness on the left and volume on the right with identical visuals.
3. Return to Launcher/Clock and confirm values remain.
4. Reboot and confirm persisted values return.
5. Trigger a new Pending alert and confirm one complete startup sound plays.
6. Confirm Running and Completed transitions remain silent.

- [ ] **Step 6: Report physical-only checks separately**

Automated completion requires tests, build, flash hash, and BLE reconnect. Clearly mark any screen, gesture, audio, or reboot behavior that still requires the user to observe physically.
