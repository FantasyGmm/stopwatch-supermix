# Global Edge Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Provide Launcher-style brightness and volume gestures on every visible application screen, make the gesture globally exclusive after activation, and center both progress markers on their arc tracks.

**Architecture:** A process-wide GlobalEdgeControlService owns the pure EdgeControlLogic state machine and a drawing-only EdgeControlOverlay mounted on lv_layer_top(). The service updates before Mooncake applications, cancels LVGL input with the standard wait-and-reset sequence, publishes touch ownership and setting revisions, persists through HAL, and queues Assistant volume synchronization through RuntimeSupervisor.

**Tech Stack:** C++17, ESP-IDF 5.5.4, LVGL 9, Mooncake, Brookesia service helpers, Python unittest, host C++ tests, NVS-backed HAL settings.

---

## File Map

- Create main/apps/common/edge_control/global_edge_control_service.h for the singleton API, ownership state, and setting revisions.
- Create main/apps/common/edge_control/global_edge_control_service.cpp for touch polling, cancellation, HAL writes, and overlay lifecycle.
- Modify main/apps/common/edge_control/edge_control_overlay.h and .cpp so the overlay only draws presentation state.
- Modify main/main.cpp to initialize and update the global service before Mooncake.
- Modify Launcher, Codex, and Watch Face integration to remove local overlays and arbitrate raw touch.
- Modify Settings workers to follow global revisions.
- Modify RuntimeSupervisor and AssistantRuntime to synchronize final volume and mute asynchronously.
- Modify the edge-control Python contract, pure host test, and unified offline runner.

### Task 1: Make the overlay presentation-only and center both markers

**Files:**
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py
- Modify: main/apps/common/edge_control/edge_control_overlay.h
- Modify: main/apps/common/edge_control/edge_control_overlay.cpp

- [ ] **Step 1: Write the failing presentation contract**

Replace the current shared-overlay contract with:

~~~python
def test_shared_overlay_is_presentation_only_and_centers_markers(self):
    root = ROOT / "main/apps/common/edge_control"
    header = (root / "edge_control_overlay.h").read_text(encoding="utf-8")
    source = (root / "edge_control_overlay.cpp").read_text(encoding="utf-8")

    for token in (
        "void init(lv_obj_t* parent)",
        "void show(EdgeControlSide side, int value)",
        "void hide()",
        "EdgeControlSide _visible_side",
        "int _current_value",
    ):
        self.assertIn(token, header)
    self.assertIn(
        "_control_arc_radius - _control_arc_width / 2", source
    )
    for forbidden in (
        "EdgeControlLogic _logic",
        "GetHAL()",
        "lv_indev_get_state",
        "_logic.press",
        "_logic.move",
        "_logic.release",
    ):
        self.assertNotIn(forbidden, header + source)
~~~

- [ ] **Step 2: Run it and verify the expected failure**

Run:

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract.EdgeControlFirmwareContractTests.test_shared_overlay_is_presentation_only_and_centers_markers -v
~~~

Expected: FAIL because the current overlay still owns touch, logic, and HAL.

- [ ] **Step 3: Implement the presentation-only API**

Use this state in edge_control_overlay.h:

~~~cpp
class EdgeControlOverlay {
public:
    EdgeControlOverlay() = default;
    ~EdgeControlOverlay();

    void init(lv_obj_t* parent);
    void show(EdgeControlSide side, int value);
    void hide();
    void shutdown();

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _overlay;
    EdgeControlSide _visible_side = EdgeControlSide::None;
    int _current_value = 0;

    void invalidate();
    static void draw_event_cb(lv_event_t* event);
};
~~~

Remove the active callback, input state, update(), last_release_consumed(), and apply(). Implement:

~~~cpp
void EdgeControlOverlay::show(EdgeControlSide side, int value)
{
    if (_overlay == nullptr || side == EdgeControlSide::None) return;
    _visible_side = side;
    _current_value = value;
    _overlay->setHidden(false);
    _overlay->moveForeground();
    invalidate();
}

void EdgeControlOverlay::hide()
{
    _visible_side = EdgeControlSide::None;
    if (_overlay != nullptr) _overlay->setHidden(true);
}
~~~

The draw callback reads _visible_side and _current_value. For both marker calls:

~~~cpp
const int marker_radius = _control_arc_radius - _control_arc_width / 2;
lv_point_t knob = point_on_arc(
    center.x, center.y, marker_radius, static_cast<float>(value_angle));
~~~

- [ ] **Step 4: Verify green and commit**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract.EdgeControlFirmwareContractTests.test_shared_overlay_is_presentation_only_and_centers_markers -v
git diff --check
git add main/apps/common/edge_control/edge_control_overlay.h   main/apps/common/edge_control/edge_control_overlay.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Center shared edge control markers"
~~~

Expected: test OK and clean whitespace.

### Task 2: Add global touch ownership and HAL application

**Files:**
- Create: main/apps/common/edge_control/global_edge_control_service.h
- Create: main/apps/common/edge_control/global_edge_control_service.cpp
- Modify: main/main.cpp
- Modify: tests/edge_control_logic_test.cpp
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Write failing global-service contracts**

~~~python
def test_global_service_owns_touch_and_persists_hal_values(self):
    root = ROOT / "main/apps/common/edge_control"
    header = (root / "global_edge_control_service.h").read_text(encoding="utf-8")
    source = (root / "global_edge_control_service.cpp").read_text(encoding="utf-8")
    for token in (
        "bool ownsTouch() const",
        "bool lastReleaseConsumed() const",
        "uint32_t brightnessRevision() const",
        "uint32_t volumeRevision() const",
        "GlobalEdgeControlService& GetGlobalEdgeControlService()",
    ):
        self.assertIn(token, header)
    for token in (
        "lv_layer_top()",
        "lv_indev_wait_release(indev)",
        "lv_indev_reset(indev, nullptr)",
        "setBackLightBrightness(update.value, update.persist)",
        "setSpeakerVolume(update.value, update.persist)",
    ):
        self.assertIn(token, source)

def test_global_service_updates_before_mooncake_apps(self):
    main = (ROOT / "main/main.cpp").read_text(encoding="utf-8")
    self.assertIn("GetGlobalEdgeControlService().init()", main)
    self.assertLess(
        main.index("GetGlobalEdgeControlService().update()"),
        main.index("GetMooncake().update()"),
    )
~~~

- [ ] **Step 2: Run and verify failure**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract.EdgeControlFirmwareContractTests.test_global_service_owns_touch_and_persists_hal_values   mac-codex-agent.tests.test_edge_control_firmware_contract.EdgeControlFirmwareContractTests.test_global_service_updates_before_mooncake_apps -v
~~~

Expected: ERROR or FAIL because the service does not exist.

- [ ] **Step 3: Create the service interface**

~~~cpp
#pragma once
#include "edge_control_logic.h"
#include "edge_control_overlay.h"
#include <cstdint>
#include <lvgl.h>

class GlobalEdgeControlService {
public:
    bool init();
    void update();
    void shutdown();
    bool ownsTouch() const;
    bool lastReleaseConsumed() const;
    uint32_t brightnessRevision() const;
    uint32_t volumeRevision() const;

private:
    EdgeControlLogic _logic;
    EdgeControlOverlay _overlay;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    bool _initialized = false;
    bool _owns_touch = false;
    bool _last_release_consumed = false;
    uint32_t _brightness_revision = 0;
    uint32_t _volume_revision = 0;

    void apply(const EdgeControlUpdate& update, lv_indev_t* indev);
    void suspendForScreenOff(lv_indev_t* indev);
};

GlobalEdgeControlService& GetGlobalEdgeControlService();
~~~

- [ ] **Step 4: Implement service state transitions**

Initialization mounts _overlay on lv_layer_top(). update() reads the current touch, presses EdgeControlLogic on REL-to-PR, calls move while pressed, release on PR-to-REL, and tick while idle.

Activation must execute:

~~~cpp
_owns_touch = true;
lv_indev_wait_release(indev);
lv_indev_reset(indev, nullptr);
~~~

Application uses:

~~~cpp
if (update.side != EdgeControlSide::None &&
    (update.activated || update.consumed)) {
    _overlay.show(update.side, update.value);
}
if (update.apply && update.side == EdgeControlSide::Brightness) {
    GetHAL().setBackLightBrightness(update.value, update.persist);
    _brightness_revision++;
} else if (update.apply && update.side == EdgeControlSide::Volume) {
    GetHAL().setSpeakerVolume(update.value, update.persist);
    _volume_revision++;
}
if (update.persist && update.consumed) {
    _last_release_consumed = true;
    _owns_touch = false;
}
if (update.hide) _overlay.hide();
~~~

Task 2 deliberately stops at HAL persistence so this commit remains buildable.
Task 5 adds the runtime notification only after its API exists.

When brightness is zero, suspendForScreenOff() releases and persists an active gesture once, resets logic and ownership, changes the remembered touch state to released, and hides the overlay.

- [ ] **Step 5: Wire main initialization and ordering**

Add the global-service include. Initialize under LvglLockGuard after UI HAL callbacks. Before GetMooncake().update():

~~~cpp
{
    LvglLockGuard lock;
    GetGlobalEdgeControlService().update();
}
~~~

- [ ] **Step 6: Extend the pure test and verify**

After horizontal cancellation, assert:

~~~cpp
assert(!logic.active());
assert(logic.visible_side() == EdgeControlSide::None);
~~~

Run:

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -I.   tests/edge_control_logic_test.cpp   main/apps/common/edge_control/edge_control_logic.cpp   -o /tmp/edge_control_logic_test
/tmp/edge_control_logic_test
git diff --check
~~~

Expected: focused Python and host tests pass.

- [ ] **Step 7: Commit**

~~~bash
git add main/apps/common/edge_control/global_edge_control_service.h   main/apps/common/edge_control/global_edge_control_service.cpp   main/main.cpp tests/edge_control_logic_test.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Add global edge control service"
~~~

### Task 3: Remove local overlays and arbitrate raw-touch consumers

**Files:**
- Modify: main/apps/app_launcher/view/view.h
- Modify: main/apps/app_launcher/view/view.cpp
- Modify: main/apps/app_codex_status/app_codex_status.h
- Modify: main/apps/app_codex_status/app_codex_status.cpp
- Modify: main/apps/app_watch_face/view/watch_face_manager.cpp
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Write failing consumer contracts**

Add:

~~~python
def test_launcher_uses_global_ownership_and_preserves_horizontal_swipes(self):
    root = ROOT / "main/apps/app_launcher/view"
    combined = (root / "view.h").read_text(encoding="utf-8") + (
        root / "view.cpp"
    ).read_text(encoding="utf-8")
    self.assertNotIn("std::unique_ptr<EdgeControlOverlay>", combined)
    for token in (
        "GetGlobalEdgeControlService()",
        "ownsTouch()",
        "lastReleaseConsumed()",
        "_edge_control_active",
        "handle_horizontal_swipe_release",
    ):
        self.assertIn(token, combined)

def test_codex_relies_on_global_overlay_and_keeps_boot_sound(self):
    root = ROOT / "main/apps/app_codex_status"
    combined = (root / "app_codex_status.h").read_text(encoding="utf-8") + (
        root / "app_codex_status.cpp"
    ).read_text(encoding="utf-8")
    self.assertNotIn("EdgeControlOverlay", combined)
    self.assertIn("GetHAL().playBootSfx()", combined)

def test_watch_face_cancels_raw_gesture_when_global_touch_is_owned(self):
    source = (
        ROOT / "main/apps/app_watch_face/view/watch_face_manager.cpp"
    ).read_text(encoding="utf-8")
    self.assertIn("GetGlobalEdgeControlService()", source)
    self.assertIn(
        "controls.ownsTouch() || controls.lastReleaseConsumed()", source
    )
    self.assertIn("_gesture_pressing = false", source)
~~~

Require Watch Face production code to contain:

~~~cpp
if (controls.ownsTouch() || controls.lastReleaseConsumed()) {
    _gesture_pressing = false;
    return;
}
~~~

Run the edge-control Python module and confirm these assertions fail because the
three consumers still use their previous ownership rules.

- [ ] **Step 2: Migrate Launcher**

Replace the overlay include with global_edge_control_service.h, remove _edge_controls, and add:

~~~cpp
bool _edge_control_active = false;
~~~

At the start of normal-state handling, observe ownership. While active, remove LV_OBJ_FLAG_SCROLLABLE, hide indicators, clear _clicked_app_id, stop touch hold-scroll, and set the click-suppression deadline. Restore scrolling and indicators on release.

In the raw swipe block, do not process an owned touch. On release, call handle_horizontal_swipe_release() only when lastReleaseConsumed() is false.

- [ ] **Step 3: Remove Codex local ownership**

Remove the overlay include, field, create, update, shutdown, and reset code. Keep screen-off, BLE, WebSocket, alert carousel, key handling, and playBootSfx() unchanged.

- [ ] **Step 4: Cancel Watch Face raw candidates**

Include global_edge_control_service.h and add the ownership guard shown in Step 1 at the beginning of update_gesture().

- [ ] **Step 5: Verify and commit**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract -v
git diff --check
git add main/apps/app_launcher/view/view.h   main/apps/app_launcher/view/view.cpp   main/apps/app_codex_status/app_codex_status.h   main/apps/app_codex_status/app_codex_status.cpp   main/apps/app_watch_face/view/watch_face_manager.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Share edge controls across all apps"
~~~

### Task 4: Synchronize Settings editors from global revisions

**Files:**
- Modify: main/apps/app_setup/workers/workers.h
- Modify: main/apps/app_setup/workers/device.cpp
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Add and run the failing Settings contract**

Add:

~~~python
def test_settings_editors_follow_global_setting_revisions(self):
    header = (ROOT / "main/apps/app_setup/workers/workers.h").read_text(
        encoding="utf-8"
    )
    source = (ROOT / "main/apps/app_setup/workers/device.cpp").read_text(
        encoding="utf-8"
    )
    for token in (
        "uint32_t _global_revision",
        "void setCurrentValue(int value)",
        "brightnessRevision()",
        "volumeRevision()",
        "_view->setCurrentValue",
    ):
        self.assertIn(token, header + source)
~~~

Run the focused Python test and expect FAIL because no revision is cached.

- [ ] **Step 2: Add programmatic editor updates**

Add:

~~~cpp
void setCurrentValue(int value)
{
    const int normalized = normalizeValue(value);
    if (normalized == _current_value) return;
    _syncing_slider = true;
    _slider->setValue(normalized);
    _syncing_slider = false;
    _current_value = normalized;
    updateValueLabel(_current_value);
}
~~~

Cache the corresponding global revision in each worker constructor. At the beginning of update(), if it changed, read HAL, update _applied_brightness or _applied_volume, and call setCurrentValue(). Process local slider edits after this synchronization.

- [ ] **Step 3: Verify and commit**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract -v
git diff --check
git add main/apps/app_setup/workers/workers.h   main/apps/app_setup/workers/device.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Sync Settings with global edge controls"
~~~

### Task 5: Synchronize Assistant volume and mute asynchronously

**Files:**
- Modify: main/runtime/runtime_supervisor.h
- Modify: main/runtime/runtime_supervisor.cpp
- Modify: main/runtime/assistant/assistant_runtime.h
- Modify: main/runtime/assistant/assistant_runtime.cpp
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Add and run a failing Assistant synchronization contract**

Add:

~~~python
def test_global_volume_queues_assistant_volume_and_mute_sync(self):
    service = (
        ROOT / "main/apps/common/edge_control/global_edge_control_service.cpp"
    ).read_text(encoding="utf-8")
    supervisor = (ROOT / "main/runtime/runtime_supervisor.cpp").read_text(
        encoding="utf-8"
    )
    runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text(
        encoding="utf-8"
    )
    self.assertIn("requestAssistantVolumeSync(update.value)", service)
    self.assertIn("requestOutputVolumeSync(volume)", supervisor)
    self.assertIn("pending_output_volume", runtime)
    self.assertIn("AudioHelper::call_function_async", runtime)
    self.assertIn("AudioHelper::FunctionId::SetVolume", runtime)
    self.assertIn("AudioHelper::FunctionId::SetMute", runtime)
    self.assertIn("volume <= 0", runtime)
~~~

Run the test and expect FAIL because the request path does not exist.

- [ ] **Step 2: Add runtime request APIs**

Add bool requestAssistantVolumeSync(int volume) to RuntimeSupervisor and bool requestOutputVolumeSync(int volume) to AssistantRuntime.

Supervisor implementation:

~~~cpp
bool RuntimeSupervisor::requestAssistantVolumeSync(int volume)
{
    if (!_started || !onRuntimeThread()) return false;
    if (GetAssistantRuntime().state() != AssistantRuntimeState::Running) return true;
    return GetAssistantRuntime().requestOutputVolumeSync(volume);
}
~~~

- [ ] **Step 3: Queue and submit the value**

Add std::atomic<int> pending_output_volume{-1}. The request clamps 0-100 and stores only while on the runtime thread and Running.

In AssistantRuntime::Impl::update(), consume:

~~~cpp
const int volume = pending_output_volume.exchange(-1);
if (volume >= 0 && state.state() == AssistantRuntimeState::Running) {
    const bool volume_submitted = AudioHelper::call_function_async(
        AudioHelper::FunctionId::SetVolume, static_cast<double>(volume));
    const bool mute_submitted = AudioHelper::call_function_async(
        AudioHelper::FunctionId::SetMute, volume <= 0);
    if (!volume_submitted || !mute_submitted) {
        ESP_LOGW(tag, "failed to submit output volume sync: volume=%d", volume);
    }
}
~~~

Reset pending_output_volume at start and release boundaries.

After both APIs exist, extend the volume release branch in
GlobalEdgeControlService::apply():

~~~cpp
if (update.persist) {
    GetRuntimeSupervisor().requestAssistantVolumeSync(update.value);
}
~~~

- [ ] **Step 4: Verify and commit**

~~~bash
mac-codex-agent/.venv/bin/python -m unittest   mac-codex-agent.tests.test_edge_control_firmware_contract   mac-codex-agent.tests.test_audio_service_firmware_contract -v
git diff --check
git add main/runtime/runtime_supervisor.h   main/runtime/runtime_supervisor.cpp   main/runtime/assistant/assistant_runtime.h   main/runtime/assistant/assistant_runtime.cpp   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Sync Assistant with global volume"
~~~

### Task 6: Put edge controls in the standard offline gate

**Files:**
- Modify: scripts/run_unified_offline_validation.sh
- Modify: mac-codex-agent/tests/test_edge_control_firmware_contract.py

- [ ] **Step 1: Add and run the failing runner contract**

Add:

~~~python
def test_offline_runner_executes_edge_control_host_test(self):
    runner = (ROOT / "scripts/run_unified_offline_validation.sh").read_text(
        encoding="utf-8"
    )
    self.assertIn("Global edge control host test", runner)
    self.assertIn("tests/edge_control_logic_test.cpp", runner)
    self.assertIn(
        "main/apps/common/edge_control/edge_control_logic.cpp", runner
    )
~~~

Run it and expect FAIL because the runner omits the edge-control host test.

- [ ] **Step 2: Add the host stage**

Insert before the clean build:

~~~bash
echo "[15/17] Global edge control host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I.     tests/edge_control_logic_test.cpp     main/apps/common/edge_control/edge_control_logic.cpp     -o "$HOST_TEST_DIR/edge_control_logic_test"
"$HOST_TEST_DIR/edge_control_logic_test"

echo "[16/17] Isolated unified candidate clean build and capacity gates"
./scripts/build_unified_candidate.sh --clean

echo "[17/17] Whitespace error check"
git diff --check
~~~

Change the earlier denominators from /16 to /17 without altering commands.

- [ ] **Step 3: Run the complete gate**

~~~bash
./scripts/run_unified_offline_validation.sh
~~~

Expected: Python suite, all host tests, clean ESP-IDF build, capacity gate, and whitespace check pass; the final line reports unified offline validation passed.

- [ ] **Step 4: Commit runner coverage**

~~~bash
git add scripts/run_unified_offline_validation.sh   mac-codex-agent/tests/test_edge_control_firmware_contract.py
git commit -m "Validate global edge controls offline"
~~~

### Task 7: Final source review and hardware handoff

**Files:**
- Inspect every file changed since a7b95bf.
- Keep hardware evidence separate from source commits.

- [ ] **Step 1: Review exact scope**

~~~bash
git diff --stat a7b95bf..HEAD
git diff --check a7b95bf..HEAD
rg -n "EdgeControlOverlay|GlobalEdgeControlService|lvTouchpad"   main/apps main/main.cpp
~~~

Expected: one global overlay owner; no Launcher/Codex overlay; only documented raw-touch consumers poll lvTouchpad.

- [ ] **Step 2: Check hardware without writing**

~~~bash
ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null
~~~

If no port exists, report device testing as pending. If a port exists, use the repository's backup-first, readback-SHA workflow before any candidate flash.

- [ ] **Step 3: Run the physical acceptance matrix when connected**

Open Launcher, Assistant, Codex, Clock selection, Alarm, Watch Face, Stopwatch, Badge, IMU, FFT, Settings menu, Settings Brightness, Settings Volume, and Wi-Fi. On each visible page verify left brightness, right volume, centered markers, and no accidental navigation. Verify Settings synchronization, live Assistant speech volume, screen-off inactivity, cross-app persistence, and reboot persistence.

- [ ] **Step 4: Report gates separately**

Report exact commit, test counts, firmware bytes/headroom, and physical matrix status. Never treat offline success as hardware validation.
