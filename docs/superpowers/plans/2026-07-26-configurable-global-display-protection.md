# Configurable Global Display Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one configurable display-protection policy govern Launcher and every application, wake reliably on power transitions, and expose charging behavior plus dim/off timeouts in Settings.

**Architecture:** Keep timeout normalization and state transitions in host-testable pure C++ units. Move external-power sampling into a cached HAL tracker, then let `GlobalDisplayProtectionService` be the sole policy owner. Settings edits a draft configuration and commits it through the service; Codex delegates manual screen-off and display-state presentation to the same service.

**Tech Stack:** ESP-IDF 5.5.4, C++17, LVGL/smooth_ui_toolkit, M5PM1, NVS `Settings`, Python `unittest` firmware contracts, native host tests, ESP32-S3 unified build.

---

## File Map

New focused units:

- `main/runtime/display/display_protection_config.h/.cpp`: legal timeout choices, labels, sanitization, and directional slider linking.
- `main/hal/external_power_state.h`: pure three-state/debounce tracker shared by HAL and its host test.
- `main/apps/app_setup/workers/display_protection_settings_model.h`: draft model used by the Settings view.
- `main/apps/app_setup/workers/display_protection.cpp`: display-protection Settings view and worker.
- `tests/display_protection_config_test.cpp`: configuration and linked-choice tests.
- `tests/external_power_state_test.cpp`: insertion, removal debounce, and invalid-sample tests.
- `tests/display_protection_settings_model_test.cpp`: UI draft and linked-slider tests.

Existing units to modify:

- `main/runtime/display/display_protection_logic.h/.cpp`: configurable thresholds, manual off, timing diagnostics, and corrected keep-awake wake behavior.
- `main/runtime/display/global_display_protection_service.h/.cpp`: persistence, cached power transitions, effective policy, diagnostics, and sole ownership.
- `main/hal/hal.h` and `main/hal/hal_pmic.cpp`: thread-safe external-power cache and 250ms sampling.
- `main/apps/app_setup/app_setup.cpp` and `main/apps/app_setup/workers/workers.h`: menu entry and worker declaration.
- `main/apps/app_codex_status/app_codex_status.h/.cpp`: remove local power policy/screen state and delegate manual off.
- `main/debug/debug_command_service.cpp`: richer protection diagnostics.
- `tests/display_protection_logic_test.cpp` and Python contract tests: regression coverage.
- `scripts/run_unified_offline_validation.sh`: make all new native tests part of the standard gate.

## Execution Prerequisite

Use `superpowers:using-git-worktrees` before implementation. Create an isolated branch/worktree from the current `main`; do not reuse `build-unified` from another active task. Preserve root-level `.superpowers/`, `build-flash-latest/`, and `hardware-evidence/`. At the start of execution, record the immutable comparison point and both worktree roots:

```bash
BASE_COMMIT="$(git rev-parse main)"
IMPLEMENTATION_ROOT="$(git rev-parse --show-toplevel)"
MAIN_ROOT="$(git worktree list --porcelain | awk '/^worktree / { print substr($0, 10); exit }')"
```

Before the clean unified build, ensure `components/` and `managed_components/` in the isolated worktree are real dependency trees, not symlinks to a mutable active worktree.

### Task 1: Add the pure configuration model

**Files:**
- Create: `main/runtime/display/display_protection_config.h`
- Create: `main/runtime/display/display_protection_config.cpp`
- Create: `tests/display_protection_config_test.cpp`

- [ ] **Step 1: Write the failing configuration test**

```cpp
#include "runtime/display/display_protection_config.h"

#include <cassert>

int main()
{
    const auto defaults = defaultDisplayProtectionConfig();
    assert(defaults.dim_choice == 1);
    assert(defaults.screen_off_choice == 1);
    assert(!defaults.protect_while_charging);
    assert(displayProtectionDimMs(defaults) == 15000U);
    assert(displayProtectionScreenOffMs(defaults) == 60000U);
    assert(displayProtectionDimLabel(5) == "5m");
    assert(displayProtectionScreenOffLabel(4) == "10m");

    DisplayProtectionConfig invalid{99, 0, true};
    const auto repaired = sanitizeDisplayProtectionConfig(invalid);
    assert(repaired.dim_choice == 5);
    assert(repaired.screen_off_choice == 4);
    assert(repaired.protect_while_charging);

    auto linked = withDisplayProtectionDimChoice(defaults, 4);
    assert(linked.dim_choice == 4);
    assert(linked.screen_off_choice == 3);
    linked = withDisplayProtectionDimChoice(linked, 5);
    assert(linked.screen_off_choice == 4);

    linked = withDisplayProtectionScreenOffChoice(defaults, 0);
    assert(linked.screen_off_choice == 0);
    assert(linked.dim_choice == 1);
    return 0;
}
```

- [ ] **Step 2: Compile the test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_config_test.cpp \
  main/runtime/display/display_protection_config.cpp \
  -o /tmp/display_protection_config_test
```

Expected: compilation fails because the header and API do not exist.

- [ ] **Step 3: Implement legal choices and directional linking**

Define this public API:

```cpp
#pragma once

#include <cstdint>
#include <string_view>

struct DisplayProtectionConfig {
    uint8_t dim_choice = 1;
    uint8_t screen_off_choice = 1;
    bool protect_while_charging = false;
};

constexpr uint8_t DisplayProtectionDimChoiceCount = 6;
constexpr uint8_t DisplayProtectionScreenOffChoiceCount = 5;

DisplayProtectionConfig defaultDisplayProtectionConfig();
DisplayProtectionConfig sanitizeDisplayProtectionConfig(DisplayProtectionConfig config);
DisplayProtectionConfig withDisplayProtectionDimChoice(DisplayProtectionConfig config,
                                                       int choice);
DisplayProtectionConfig withDisplayProtectionScreenOffChoice(DisplayProtectionConfig config,
                                                             int choice);
uint32_t displayProtectionDimMs(const DisplayProtectionConfig& config);
uint32_t displayProtectionScreenOffMs(const DisplayProtectionConfig& config);
std::string_view displayProtectionDimLabel(int choice);
std::string_view displayProtectionScreenOffLabel(int choice);
```

Implement with fixed tables:

```cpp
constexpr std::array<uint32_t, 6> kDimMs{
    10000U, 15000U, 30000U, 60000U, 120000U, 300000U};
constexpr std::array<std::string_view, 6> kDimLabels{
    "10s", "15s", "30s", "1m", "2m", "5m"};
constexpr std::array<uint32_t, 5> kOffMs{
    30000U, 60000U, 120000U, 300000U, 600000U};
constexpr std::array<std::string_view, 5> kOffLabels{
    "30s", "1m", "2m", "5m", "10m"};
```

Clamp raw indices. Sanitization preserves the dim choice and advances screen-off to the first strictly greater value. `withDisplayProtectionScreenOffChoice` instead lowers dim to the greatest strictly smaller value.

- [ ] **Step 4: Compile and run GREEN**

Run the Step 2 command, then `/tmp/display_protection_config_test`.

Expected: exit 0 with no warnings.

- [ ] **Step 5: Commit the model**

```bash
git add main/runtime/display/display_protection_config.* \
  tests/display_protection_config_test.cpp
git commit -m "feat: model display protection settings"
```

### Task 2: Make the protection state machine configurable

**Files:**
- Modify: `main/runtime/display/display_protection_logic.h`
- Modify: `main/runtime/display/display_protection_logic.cpp`
- Modify: `tests/display_protection_logic_test.cpp`

- [ ] **Step 1: Add failing timing and manual-off assertions**

Add these scenarios to `tests/display_protection_logic_test.cpp`:

```cpp
logic.reset(1000);
logic.setTimeouts(30000U, 120000U, 1000U);
assert(logic.update(30999U).action == DisplayProtectionAction::None);
assert(logic.update(31000U).action == DisplayProtectionAction::Dim);
assert(logic.update(120999U).action == DisplayProtectionAction::None);
assert(logic.update(121000U).action == DisplayProtectionAction::ScreenOff);

logic.reset(0);
auto manual_off = logic.screenOffNow(100);
assert(manual_off.action == DisplayProtectionAction::ScreenOff);
assert(logic.state() == DisplayProtectionState::ScreenOff);
auto manual_wake = logic.userActivity(101);
assert(manual_wake.action == DisplayProtectionAction::Restore);
assert(manual_wake.consume_input);

logic.reset(0);
logic.setKeepAwake(DisplayKeepAwakeReason::StopwatchRunning, true, 10);
logic.screenOffNow(20);
auto alarm_wake = logic.setKeepAwake(
    DisplayKeepAwakeReason::AlarmRinging, true, 30);
assert(alarm_wake.action == DisplayProtectionAction::Restore);

logic.reset(UINT32_MAX - 5000U);
logic.setTimeouts(10000U, 30000U, UINT32_MAX - 5000U);
assert(logic.update(4999U).action == DisplayProtectionAction::Dim);
assert(logic.update(24999U).action == DisplayProtectionAction::ScreenOff);
```

- [ ] **Step 2: Run the current native test and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_logic_test.cpp \
  main/runtime/display/display_protection_logic.cpp \
  -o /tmp/display_protection_logic_test && \
  /tmp/display_protection_logic_test
```

Expected: compilation fails for missing `setTimeouts` and `screenOffNow`.

- [ ] **Step 3: Implement configurable timing and diagnostics accessors**

Add:

```cpp
struct DisplayProtectionTimingSnapshot {
    uint32_t inactivity_ms = 0;
    uint32_t dim_after_ms = 15000;
    uint32_t screen_off_after_ms = 60000;
    uint32_t dim_remaining_ms = 0;
    uint32_t screen_off_remaining_ms = 0;
};

DisplayProtectionDecision setTimeouts(uint32_t dim_after_ms,
                                      uint32_t screen_off_after_ms,
                                      uint32_t now_ms);
DisplayProtectionDecision screenOffNow(uint32_t now_ms);
DisplayProtectionTimingSnapshot timingSnapshot(uint32_t now_ms) const;
```

Store `_dim_after_ms` and `_screen_off_after_ms` per instance. `setTimeouts` clamps screen-off to be at least one millisecond later, resets the inactivity origin, and restores if currently dim/off. `screenOffNow` sets `ScreenOff` regardless of automatic/keep-awake state. Compute remaining time with saturating subtraction and existing wrap-safe unsigned elapsed arithmetic.

Change `setKeepAwake`: whenever a previously inactive reason becomes active and the state is not `Active`, restore. Do not require the old mask to be zero.

- [ ] **Step 4: Verify GREEN and existing behavior**

Run the Step 2 command.

Expected: exit 0; existing 15-second/60-second defaults still pass.

- [ ] **Step 5: Commit state-machine behavior**

```bash
git add main/runtime/display/display_protection_logic.* \
  tests/display_protection_logic_test.cpp
git commit -m "feat: configure display protection timing"
```

### Task 3: Add reliable cached external-power state

**Files:**
- Create: `main/hal/external_power_state.h`
- Create: `tests/external_power_state_test.cpp`
- Modify: `main/hal/hal.h`
- Modify: `main/hal/hal_pmic.cpp`

- [ ] **Step 1: Write the failing tracker test**

```cpp
#include "hal/external_power_state.h"

#include <cassert>

int main()
{
    ExternalPowerTracker tracker;
    assert(tracker.snapshot().state == ExternalPowerState::Unknown);
    assert(!tracker.snapshot().latest_sample_valid);

    tracker.observe(false, false);
    assert(tracker.snapshot().state == ExternalPowerState::Unknown);

    tracker.observe(true, false);
    tracker.observe(true, false);
    auto boot_battery = tracker.observe(true, false);
    assert(boot_battery.changed);
    assert(boot_battery.snapshot.state == ExternalPowerState::Battery);

    auto inserted = tracker.observe(true, true);
    assert(inserted.changed);
    assert(inserted.snapshot.state == ExternalPowerState::ExternalPower);

    tracker.observe(true, false);
    tracker.observe(false, false);
    assert(!tracker.snapshot().latest_sample_valid);
    tracker.observe(true, false);
    tracker.observe(true, false);
    assert(tracker.snapshot().state == ExternalPowerState::ExternalPower);
    tracker.observe(true, true);
    assert(tracker.snapshot().state == ExternalPowerState::ExternalPower);

    tracker.observe(true, false);
    tracker.observe(true, false);
    auto removed = tracker.observe(true, false);
    assert(removed.changed);
    assert(removed.snapshot.state == ExternalPowerState::Battery);
    assert(removed.snapshot.revision == inserted.snapshot.revision + 1);
    return 0;
}
```

- [ ] **Step 2: Compile and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/external_power_state_test.cpp \
  -o /tmp/external_power_state_test
```

Expected: missing-header failure.

- [ ] **Step 3: Implement the pure tracker**

Use this API:

```cpp
enum class ExternalPowerState : uint8_t { Unknown, Battery, ExternalPower };

struct ExternalPowerSnapshot {
    ExternalPowerState state = ExternalPowerState::Unknown;
    uint32_t revision = 0;
    bool latest_sample_valid = false;
};

struct ExternalPowerUpdate {
    ExternalPowerSnapshot snapshot;
    bool changed = false;
};

class ExternalPowerTracker {
public:
    ExternalPowerUpdate observe(bool valid, bool inserted);
    ExternalPowerSnapshot snapshot() const;
private:
    ExternalPowerSnapshot _snapshot;
    uint8_t _battery_confirmations = 0;
};
```

Invalid samples retain the accepted state, mark `latest_sample_valid` false, clear removal confirmations, and do not increment the state revision. A valid inserted sample transitions immediately, marks the latest sample valid, and clears removal confirmations. Three consecutive valid absent samples transition to `Battery`. A valid inserted sample between absent samples resets the counter. This makes “consecutive” literal while keeping policy stable across I2C failures.

- [ ] **Step 4: Verify tracker GREEN**

Run the Step 2 command and `/tmp/external_power_state_test`.

- [ ] **Step 5: Add the thread-safe HAL cache with a failing firmware contract first**

Extend `mac-codex-agent/tests/test_display_protection_firmware_contract.py` to require:

```python
self.assertIn("ExternalPowerSnapshot getExternalPowerSnapshot()", hal_header)
self.assertIn("constexpr uint32_t _power_sample_period_ms = 250", pmic_source)
self.assertIn("_external_power_tracker.observe", pmic_source)
self.assertIn("readVin", pmic_source)
```

Run the focused Python test and expect failure.

- [ ] **Step 6: Integrate sampling in `bat_reading_task`**

Add a mutex-protected `ExternalPowerTracker`. Change the task cadence to 250ms. Read VIN every iteration and publish valid/invalid observations. Read battery voltage every fourth iteration so battery percentage remains on its one-second cadence.

Expose:

```cpp
ExternalPowerSnapshot Hal::getExternalPowerSnapshot();
```

For `isBatteryCharging(false)`, return whether the cached state is `ExternalPower`. For `isBatteryCharging(true)`, require that cached external power is present and a fresh `PMG2_CHG_STAT` GPIO read reports active charging; return false if the GPIO read fails.

- [ ] **Step 7: Run host and contract tests GREEN**

Run `/tmp/external_power_state_test` and the focused Python contract test.

- [ ] **Step 8: Commit cached power state**

```bash
git add main/hal/external_power_state.h main/hal/hal.h main/hal/hal_pmic.cpp \
  tests/external_power_state_test.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: cache external power state"
```

### Task 4: Make the global service the policy owner

**Files:**
- Modify: `main/runtime/display/global_display_protection_service.h`
- Modify: `main/runtime/display/global_display_protection_service.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Replace the old Codex-policy contract with a failing service-ownership contract**

Require these service tokens:

```python
for token in (
    'Settings settings("disp_guard")',
    'GetInt("dim_idx"',
    'GetInt("off_idx"',
    'GetBool("chg_guard"',
    'SetInt("dim_idx"',
    'SetInt("off_idx"',
    'SetBool("chg_guard"',
    "getExternalPowerSnapshot()",
    "screenOffNow()",
    "applyConfig(const DisplayProtectionConfig& config, bool persist)",
):
    self.assertIn(token, service_header + service_source)
```

Also assert that all three NVS keys are at most 15 characters and that the service contains the effective charging-policy calculation. Defer the Codex absence assertions to Task 6 so Task 4 can reach GREEN while the temporary compatibility API still exists.

Run the focused Python test. Expected: FAIL because persistence and policy ownership are absent.

- [ ] **Step 2: Expand the service API and snapshot**

Define:

```cpp
enum class DisplayWakeReason : uint8_t {
    Initialization,
    Button,
    Touch,
    PowerInserted,
    PowerRemoved,
    ConfigurationChanged,
    KeepAwakeActivated,
};

struct DisplayProtectionSnapshot {
    DisplayProtectionState state;
    int effective_brightness;
    int restore_brightness;
    bool automatic_enabled;
    uint8_t reason_mask;
    DisplayProtectionConfig config;
    ExternalPowerSnapshot external_power;
    DisplayProtectionTimingSnapshot timing;
    DisplayWakeReason last_wake_reason;
};

const DisplayProtectionConfig& config() const;
void applyConfig(const DisplayProtectionConfig& config, bool persist);
void screenOffNow();
```

Remove public `setAutomaticProtection` after Codex migration is staged in Task 6; until then retain the existing public compatibility API so every intermediate commit builds.

Expose `automatic_enabled` as the effective value seen by diagnostics: the power/configuration gate must be enabled and the keep-awake mask must be empty. Keep the raw power/configuration gate internal to the logic so clearing the final keep-awake reason can restart timing without losing policy.

- [ ] **Step 3: Load and sanitize NVS at initialization**

Read `dim_idx`, `off_idx`, and `chg_guard` from `Settings("disp_guard")`, sanitize them, apply timeouts, read the cached external-power snapshot, and derive effective automatic state. Unknown power uses battery policy.

- [ ] **Step 4: Handle power and input in deterministic order**

At the start of `update`, capture whether the display was off. Refresh the cached snapshot every loop so diagnostic sample health stays current, but process wake policy only when the accepted-state revision changes. Treat `Battery`→`ExternalPower` as insertion and `ExternalPower`→`Battery` as removal. A first known `ExternalPower` state after `Unknown` is also an insertion and wakes; a first known `Battery` state only adopts battery policy and preserves the initialization timer because no removal occurred. Every accepted insertion/removal:

1. sync latest user brightness;
2. restore/reset inactivity;
3. set the wake reason;
4. recalculate automatic protection.

Then process button/touch edges. If input began while the display was off at loop start, suppress it even when power restoration already changed state to active. Reuse one helper for button suppression and LVGL wait/reset.

- [ ] **Step 5: Implement configuration application and manual off**

`applyConfig` sanitizes, optionally writes all three short NVS keys, restores/reset timing, applies new timeouts, and recalculates effective policy. `screenOffNow` delegates to logic and applies temporary brightness zero without modifying the saved user brightness.

Every restore path calls `syncUserBrightness()` first so it restores the latest saved brightness.

- [ ] **Step 6: Verify the focused contract GREEN**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

Expected: service ownership checks pass; Codex-removal checks remain for Task 6.

- [ ] **Step 7: Commit global policy ownership**

```bash
git add main/runtime/display/global_display_protection_service.* \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "feat: centralize display protection policy"
```

### Task 5: Add the Settings draft model and page

**Files:**
- Create: `main/apps/app_setup/workers/display_protection_settings_model.h`
- Create: `main/apps/app_setup/workers/display_protection.cpp`
- Create: `tests/display_protection_settings_model_test.cpp`
- Modify: `main/apps/app_setup/workers/workers.h`
- Modify: `main/apps/app_setup/app_setup.cpp`
- Create: `mac-codex-agent/tests/test_display_protection_settings_firmware_contract.py`

- [ ] **Step 1: Write the failing draft-model test**

```cpp
#include "apps/app_setup/workers/display_protection_settings_model.h"

#include <cassert>

int main()
{
    DisplayProtectionSettingsModel model(defaultDisplayProtectionConfig());
    model.setDimChoice(4);
    assert(model.config().dim_choice == 4);
    assert(model.config().screen_off_choice == 3);
    model.setDimChoice(5);
    assert(model.config().screen_off_choice == 4);
    model.setScreenOffChoice(0);
    assert(model.config().screen_off_choice == 0);
    assert(model.config().dim_choice == 1);
    model.setProtectWhileCharging(true);
    assert(model.config().protect_while_charging);
    return 0;
}
```

- [ ] **Step 2: Compile and verify RED**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_settings_model_test.cpp \
  main/runtime/display/display_protection_config.cpp \
  -o /tmp/display_protection_settings_model_test
```

Expected: missing model header.

- [ ] **Step 3: Implement the header-only draft model**

```cpp
class DisplayProtectionSettingsModel {
public:
    explicit DisplayProtectionSettingsModel(DisplayProtectionConfig config)
        : _config(sanitizeDisplayProtectionConfig(config)) {}
    const DisplayProtectionConfig& config() const { return _config; }
    void setDimChoice(int choice) {
        _config = withDisplayProtectionDimChoice(_config, choice);
    }
    void setScreenOffChoice(int choice) {
        _config = withDisplayProtectionScreenOffChoice(_config, choice);
    }
    void setProtectWhileCharging(bool enabled) {
        _config.protect_while_charging = enabled;
    }
private:
    DisplayProtectionConfig _config;
};
```

Run the Step 2 command and binary. Expected: PASS.

- [ ] **Step 4: Write a failing firmware UI contract**

Require:

- `Display Protection` immediately after `Brightness` in the Device menu;
- `Charging Protection` switch;
- two `Slider` instances with index ranges `0..5` and `0..4`;
- labels built from the configuration label functions;
- existing colors `0x343434`, `0x4AD78C`, and white knobs;
- a 374-pixel-wide green `OK` button;
- Settings draft changes calling only the model;
- `GetGlobalDisplayProtectionService().applyConfig(..., true)` only after save.

Run the new Python contract. Expected: FAIL.

- [ ] **Step 5: Declare and register `DisplayProtectionWorker`**

Add the worker to `workers.h`, including a private `DisplayProtectionConfigView`. In `AppSetup::onOpen`, insert the menu item after Brightness and instantiate the worker.

- [ ] **Step 6: Implement the approved single-page view**

Use a 466x466 black non-scrollable panel. Place:

- a 374x60 charging switch row at y=24, label font 22, 80x44 switch;
- a centered `Dim After: <label>` at y=110, font 20;
- a 374x20 dim slider at y=150;
- a centered `Screen Off After: <label>` at y=205, font 20;
- a 374x20 off slider at y=245; and
- the existing 374x130, radius-77 green OK button aligned center y=175.

Both sliders use gray main track, green indicator, and centered white round knob. On either value change, update the draft model, then synchronize both slider positions and labels under a `_syncing_controls` guard.

- [ ] **Step 7: Commit only on OK**

Construct the model from `GetGlobalDisplayProtectionService().config()`. The worker makes no runtime calls during edits. On save:

```cpp
GetGlobalDisplayProtectionService().applyConfig(_view->config(), true);
_is_done = true;
```

Destroying or closing the worker without OK discards the view and draft with no rollback call because runtime state never changed.

- [ ] **Step 8: Run model and UI contracts GREEN**

Run the native model command and:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_settings_firmware_contract -v
```

- [ ] **Step 9: Commit Settings UI**

```bash
git add main/apps/app_setup/app_setup.cpp \
  main/apps/app_setup/workers/workers.h \
  main/apps/app_setup/workers/display_protection.cpp \
  main/apps/app_setup/workers/display_protection_settings_model.h \
  tests/display_protection_settings_model_test.cpp \
  mac-codex-agent/tests/test_display_protection_settings_firmware_contract.py
git commit -m "feat: configure global display protection"
```

### Task 6: Migrate Codex manual off and glow state

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_codex_task_navigation_firmware_contract.py`

- [ ] **Step 1: Write failing Codex ownership assertions**

Assert Codex no longer contains:

```python
for forbidden in (
    "kPowerPolicyCheckMs",
    "isBatteryCharging(false)",
    "setAutomaticProtection(",
    "_external_power_connected",
    "_power_policy_known",
    "_screen_off_restore_brightness",
):
    self.assertNotIn(forbidden, codex_source_or_header)
```

Require `GetGlobalDisplayProtectionService().screenOffNow()` and global snapshot state when synchronizing Pending glow. Run both focused contracts and expect failure.

- [ ] **Step 2: Remove local policy and screen brightness ownership**

Delete the one-second PMIC policy refresh, local restore brightness, local wake method, and calls that reset automatic protection on close.

Keep long-left-button detection. Once the hold reaches two seconds, call global `screenOffNow()`, set `_suppress_task_navigation`, and do not issue another manual-off request until both buttons are released.

- [ ] **Step 3: Derive Codex presentation from the global snapshot**

In `sync_pending_glow`, pass:

```cpp
const bool screen_off =
    GetGlobalDisplayProtectionService().snapshot().state ==
    DisplayProtectionState::ScreenOff;
_pending_glow->set_state(_alert_carousel.display_state(), screen_off);
```

Rely on the global button/touch gate for wake consumption. Retain `_suppress_task_navigation` so the release following a manual-off hold or wake cannot rotate tasks.

- [ ] **Step 4: Remove `setAutomaticProtection` from the public global API**

After Codex compiles without it, delete the method if no remaining caller exists:

```bash
rg -n "setAutomaticProtection" main
```

Expected before deletion: only its declaration/definition. Expected after deletion: no matches.

- [ ] **Step 5: Run focused tests GREEN**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_codex_task_navigation_firmware_contract \
  mac-codex-agent.tests.test_codex_pending_glow_firmware_contract -v
```

- [ ] **Step 6: Commit Codex migration**

```bash
git add main/apps/app_codex_status/app_codex_status.* \
  main/runtime/display/global_display_protection_service.* \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py \
  mac-codex-agent/tests/test_codex_task_navigation_firmware_contract.py
git commit -m "refactor: delegate Codex screen protection"
```

### Task 7: Expand protection diagnostics

**Files:**
- Modify: `main/debug/debug_command_service.cpp`
- Modify: `mac-codex-agent/tests/test_debug_command_firmware_contract.py`

- [ ] **Step 1: Write failing diagnostic contract assertions**

Require the debug response to include these named fields:

```text
state= effective= user= automatic= power= power_valid=
dim_choice= off_choice= dim_ms= off_ms= idle_ms=
dim_remaining_ms= off_remaining_ms=
charging_guard= reasons= last_wake=
```

Run the focused Python test. Expected: FAIL because the current output exposes only state, brightness, automatic, and reasons.

- [ ] **Step 2: Add stable enum-to-string helpers and formatted output**

Map every `DisplayProtectionState`, `ExternalPowerState`, and `DisplayWakeReason` value to a fixed ASCII name with an `unknown` fallback. Format choice indices and millisecond values as unsigned integers so automation can parse them without localized units. `automatic` reports the effective state after both power/configuration policy and keep-awake reasons are applied; `power_valid` reports the health of the latest VIN sample without discarding the last accepted power state.

Keep the existing command name `debug display protection` and a single-line response prefix so existing tools remain compatible.

- [ ] **Step 3: Add rate-limited transition logging**

Log accepted power transitions, configuration commits, and display-state actions. Do not log successful 250ms samples. Log invalid-sample health only on healthy→unhealthy and unhealthy→healthy changes or at a rate-limited retry interval.

- [ ] **Step 4: Run the focused debug contract GREEN**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_debug_command_firmware_contract -v
```

- [ ] **Step 5: Commit diagnostics**

```bash
git add main/debug/debug_command_service.cpp \
  mac-codex-agent/tests/test_debug_command_firmware_contract.py
git commit -m "feat: diagnose display protection policy"
```

### Task 8: Integrate all new host tests into the standard gate

**Files:**
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`

- [ ] **Step 1: Write a failing runner contract**

Extend the Python contract to require the runner names and compiles:

- `display_protection_config_test.cpp`;
- `display_protection_logic_test.cpp` with `display_protection_config.cpp` where needed;
- `external_power_state_test.cpp`; and
- `display_protection_settings_model_test.cpp`.

Run the contract and expect failure before changing the script.

- [ ] **Step 2: Expand the runner from 29 to 32 stages**

Keep stages 1–23 unchanged, then use:

```text
[24/32] Display protection configuration host test
[25/32] AMOLED protection state machine host test
[26/32] External power tracker host test
[27/32] Display protection settings model host test
[28/32] Button input wake gate host test
[29/32] Assistant display activity host test
[30/32] Codex status UI host tests
[31/32] Trae status UI layout host test
[32/32] Isolated unified candidate clean build and capacity gates
```

Compile each new test with `-std=c++17 -Wall -Wextra -Werror -Imain` and only its required source files.

- [ ] **Step 3: Run the focused runner contract GREEN**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract -v
```

- [ ] **Step 4: Commit validation integration**

```bash
git add scripts/run_unified_offline_validation.sh \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py
git commit -m "test: gate configurable display protection"
```

### Task 9: Run merged-source verification and review

**Files:**
- Review: all files changed since the implementation branch base

- [ ] **Step 1: Run focused native tests**

Run the four native commands introduced in Tasks 1, 2, 3, and 5.

Expected: all exit 0 with `-Werror`.

- [ ] **Step 2: Run focused Python contracts**

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_display_protection_settings_firmware_contract \
  mac-codex-agent.tests.test_codex_task_navigation_firmware_contract \
  mac-codex-agent.tests.test_codex_pending_glow_firmware_contract \
  mac-codex-agent.tests.test_debug_command_firmware_contract -v
```

Expected: all pass.

- [ ] **Step 3: Run the full offline gate in the isolated worktree**

```bash
./scripts/run_unified_offline_validation.sh
```

Expected: all 32 stages pass, the firmware links, and capacity gates report true. Do not share this worktree's `build-unified` with another task.

- [ ] **Step 4: Review for policy leaks and storage mistakes**

```bash
rg -n "setAutomaticProtection|isBatteryCharging\(false\)" main/apps
rg -n 'dim_idx|off_idx|chg_guard|disp_guard' main
git diff --check "$BASE_COMMIT"...HEAD
```

Expected: no application owns automatic protection; all four NVS names are present and no key exceeds 15 characters; whitespace check passes.

- [ ] **Step 5: Request code review and address findings**

Use `superpowers:requesting-code-review` against the recorded base commit. Fix any correctness finding with a new failing regression test before changing production code.

### Task 10: Flash and execute hardware verification

**Files:**
- Create: timestamped files under `hardware-evidence/`
- Use: `scripts/backup_watch_flash.sh`
- Use: `scripts/flash_unified_candidate.sh`
- Use: `scripts/validate_watch_boot.py`
- Use: `scripts/watch_debug.py`

- [ ] **Step 1: Detect one usable watch port and mute test audio**

```bash
PORTS=(/dev/cu.usbmodem*(N))
(( ${#PORTS[@]} == 1 ))
PORT="${PORTS[1]}"
python3 scripts/watch_debug.py --port "$PORT" volume 0
```

Expected: exactly one intended watch port and `DEBUG VOLUME 0 OK`. If the debug command is unavailable before flashing, mute immediately after boot validation instead.

- [ ] **Step 2: Back up the complete 16MiB flash**

```bash
source scripts/resolve_esptool_python.sh
BUILD_DIR="$IMPLEMENTATION_ROOT/build-unified"
ESPTOOL_PYTHON="$(resolve_esptool_python "$BUILD_DIR")"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="$MAIN_ROOT/hardware-evidence/firmware-backups/${TIMESTAMP}-before-display-protection-settings.bin"
mkdir -p "$(dirname "$BACKUP")"
PYTHON="$ESPTOOL_PYTHON" ./scripts/backup_watch_flash.sh \
  --port "$PORT" \
  --output "$BACKUP"
```

Expected: 16,777,216-byte image and a valid `.sha256` sidecar.

- [ ] **Step 3: Flash the verified isolated build**

```bash
./scripts/flash_unified_candidate.sh \
  --port "$PORT" \
  --backup "$BACKUP" \
  --build-dir "$BUILD_DIR"
```

Expected: bootloader, partition table, app, and assets each report verified hashes.

- [ ] **Step 4: Validate boot and reapply volume zero**

```bash
BOOT_LOG="$MAIN_ROOT/hardware-evidence/${TIMESTAMP}-display-protection-boot.log"
BOOT_SUMMARY="$MAIN_ROOT/hardware-evidence/${TIMESTAMP}-display-protection-boot.json"
"$ESPTOOL_PYTHON" scripts/validate_watch_boot.py \
  --port "$PORT" --timeout 90 \
  --log "$BOOT_LOG" \
  --summary "$BOOT_SUMMARY"
python3 scripts/watch_debug.py --port "$PORT" volume 0
```

Expected: Launcher opens, runtime services start, and no reset, panic, or watchdog is reported.

- [ ] **Step 5: Verify the confirmed behavior matrix**

Record observations for:

1. battery defaults: dim at 15 seconds and off at 60 seconds, plus one non-default timeout pair;
2. screen-off insertion: wake within about 250ms;
3. connected default: no automatic dim/off;
4. removal: normal brightness, fresh 15/60-second timer;
5. charging protection enabled: connected watch follows selected thresholds;
6. Codex manual-off while connected: remains off until input or a later power transition;
7. Codex manual-off followed by insertion: wakes;
8. first wake input does not navigate or change brightness/volume;
9. Launcher, Settings, Codex, Assistant, Clock, and another application all obey the same automatic policy;
10. Assistant interaction, Stopwatch timing, and Alarm ringing each retain keep-awake behavior;
11. Alarm wakes a manually dark display while Stopwatch already holds its keep-awake reason;
12. a user brightness change is restored after dim, screen-off, insertion, and removal;
13. connector bounce and injected invalid VIN reads do not flap accepted power policy; and
14. saved settings survive reset while canceled edits change neither runtime behavior nor NVS;
15. linked timeout changes keep screen-off strictly later than dim in both slider directions; and
16. the new subpage visually matches existing Settings typography, spacing, switch, slider colors, centered knobs, and green OK button.

- [ ] **Step 6: Finish the branch only after evidence is complete**

Use `superpowers:verification-before-completion`, then `superpowers:finishing-a-development-branch`. Keep flash/hash/boot evidence distinct from manual visual behavior in the final report.
