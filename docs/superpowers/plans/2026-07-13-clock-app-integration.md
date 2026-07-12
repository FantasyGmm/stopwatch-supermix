# Clock App Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Alarm Clock, Watch Face, and Stopwatch launcher entries with one Clock application that opens a three-button feature selector and preserves all existing feature behavior.

**Architecture:** `AppClock` owns a four-state UI controller and the long-lived alarm model. Existing alarm views, `WatchFaceManager`, and `StopwatchView` remain focused feature modules; a small pure navigation resolver gives Home priority over long-left feature navigation and makes button behavior host-testable.

**Tech Stack:** C++17, ESP-IDF 5.5.4, Mooncake, LVGL 9, Smooth UI Toolkit, Python unittest source contracts

---

## File Map

- Create `main/apps/app_clock/app_clock.h` and `.cpp`: single Clock `AppAbility`, lifecycle, alarm ownership, and feature state transitions.
- Create `main/apps/app_clock/clock_navigation.h` and `.cpp`: pure button-priority resolver.
- Create `main/apps/app_clock/view/selection.h` and `.cpp`: approved vertical three-button selector.
- Create `tests/clock_navigation_test.cpp`: host test for button priority.
- Create `mac-codex-agent/tests/test_clock_firmware_contract.py`: registration, icon, and selector source contract.
- Modify `main/main.cpp` and `main/apps/apps.h`: replace three app registrations/includes with Clock.
- Modify `main/apps/app_stopwatch/view/view.cpp`: execute left reset/lap on short release.
- Delete three old application wrapper `.h/.cpp` pairs while preserving their model/view directories.
- Delete `icon_watch_face.c`, `icon_stopwatch.c`, and their declarations.
- Modify `README.md`: document the merged Clock entry.

### Task 1: Lock The Launcher And Asset Contract

**Files:**
- Create: `mac-codex-agent/tests/test_clock_firmware_contract.py`
- Modify: `main/main.cpp`
- Modify: `main/apps/apps.h`
- Modify: `main/assets/assets.h`

- [ ] **Step 1: Write the failing source contract**

Create a unittest that reads the repository sources and asserts:

```python
self.assertEqual(main_cpp.count("std::make_unique<AppClock>()"), 1)
self.assertNotIn("std::make_unique<AppAlarmClock>()", main_cpp)
self.assertNotIn("std::make_unique<AppWatchFace>()", main_cpp)
self.assertNotIn("std::make_unique<AppStopWatch>()", main_cpp)
self.assertIn('#include "app_clock/app_clock.h"', apps_h)
self.assertNotIn("app_alarm_clock/app_alarm_clock.h", apps_h)
self.assertNotIn("app_watch_face/app_watch_face.h", apps_h)
self.assertNotIn("app_stopwatch/app_stopwatch.h", apps_h)
self.assertIn("LV_IMG_DECLARE(icon_clock);", assets_h)
self.assertNotIn("LV_IMG_DECLARE(icon_watch_face);", assets_h)
self.assertNotIn("LV_IMG_DECLARE(icon_stopwatch);", assets_h)
```

- [ ] **Step 2: Run the contract and confirm RED**

```bash
python3 -m unittest mac-codex-agent/tests/test_clock_firmware_contract.py
```

Expected: failures because Clock is not registered and old registrations remain.

- [ ] **Step 3: Replace registration and declarations**

Register only `AppClock`, include only `app_clock/app_clock.h` for this feature family, retain `icon_clock`, and remove the two obsolete icon declarations.

- [ ] **Step 4: Run the contract**

Expected: registration assertions pass; assertions that depend on not-yet-created Clock sources may remain RED until Task 3.

- [ ] **Step 5: Commit**

```bash
git add main/main.cpp main/apps/apps.h main/assets/assets.h mac-codex-agent/tests/test_clock_firmware_contract.py
git commit -m "Register merged Clock application"
```

### Task 2: Add Testable Clock Button Routing

**Files:**
- Create: `main/apps/app_clock/clock_navigation.h`
- Create: `main/apps/app_clock/clock_navigation.cpp`
- Create: `tests/clock_navigation_test.cpp`

- [ ] **Step 1: Write the failing navigation test**

Cover these observable cases:

```cpp
assert(resolve_clock_navigation(true, true, true, true) == ClockNavigation::Home);
assert(resolve_clock_navigation(false, true, false, false) == ClockNavigation::FeatureMenu);
assert(resolve_clock_navigation(false, false, true, false) == ClockNavigation::Previous);
assert(resolve_clock_navigation(false, false, false, true) == ClockNavigation::Next);
assert(resolve_clock_navigation(false, false, false, false) == ClockNavigation::None);
```

- [ ] **Step 2: Compile and confirm RED**

```bash
c++ -std=c++17 -Imain tests/clock_navigation_test.cpp main/apps/app_clock/clock_navigation.cpp -o /tmp/clock_navigation_test
```

Expected: compilation fails because the resolver does not exist.

- [ ] **Step 3: Implement the minimal resolver**

Define `ClockNavigation` and `resolve_clock_navigation(bool home_chord, bool left_hold, bool left_click, bool right_click)`. Resolve in exactly this order: Home, FeatureMenu, Previous, Next, None.

- [ ] **Step 4: Compile and run GREEN**

```bash
c++ -std=c++17 -Imain tests/clock_navigation_test.cpp main/apps/app_clock/clock_navigation.cpp -o /tmp/clock_navigation_test
/tmp/clock_navigation_test
```

Expected: exit status 0.

- [ ] **Step 5: Commit**

```bash
git add main/apps/app_clock/clock_navigation.h main/apps/app_clock/clock_navigation.cpp tests/clock_navigation_test.cpp
git commit -m "Add Clock button navigation"
```

### Task 3: Build The Clock Selector And State Controller

**Files:**
- Create: `main/apps/app_clock/app_clock.h`
- Create: `main/apps/app_clock/app_clock.cpp`
- Create: `main/apps/app_clock/view/selection.h`
- Create: `main/apps/app_clock/view/selection.cpp`
- Modify: `mac-codex-agent/tests/test_clock_firmware_contract.py`

- [ ] **Step 1: Extend the contract for Clock identity and selector labels**

Assert that the new sources contain:

```python
self.assertIn('setAppInfo().name = "Clock"', app_clock_cpp)
self.assertIn('setAppInfo().icon = (void*)&icon_clock', app_clock_cpp)
for label in ("Alarm", "Watch Face", "Stopwatch"):
    self.assertIn(label, selection_cpp)
```

Run the focused unittest and confirm RED.

- [ ] **Step 2: Implement the selector view**

Create a black 466×466 root with centered `CLOCK` heading and three 374-pixel-wide rounded buttons. Expose a consumed selection enum and an `alarm_enabled` constructor argument; disabled Alarm is visibly dimmed and non-clickable.

- [ ] **Step 3: Implement `AppClock` lifecycle**

Use `enum class Feature { Selection, Alarm, WatchFace, Stopwatch }`. On create, initialize and connect the alarm model using the existing trigger flow. On open, create Selection. On running:

1. update HAL button states once;
2. resolve Home/FeatureMenu/Previous/Next;
3. close immediately on Home;
4. transition active features to Selection on FeatureMenu;
5. update the alarm model in every state;
6. route Previous/Next only to Watch Face;
7. update only the active feature view.

On sleeping, update the alarm model. On close, destroy current UI and input state. On destroy, release the alarm model.

- [ ] **Step 4: Run the contract and navigation tests**

Expected: both pass.

- [ ] **Step 5: Commit**

```bash
git add main/apps/app_clock mac-codex-agent/tests/test_clock_firmware_contract.py
git commit -m "Add Clock feature selector"
```

### Task 4: Preserve Stopwatch And Feature Behavior

**Files:**
- Modify: `main/apps/app_stopwatch/view/view.cpp`
- Modify: `mac-codex-agent/tests/test_clock_firmware_contract.py`
- Modify: `tests/clock_navigation_test.cpp`

- [ ] **Step 1: Add a contract assertion for left short-release behavior**

Assert that Stopwatch uses `btnA.wasClicked()` for `handle_btn_left_clicked()` and does not invoke that handler from `btnA.wasPressed()`.

- [ ] **Step 2: Run and confirm RED**

Expected: failure because the current implementation acts on press.

- [ ] **Step 3: Move only the left action to click/release**

Keep press/release animation feedback, keep right-button behavior immediate, and call the left reset/lap handler only from `wasClicked()`. A held left button must produce no click action.

- [ ] **Step 4: Run focused tests**

Run the Clock contract and navigation executable. Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add main/apps/app_stopwatch/view/view.cpp mac-codex-agent/tests/test_clock_firmware_contract.py
git commit -m "Route Stopwatch controls through Clock"
```

### Task 5: Remove Old Wrappers And Launcher Icons

**Files:**
- Delete: `main/apps/app_alarm_clock/app_alarm_clock.h`
- Delete: `main/apps/app_alarm_clock/app_alarm_clock.cpp`
- Delete: `main/apps/app_watch_face/app_watch_face.h`
- Delete: `main/apps/app_watch_face/app_watch_face.cpp`
- Delete: `main/apps/app_stopwatch/app_stopwatch.h`
- Delete: `main/apps/app_stopwatch/app_stopwatch.cpp`
- Delete: `main/assets/images/icon_watch_face.c`
- Delete: `main/assets/images/icon_stopwatch.c`

- [ ] **Step 1: Extend the contract to require deleted paths**

Use `assertFalse(path.exists())` for all eight files and confirm RED.

- [ ] **Step 2: Delete exactly the eight obsolete files**

Keep all alarm model/views, watch-face views and images, and Stopwatch model/view files.

- [ ] **Step 3: Run focused tests and residual search**

```bash
python3 -m unittest mac-codex-agent/tests/test_clock_firmware_contract.py
rg -n "AppAlarmClock|AppWatchFace|AppStopWatch|icon_watch_face|icon_stopwatch" main -g '!build/**'
```

Expected: unittest passes and ripgrep returns no functional references.

- [ ] **Step 4: Commit**

```bash
git add -u main/apps/app_alarm_clock main/apps/app_watch_face main/apps/app_stopwatch main/assets/images
git commit -m "Remove standalone clock applications"
```

### Task 6: Documentation, Full Verification, And Device Flash

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Replace the three launcher entries with one `Clock` entry and document its Alarm, Watch Face, and Stopwatch selection page. Renumber later launcher entries without changing unrelated Codex documentation.

- [ ] **Step 2: Run host verification**

```bash
python3 -m unittest mac-codex-agent/tests/test_clock_firmware_contract.py
c++ -std=c++17 -Imain tests/clock_navigation_test.cpp main/apps/app_clock/clock_navigation.cpp -o /tmp/clock_navigation_test
/tmp/clock_navigation_test
git diff --check
```

Expected: all commands exit 0.

- [ ] **Step 3: Build with ESP-IDF 5.5.4**

```bash
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
```

Expected: `StopWatch-UserDemo.bin` links successfully and fits `ota_0`.

- [ ] **Step 4: Commit documentation**

```bash
git add README.md
git commit -m "Document merged Clock application"
```

- [ ] **Step 5: Flash the connected StopWatch**

Detect the current `/dev/cu.usbmodem*` port, then run:

```bash
idf.py -p <detected-port> flash
```

Expected: bootloader, partition table, OTA data, and launcher application writes all pass hash verification and the device hard-resets.

- [ ] **Step 6: Perform device acceptance checks**

Confirm one Clock launcher entry with the alarm icon; validate the selector, all three features, long-left feature return, two-button Home, Watch Face short navigation, Stopwatch controls, and alarm background triggering. Record any check that requires a timed alarm as pending only if physical observation cannot be automated.
