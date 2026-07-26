# Global AMOLED Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one global 15-second dim and 60-second screen-off policy, with wake-only input consumption, semantic keep-awake reasons, and a Codex exception that applies only while external power is connected.

**Architecture:** Put timeout decisions in a dependency-free `DisplayProtectionLogic` state machine and keep HAL/LVGL effects in `GlobalDisplayProtectionService`. HAL distinguishes user brightness from temporary protection brightness and provides a generic button-release gate. Applications publish semantic state only: Codex publishes its power-aware policy, Assistant publishes real interaction activity, and Clock publishes Stopwatch/alarm holds.

**Tech Stack:** C++17, ESP-IDF 5.5.4, LVGL 9, M5GFX, Mooncake, native assertion tests, Python firmware-contract tests, existing unified validation runner.

---

## File map

- Create `main/runtime/display/display_protection_logic.h` and `.cpp`: pure timeout, keep-awake, automatic-policy, and wake-consumption state machine.
- Create `main/runtime/display/global_display_protection_service.h` and `.cpp`: HAL/LVGL integration, brightness restoration, input observation, and diagnostics snapshot.
- Create `main/hal/button_input_gate.h`: dependency-free raw-button suppression until both physical buttons are released.
- Modify `main/hal/hal.h`, `main/hal/hal_button.cpp`, and `main/hal/hal_display.cpp`: raw button access, gate integration, and separate user/temporary brightness APIs.
- Modify `main/main.cpp`: initialize and update protection before edge controls and Mooncake.
- Create `main/apps/app_assistant/assistant_display_activity.h`: pure Assistant interaction/stale-timeout tracker.
- Modify `main/apps/app_assistant/app_assistant.h` and `.cpp`: publish `AssistantInteraction` without treating animation as activity.
- Modify `main/apps/app_stopwatch/view/view.h` and `.cpp`, plus `main/apps/app_clock/app_clock.h` and `.cpp`: make the Stopwatch model outlive its subpage and publish Running.
- Modify `main/apps/app_codex_status/app_codex_status.h` and `.cpp`: use the default policy on battery and disable it only on external power.
- Modify `main/debug/debug_command_parser.h` and `.cpp`, `main/debug/debug_command_service.cpp`: add read-only display-protection diagnostics.
- Create `tests/display_protection_logic_test.cpp`, `tests/button_input_gate_test.cpp`, and `tests/assistant_display_activity_test.cpp`: host behavior tests.
- Create `mac-codex-agent/tests/test_display_protection_firmware_contract.py`: integration/source contracts.
- Modify `tests/debug_command_test.cpp` and `scripts/run_unified_offline_validation.sh`: diagnostics parsing and standard validation coverage.

### Task 1: Pure display-protection policy

**Files:**
- Create: `main/runtime/display/display_protection_logic.h`
- Create: `main/runtime/display/display_protection_logic.cpp`
- Create: `tests/display_protection_logic_test.cpp`

- [ ] **Step 1: Write the failing policy test**

Cover exact boundaries, delayed direct-off, wraparound, activity reset, wake consumption, policy toggling, all named reasons, idempotence, and final-reason release:

```cpp
#include "runtime/display/display_protection_logic.h"
#include <cassert>
#include <cstdint>

int main()
{
    DisplayProtectionLogic logic;
    logic.reset(1000);
    assert(logic.state() == DisplayProtectionState::Active);
    assert(logic.update(15999).action == DisplayProtectionAction::None);
    assert(logic.update(16000).action == DisplayProtectionAction::Dim);
    assert(logic.update(60999).action == DisplayProtectionAction::None);
    assert(logic.update(61000).action == DisplayProtectionAction::ScreenOff);

    auto wake = logic.userActivity(62000);
    assert(wake.action == DisplayProtectionAction::Restore);
    assert(wake.consume_input);
    assert(logic.state() == DisplayProtectionState::Active);

    logic.reset(0);
    assert(logic.update(60000).action == DisplayProtectionAction::ScreenOff);
    logic.reset(UINT32_MAX - 10000U);
    assert(logic.update(4999U).action == DisplayProtectionAction::Dim);
    assert(logic.update(49999U).action == DisplayProtectionAction::ScreenOff);

    logic.reset(0);
    assert(logic.userActivity(14999).action == DisplayProtectionAction::None);
    assert(logic.update(29998).action == DisplayProtectionAction::None);
    assert(logic.update(29999).action == DisplayProtectionAction::Dim);
    auto dim_wake = logic.userActivity(30000);
    assert(dim_wake.action == DisplayProtectionAction::Restore);
    assert(!dim_wake.consume_input);

    logic.reset(0);
    assert(logic.setAutomaticEnabled(false, 100).action == DisplayProtectionAction::None);
    assert(!logic.automaticEnabled());
    assert(logic.update(70000).action == DisplayProtectionAction::None);
    logic.setAutomaticEnabled(true, 70000);
    assert(logic.update(84999).action == DisplayProtectionAction::None);
    assert(logic.update(85000).action == DisplayProtectionAction::Dim);
    auto disable_while_dimmed = logic.setAutomaticEnabled(false, 85001);
    assert(disable_while_dimmed.action == DisplayProtectionAction::Restore);

    logic.reset(0);
    logic.update(15000);
    auto held_while_dimmed = logic.setKeepAwake(
        DisplayKeepAwakeReason::AlarmRinging, true, 15001);
    assert(held_while_dimmed.action == DisplayProtectionAction::Restore);

    for (auto reason : {DisplayKeepAwakeReason::AssistantInteraction,
                        DisplayKeepAwakeReason::StopwatchRunning,
                        DisplayKeepAwakeReason::AlarmRinging}) {
        logic.reset(0);
        logic.setKeepAwake(reason, true, 100);
        logic.setKeepAwake(reason, true, 200);
        assert(logic.update(100000).action == DisplayProtectionAction::None);
        logic.setKeepAwake(reason, false, 100000);
        assert(logic.update(114999).action == DisplayProtectionAction::None);
        assert(logic.update(115000).action == DisplayProtectionAction::Dim);
    }

    logic.reset(0);
    logic.setKeepAwake(DisplayKeepAwakeReason::AssistantInteraction, true, 10);
    logic.setKeepAwake(DisplayKeepAwakeReason::StopwatchRunning, true, 20);
    logic.setKeepAwake(DisplayKeepAwakeReason::AssistantInteraction, false, 30);
    assert(logic.reasonMask() != 0);
    assert(logic.update(70000).action == DisplayProtectionAction::None);
    logic.setKeepAwake(DisplayKeepAwakeReason::StopwatchRunning, false, 70000);
    assert(logic.reasonMask() == 0);
    assert(DisplayProtectionLogic::dimBrightness(10) == 10);
    assert(DisplayProtectionLogic::dimBrightness(20) == 20);
    assert(DisplayProtectionLogic::dimBrightness(80) == 20);
    return 0;
}
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_logic_test.cpp \
  main/runtime/display/display_protection_logic.cpp \
  -o /tmp/display_protection_logic_test
```

Expected: compilation fails because the policy files do not exist.

- [ ] **Step 3: Implement the minimal pure policy**

Define these public types and constants in the header:

```cpp
enum class DisplayProtectionState : uint8_t { Active, Dimmed, ScreenOff };
enum class DisplayProtectionAction : uint8_t { None, Restore, Dim, ScreenOff };
enum class DisplayKeepAwakeReason : uint8_t {
    AssistantInteraction = 1U << 0,
    StopwatchRunning = 1U << 1,
    AlarmRinging = 1U << 2,
};

struct DisplayProtectionDecision {
    DisplayProtectionAction action = DisplayProtectionAction::None;
    bool consume_input = false;
};

class DisplayProtectionLogic {
public:
    static constexpr uint32_t DimAfterMs = 15000;
    static constexpr uint32_t ScreenOffAfterMs = 60000;

    void reset(uint32_t now_ms);
    DisplayProtectionDecision update(uint32_t now_ms);
    DisplayProtectionDecision userActivity(uint32_t now_ms);
    DisplayProtectionDecision setAutomaticEnabled(bool enabled, uint32_t now_ms);
    DisplayProtectionDecision setKeepAwake(DisplayKeepAwakeReason reason, bool active,
                                           uint32_t now_ms);
    DisplayProtectionState state() const;
    bool automaticEnabled() const;
    uint8_t reasonMask() const;
    static int dimBrightness(int user_brightness);

private:
    DisplayProtectionDecision restore(uint32_t now_ms, bool consume_input);
    bool heldAwake() const;
    DisplayProtectionState _state = DisplayProtectionState::Active;
    uint32_t _inactivity_origin_ms = 0;
    uint8_t _reason_mask = 0;
    bool _automatic_enabled = true;
};
```

In the `.cpp`, use only unsigned subtraction for elapsed time. Disabling the automatic policy and acquiring the first keep-awake reason return `Restore` only if the prior state was Dimmed or ScreenOff. Enabling the policy and releasing the final reason set `_inactivity_origin_ms = now_ms`. `userActivity()` always resets the origin; it consumes input only when the prior state was ScreenOff. `dimBrightness()` clamps its input to 10-100 and returns the smaller of that value and 20.

- [ ] **Step 4: Run the host test and verify GREEN**

Run the command from Step 2 and then `/tmp/display_protection_logic_test`.

Expected: exit code 0 and no output.

- [ ] **Step 5: Commit the pure policy**

```bash
git add main/runtime/display/display_protection_logic.h \
  main/runtime/display/display_protection_logic.cpp \
  tests/display_protection_logic_test.cpp
git commit -m "feat: add AMOLED protection policy"
```

### Task 2: HAL brightness ownership and wake-button gate

**Files:**
- Create: `main/hal/button_input_gate.h`
- Create: `tests/button_input_gate_test.cpp`
- Create: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`
- Modify: `main/hal/hal.h`
- Modify: `main/hal/hal_button.cpp`
- Modify: `main/hal/hal_display.cpp`

- [ ] **Step 1: Write failing tests for release gating and brightness contracts**

Create a host test that proves both buttons stay filtered until both are physically released:

```cpp
#include "hal/button_input_gate.h"
#include <cassert>

int main()
{
    ButtonInputGate gate;
    assert(gate.filter({true, false}).a_pressed);
    gate.suppressUntilRelease();
    assert(!gate.filter({true, false}).a_pressed);
    assert(gate.suppressing());
    assert(!gate.filter({false, true}).b_pressed);
    assert(gate.suppressing());
    const auto released = gate.filter({false, false});
    assert(!released.a_pressed && !released.b_pressed);
    assert(!gate.suppressing());
    assert(gate.filter({false, true}).b_pressed);
    return 0;
}
```

Add source assertions to `mac-codex-agent/tests/test_display_protection_firmware_contract.py` requiring:

```python
self.assertIn("void setTemporaryBackLightBrightness(int brightness)", hal_header)
self.assertIn("int getUserBackLightBrightness() const", hal_header)
self.assertIn("uint32_t backLightBrightnessRevision() const", hal_header)
self.assertIn("ButtonPhysicalState readButtonPhysicalState() const", hal_header)
self.assertIn("void suppressButtonInputUntilRelease()", hal_header)
self.assertIn("_button_input_gate.filter(readButtonPhysicalState())", button_source)
self.assertNotIn("Settings settings", temporary_brightness_body)
```

- [ ] **Step 2: Run both focused tests and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/button_input_gate_test.cpp -o /tmp/button_input_gate_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: missing header/API failures.

- [ ] **Step 3: Implement the gate and split effective from user brightness**

Use these dependency-free types:

```cpp
struct ButtonPhysicalState {
    bool a_pressed = false;
    bool b_pressed = false;
};

class ButtonInputGate {
public:
    void suppressUntilRelease() { _suppressing = true; }
    ButtonPhysicalState filter(ButtonPhysicalState raw)
    {
        if (!_suppressing) return raw;
        if (!raw.a_pressed && !raw.b_pressed) _suppressing = false;
        return {};
    }
    bool suppressing() const { return _suppressing; }
private:
    bool _suppressing = false;
};
```

Add to `Hal`:

```cpp
void setTemporaryBackLightBrightness(int brightness);
int getUserBackLightBrightness() const;
uint32_t backLightBrightnessRevision() const;
ButtonPhysicalState readButtonPhysicalState() const;
void suppressButtonInputUntilRelease();
bool buttonInputSuppressed() const;
```

Track `_user_bl_brightness`, effective `_bl_brightness`, `_bl_brightness_revision`, and `_button_input_gate`. `setBackLightBrightness()` updates both brightness values, increments the revision only when the user value changes, and optionally saves NVS. `setTemporaryBackLightBrightness()` updates only effective brightness and never saves or increments the revision. Replace direct GPIO reads in `updateButtonStates()` with the filtered physical state. Button SFX/vibration remains driven by the filtered `wasPressed()` state.

- [ ] **Step 4: Run focused tests and existing edge-control tests**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/button_input_gate_test.cpp -o /tmp/button_input_gate_test && \
  /tmp/button_input_gate_test
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/edge_control_logic_test.cpp \
  main/apps/common/edge_control/edge_control_logic.cpp \
  -o /tmp/edge_control_logic_test && /tmp/edge_control_logic_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_edge_control_firmware_contract -v
```

Expected: all pass.

- [ ] **Step 5: Commit HAL primitives**

```bash
git add main/hal/button_input_gate.h tests/button_input_gate_test.cpp \
  main/hal/hal.h main/hal/hal_button.cpp main/hal/hal_display.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: separate temporary display brightness"
```

### Task 3: Global display-protection service and main-loop ordering

**Files:**
- Create: `main/runtime/display/global_display_protection_service.h`
- Create: `main/runtime/display/global_display_protection_service.cpp`
- Modify: `main/main.cpp`
- Modify: `main/apps/common/edge_control/global_edge_control_service.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Add failing firmware integration contracts**

Require initialization and ordering:

```python
self.assertIn("GetGlobalDisplayProtectionService().init()", main)
protection = main.index("GetGlobalDisplayProtectionService().update()")
edge = main.index("GetGlobalEdgeControlService().update()")
mooncake = main.index("GetMooncake().update()")
self.assertLess(protection, edge)
self.assertLess(edge, mooncake)
self.assertIn("GetGlobalDisplayProtectionService().applyPendingHardwareUpdate()", main)
```

Require wake suppression and temporary writes:

```python
self.assertIn("lv_indev_wait_release(indev)", service)
self.assertIn("lv_indev_reset(indev, nullptr)", service)
self.assertIn("GetHAL().suppressButtonInputUntilRelease()", service)
self.assertIn("GetHAL().setTemporaryBackLightBrightness", service)
self.assertNotIn("setBackLightBrightness", service)
```

- [ ] **Step 2: Run the contract test and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: missing service and main-loop wiring failures.

- [ ] **Step 3: Implement the global service**

Expose this narrow API:

```cpp
struct DisplayProtectionSnapshot {
    DisplayProtectionState state = DisplayProtectionState::Active;
    int effective_brightness = 0;
    int restore_brightness = 80;
    bool automatic_enabled = true;
    uint8_t reason_mask = 0;
};

class GlobalDisplayProtectionService {
public:
    bool init();
    void update();
    void applyPendingHardwareUpdate();
    void shutdown();
    void setAutomaticProtection(bool enabled, bool restore_display = true);
    void setKeepAwake(DisplayKeepAwakeReason reason, bool active);
    DisplayProtectionSnapshot snapshot() const;
private:
    void acceptDecision(const DisplayProtectionDecision& decision,
                        bool restore_display = true);
    void syncUserBrightness();
    DisplayProtectionLogic _logic;
    int _restore_brightness = 80;
    int _pending_brightness = -1;
    uint32_t _brightness_revision = 0;
    lv_indev_state_t _last_touch_state = LV_INDEV_STATE_REL;
    ButtonPhysicalState _last_buttons{};
    bool _initialized = false;
};

GlobalDisplayProtectionService& GetGlobalDisplayProtectionService();
```

`init()` resets the policy with the current monotonic time, captures current input states, loads the current user brightness, and returns false with one error log when LVGL input is unavailable. `shutdown()` clears pending output and restores safe default state without allocating or starting a FreeRTOS task.

`update()` must:

1. sync `_restore_brightness` from the HAL user-brightness revision;
2. detect new physical button presses through `readButtonPhysicalState()`;
3. detect touch press transitions through `lv_indev_get_state()`;
4. call `userActivity()` before timeout `update()`;
5. on a ScreenOff wake, suppress public buttons or call LVGL wait-release/reset;
6. queue 20-or-lower dim brightness, zero off brightness, or clamped 10-100 restore brightness;
7. log only actual state transitions with state, reason, and mask.

`applyPendingHardwareUpdate()` uses `setTemporaryBackLightBrightness()` and clears the queue. `setKeepAwake()` and `setAutomaticProtection()` accept policy decisions immediately; callers that must wake synchronously may call `applyPendingHardwareUpdate()` before presenting content. `restore_display=false` is reserved for Codex's existing manual screen-off flow and changes policy state without queuing a brightness restore.

- [ ] **Step 4: Wire the service before edge controls and Mooncake**

Initialize it after HAL/LVGL initialization and before edge controls. Update it first under `LvglLockGuard`, then update edge controls. Outside the lock, apply protection hardware first and edge-control hardware second so a real user brightness gesture wins over a simultaneous wake restore:

```cpp
{
    LvglLockGuard lock;
    GetGlobalDisplayProtectionService().update();
    GetGlobalEdgeControlService().update();
}
GetGlobalDisplayProtectionService().applyPendingHardwareUpdate();
GetGlobalEdgeControlService().applyPendingHardwareUpdate();
GetMooncake().update();
```

Keep the existing edge-control zero-brightness suspension. Its temporary restoration path must use `setTemporaryBackLightBrightness(0)` so it cannot replace the saved user brightness.

- [ ] **Step 5: Run policy, gate, edge, and contract tests**

```bash
/tmp/display_protection_logic_test
/tmp/button_input_gate_test
c++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/edge_control_logic_test.cpp \
  main/apps/common/edge_control/edge_control_logic.cpp \
  -o /tmp/edge_control_logic_test && /tmp/edge_control_logic_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_edge_control_firmware_contract -v
```

Expected: all pass.

- [ ] **Step 6: Commit the global service**

```bash
git add main/runtime/display/global_display_protection_service.h \
  main/runtime/display/global_display_protection_service.cpp \
  main/main.cpp main/apps/common/edge_control/global_edge_control_service.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: enforce global AMOLED inactivity protection"
```

### Task 4: Codex external-power exception and manual-screen compatibility

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Write the failing Codex power-policy contract**

Require a one-second check, non-strict power detection, live policy changes, close restoration, and temporary manual brightness:

```python
self.assertIn("kPowerPolicyCheckMs = 1000", codex_source)
self.assertIn("GetHAL().isBatteryCharging(false)", codex_source)
self.assertIn("setAutomaticProtection(!external_power)", codex_source)
self.assertIn("refresh_display_protection_policy(now, true)", codex_source)
self.assertIn("refresh_display_protection_policy(now, false)", codex_source)
self.assertIn("setAutomaticProtection(true)", codex_close_body)
self.assertIn("setAutomaticProtection(false, false)", manual_off_body)
self.assertIn("setTemporaryBackLightBrightness(0)", manual_off_body)
self.assertIn("setTemporaryBackLightBrightness", wake_body)
```

- [ ] **Step 2: Run the contract test and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: missing Codex power-policy wiring.

- [ ] **Step 3: Add the power-aware policy**

Add fields:

```cpp
uint32_t _last_power_policy_check_ms = 0;
bool _external_power_connected = false;
bool _power_policy_known = false;
void refresh_display_protection_policy(uint32_t now_ms, bool force);
```

On open, force a power read after initializing `_screen_off`. In `onRunning()`, refresh once per second. Treat `isBatteryCharging(false) == false` as battery operation, including read failure. Only call `setAutomaticProtection()` when the derived policy changes. If Codex is manually screen-off, update the cached power state but keep automatic protection suspended until `wake_screen()`.

Before the existing manual zero-brightness write, call:

```cpp
GetGlobalDisplayProtectionService().setAutomaticProtection(false, false);
GetHAL().setTemporaryBackLightBrightness(0);
```

On manual wake, restore the cached user brightness temporarily and then apply the current power-derived policy. On close, wake if necessary and always call `setAutomaticProtection(true)` so the next app starts protected.

- [ ] **Step 4: Run Codex UI, Pending, and protection contracts**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_codex_pending_glow_firmware_contract \
  mac-codex-agent.tests.test_codex_status_f_layout_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test && /tmp/codex_pending_glow_logic_test
```

Expected: all pass.

- [ ] **Step 5: Commit Codex integration**

```bash
git add main/apps/app_codex_status/app_codex_status.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: protect Codex display on battery"
```

### Task 5: Assistant semantic activity hold with stale release

**Files:**
- Create: `main/apps/app_assistant/assistant_display_activity.h`
- Create: `tests/assistant_display_activity_test.cpp`
- Modify: `main/apps/app_assistant/app_assistant.h`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Write the failing Assistant activity test**

Use a pure tracker API and prove that visual idleness is not activity, listening/speaking are authoritative, response progress holds, completion releases, and a stale outstanding response expires after 60 seconds:

```cpp
#include "apps/app_assistant/assistant_display_activity.h"
#include <cassert>

int main()
{
    AssistantDisplayActivity activity;
    activity.reset();
    assert(!activity.active(0));
    activity.observe(AssistantDisplayActivitySignal::WakeDetected, true, 100);
    assert(activity.active(100));
    activity.observe(AssistantDisplayActivitySignal::Listening, true, 200);
    assert(activity.active(100000));
    activity.observe(AssistantDisplayActivitySignal::Listening, false, 100000);
    activity.observe(AssistantDisplayActivitySignal::UserText, true, 100100);
    assert(activity.active(160099));
    assert(!activity.active(160100));
    activity.observe(AssistantDisplayActivitySignal::Speaking, true, 170000);
    assert(activity.active(500000));
    activity.observe(AssistantDisplayActivitySignal::Speaking, false, 500001);
    assert(!activity.active(500001));
    activity.observe(AssistantDisplayActivitySignal::AgentText, true, 600000);
    assert(activity.active(600000));
    activity.observe(AssistantDisplayActivitySignal::Idle, true, 600001);
    assert(!activity.active(600001));
    return 0;
}
```

- [ ] **Step 2: Run the host test and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_display_activity_test.cpp \
  -o /tmp/assistant_display_activity_test
```

Expected: missing tracker header.

- [ ] **Step 3: Implement the tracker and publish only level changes**

Define:

```cpp
enum class AssistantDisplayActivitySignal : uint8_t {
    WakeDetected, Listening, UserText, AgentText, Emotion, Speaking, Idle
};

class AssistantDisplayActivity {
public:
    static constexpr uint32_t StaleAfterMs = 60000;
    void reset();
    void observe(AssistantDisplayActivitySignal signal, bool active, uint32_t now_ms);
    bool active(uint32_t now_ms) const;
private:
    bool _listening = false;
    bool _speaking = false;
    bool _response_outstanding = false;
    uint32_t _last_progress_ms = 0;
};
```

In `AppAssistant`, translate runtime events into tracker signals after close-event filtering using this exact mapping:

- `WakeDetected` -> `WakeDetected`;
- `ListeningStatus` -> `Listening` with `event.active`;
- `UserText` -> `UserText`;
- `AgentText` -> `AgentText`;
- non-empty `Emotion` -> `Emotion`;
- `SpeakingStatus` -> `Speaking` with `event.active`;
- runtime `Starting`, `Connecting`, `Running`, `Stopping`, or `Stopped`, any `Error`, retry reset, graceful-close request, and `onClose()` -> `Idle` unless a later event in the same drained batch establishes new active interaction.

After draining events and once per `onRunning()`, publish the tracker's current boolean through `setKeepAwake(AssistantInteraction, active)`. Keep the service call idempotent and clear it defensively on close.

- [ ] **Step 4: Run Assistant tests and contracts**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_display_activity_test.cpp \
  -o /tmp/assistant_display_activity_test && /tmp/assistant_display_activity_test
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_presentation_model_test.cpp \
  -o /tmp/assistant_presentation_model_test && /tmp/assistant_presentation_model_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_assistant_signal_core_ui_contract -v
```

Expected: all pass.

- [ ] **Step 5: Commit Assistant integration**

```bash
git add main/apps/app_assistant/assistant_display_activity.h \
  tests/assistant_display_activity_test.cpp \
  main/apps/app_assistant/app_assistant.h \
  main/apps/app_assistant/app_assistant.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: keep display awake during Assistant interaction"
```

### Task 6: Persistent Stopwatch Running hold

**Files:**
- Modify: `main/apps/app_stopwatch/view/view.h`
- Modify: `main/apps/app_stopwatch/view/view.cpp`
- Modify: `main/apps/app_clock/app_clock.h`
- Modify: `main/apps/app_clock/app_clock.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Add failing ownership and hold contracts**

```python
self.assertIn("std::unique_ptr<model::Stopwatch> _stopwatch", clock_header)
self.assertNotIn("std::unique_ptr<model::Stopwatch> _stopwatch", stopwatch_view_header)
self.assertIn("model::Stopwatch* _stopwatch", stopwatch_view_header)
self.assertIn("void init(lv_obj_t* parent, model::Stopwatch& stopwatch)", stopwatch_view_header)
self.assertIn("DisplayKeepAwakeReason::StopwatchRunning", clock_source)
self.assertIn("model::Stopwatch::State_t::Running", clock_source)
self.assertIn("setKeepAwake", clock_source)
```

- [ ] **Step 2: Run the contract test and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: Stopwatch ownership still belongs to the view.

- [ ] **Step 3: Move model ownership to AppClock and publish Running**

Create the Stopwatch model in `AppClock::onCreate()`. Change view initialization to accept a model reference and store a non-owning pointer. Do not reset the model when leaving the Stopwatch subpage or closing Clock.

Add:

```cpp
void AppClock::syncStopwatchKeepAwake()
{
    const bool running = _stopwatch &&
        _stopwatch->getState() == model::Stopwatch::State_t::Running;
    GetGlobalDisplayProtectionService().setKeepAwake(
        DisplayKeepAwakeReason::StopwatchRunning, running);
}
```

Call it after Stopwatch view updates, from `onSleeping()`, after transitions, and before closing. Do not clear a genuinely running Stopwatch in `onClose()`; clear it only in `onDestroy()` after destroying the model. This preserves timing and the hold after returning to Launcher.

- [ ] **Step 4: Run Clock, navigation, and protection contracts**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/clock_navigation_test.cpp main/apps/app_clock/clock_navigation.cpp \
  -o /tmp/clock_navigation_test && /tmp/clock_navigation_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_clock_firmware_contract \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: all pass.

- [ ] **Step 5: Commit Stopwatch integration**

```bash
git add main/apps/app_stopwatch/view/view.h \
  main/apps/app_stopwatch/view/view.cpp \
  main/apps/app_clock/app_clock.h main/apps/app_clock/app_clock.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: hold display for running Stopwatch"
```

### Task 7: Alarm wake-and-hold lifecycle

**Files:**
- Modify: `main/apps/app_clock/app_clock.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Add a failing alarm lifecycle contract**

Extract the `onTriggered()` callback body and assert this ordering:

```python
acquire = callback.index("DisplayKeepAwakeReason::AlarmRinging, true")
flush = callback.index("applyPendingHardwareUpdate()")
view = callback.index("std::make_unique<view::AlarmTriggerView>()")
release = callback.index("DisplayKeepAwakeReason::AlarmRinging, false")
self.assertLess(acquire, flush)
self.assertLess(flush, view)
self.assertLess(view, release)
self.assertIn("DisplayKeepAwakeReason::AlarmRinging, false", destroy_body)
```

- [ ] **Step 2: Run the contract and verify RED**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: no alarm keep-awake calls.

- [ ] **Step 3: Acquire before presentation and release after shutdown**

At the start of the alarm callback:

```cpp
auto& protection = GetGlobalDisplayProtectionService();
protection.setKeepAwake(DisplayKeepAwakeReason::AlarmRinging, true);
protection.applyPendingHardwareUpdate();
```

Only then create the ringing view and start audio. After confirmation, destroy the view, stop alarm audio, and release the reason. Add a defensive release in `onDestroy()`. Do not make ordinary scheduled alarms a hold before they actually trigger.

- [ ] **Step 4: Run focused Clock and protection tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_clock_firmware_contract \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: all pass.

- [ ] **Step 5: Commit alarm integration**

```bash
git add main/apps/app_clock/app_clock.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: wake display for ringing alarms"
```

### Task 8: Read-only diagnostics and standard validation gate

**Files:**
- Modify: `main/debug/debug_command_parser.h`
- Modify: `main/debug/debug_command_parser.cpp`
- Modify: `main/debug/debug_command_service.cpp`
- Modify: `tests/debug_command_test.cpp`
- Modify: `mac-codex-agent/tests/test_debug_command_firmware_contract.py`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing diagnostics parser and source tests**

Add to the native parser test:

```cpp
assert(ParseDebugCommand("debug display protection").kind ==
       DebugCommandKind::DisplayProtectionStatus);
```

Add firmware-contract assertions for one bounded metadata-only line:

```python
self.assertIn("DisplayProtectionStatus", parser_header)
self.assertIn("GetGlobalDisplayProtectionService().snapshot()", service)
self.assertIn("DISPLAY PROTECTION state=%s brightness=%d restore=%d automatic=%u reasons=0x%02x", service)
for forbidden in ("transcript", "payload", "audio data", "conversation"):
    self.assertNotIn(forbidden, display_status_body.lower())
```

- [ ] **Step 2: Run diagnostics tests and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/debug_command_test.cpp main/debug/debug_command_parser.cpp \
  main/debug/debug_app_control.cpp main/debug/debug_assistant_control.cpp \
  -o /tmp/debug_command_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_debug_command_firmware_contract -v
```

Expected: missing enum/command/log failures.

- [ ] **Step 3: Implement read-only status output**

Parse only the exact command `debug display protection`. Map the state enum to `active`, `dimmed`, or `off`, obtain one snapshot, and log:

```text
DISPLAY PROTECTION state=active brightness=80 restore=80 automatic=1 reasons=0x00
```

Do not add time injection or mutation commands.

- [ ] **Step 4: Add all three new host tests to the unified runner**

Renumber the runner stages and add explicit compile-and-run blocks for:

```bash
tests/display_protection_logic_test.cpp main/runtime/display/display_protection_logic.cpp
tests/button_input_gate_test.cpp
tests/assistant_display_activity_test.cpp
```

The final clean-build stage remains last.

- [ ] **Step 5: Run focused diagnostics and contract tests**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/debug_command_test.cpp main/debug/debug_command_parser.cpp \
  main/debug/debug_app_control.cpp main/debug/debug_assistant_control.cpp \
  -o /tmp/debug_command_test && /tmp/debug_command_test
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_debug_command_firmware_contract \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: all pass.

- [ ] **Step 6: Commit diagnostics and validation wiring**

```bash
git add main/debug/debug_command_parser.h main/debug/debug_command_parser.cpp \
  main/debug/debug_command_service.cpp tests/debug_command_test.cpp \
  mac-codex-agent/tests/test_debug_command_firmware_contract.py \
  scripts/run_unified_offline_validation.sh
git commit -m "test: cover AMOLED protection diagnostics"
```

### Task 9: Full verification and hardware-ready handoff

**Files:**
- Verify only; do not modify unrelated files.

- [ ] **Step 1: Run whitespace and exact-scope checks**

```bash
git diff --check
git status --short
```

Expected: no whitespace errors. Preserve `.superpowers/`, `hardware-evidence/`, and any unrelated user work exactly as found.

- [ ] **Step 2: Run the complete offline validation**

```bash
IDF_COMPONENT_CACHE_PATH=/tmp/stopwatch-amoled-protection-idf-cache \
PYTHON="$PWD/mac-codex-agent/.venv/bin/python" \
./scripts/run_unified_offline_validation.sh
```

Expected: every Python and native host test passes, clean unified firmware build succeeds, and the capacity gate passes.

- [ ] **Step 3: Inspect the final artifact**

```bash
python3 -m json.tool build-unified/unified-runtime-metrics.json
git describe --always --dirty
```

Expected: `capacity_gate_passed` is true. If unrelated user work remains in the shared checkout, record the expected `-dirty` build suffix instead of staging, discarding, or rewriting those files; otherwise the reported version must match `HEAD` without `-dirty`.

- [ ] **Step 4: Prepare but do not invent hardware evidence**

When the watch is connected, use the existing backup-first flash workflow, set volume to zero, and validate:

1. ordinary apps dim at 15 seconds and turn black at 60 seconds;
2. dimmed input both restores and performs its action;
3. the first off-screen touch/button wakes only, and the next input works;
4. Assistant stays awake only through ongoing interaction;
5. running Stopwatch stays awake after leaving its subpage and returning to Launcher;
6. an alarm wakes and remains visible until dismissal;
7. Codex stays awake on external power, protects on battery, and reacts to live plug/unplug;
8. Codex manual screen-off, Pending glow, global edge controls, brightness persistence, and application exit remain correct.

Record offline completion separately from physical validation. Do not claim display timing or wake-consumption success until observed on the device.
