# Power LED Blink Interval Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persisted 1-10 second Power LED blink interval slider with live preview and cancel restoration.

**Architecture:** A small host-testable helper owns interval bounds and conversion. `Hal` owns the persisted interval and restarts blink scheduling when it changes. `PowerLedWorker` owns the Settings presentation and treats mode plus interval as one previewable configuration.

**Tech Stack:** C++23, ESP-IDF 5.5.4, M5PM1, LVGL/smooth_ui_toolkit, Python unittest contract tests.

---

### Task 1: Host-Tested Blink Interval Rules

**Files:**
- Create: `main/hal/power_led_blink_config.h`
- Create: `tests/power_led_blink_config_test.cpp`

- [ ] **Step 1: Write the failing host test**

```cpp
#include "hal/power_led_blink_config.h"
#include <cassert>

int main()
{
    assert(normalize_power_led_blink_interval_seconds(0) == 1);
    assert(normalize_power_led_blink_interval_seconds(2) == 2);
    assert(normalize_power_led_blink_interval_seconds(11) == 10);
    assert(power_led_blink_interval_ms(3) == 3000U);
    return 0;
}
```

- [ ] **Step 2: Run the host test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/power_led_blink_config_test.cpp -o /tmp/power_led_blink_config_test
```

Expected: compilation fails because `hal/power_led_blink_config.h` does not exist.

- [ ] **Step 3: Implement the interval helper**

```cpp
#pragma once
#include <cstdint>

constexpr int PowerLedBlinkIntervalMinSeconds = 1;
constexpr int PowerLedBlinkIntervalMaxSeconds = 10;
constexpr int PowerLedBlinkIntervalDefaultSeconds = 2;

constexpr int normalize_power_led_blink_interval_seconds(int seconds)
{
    return seconds < PowerLedBlinkIntervalMinSeconds
               ? PowerLedBlinkIntervalMinSeconds
               : (seconds > PowerLedBlinkIntervalMaxSeconds ? PowerLedBlinkIntervalMaxSeconds : seconds);
}

constexpr uint32_t power_led_blink_interval_ms(int seconds)
{
    return static_cast<uint32_t>(normalize_power_led_blink_interval_seconds(seconds)) * 1000U;
}
```

- [ ] **Step 4: Run the host test and verify GREEN**

Run the compile command from Step 2, then run `/tmp/power_led_blink_config_test`.

Expected: both commands exit 0.

- [ ] **Step 5: Commit**

```bash
git add main/hal/power_led_blink_config.h tests/power_led_blink_config_test.cpp
git commit -m "test: define power LED blink interval rules"
```

### Task 2: Persist And Schedule The Selected Interval

**Files:**
- Modify: `mac-codex-agent/tests/test_power_led_settings_firmware_contract.py`
- Modify: `main/hal/hal.h`
- Modify: `main/hal/hal_pmic.cpp`

- [ ] **Step 1: Extend the firmware contract test**

Add assertions for:

```python
self.assertIn("void setPowerLedBlinkIntervalSeconds(int seconds", self.hal_header)
self.assertIn("int getPowerLedBlinkIntervalSeconds", self.hal_header)
self.assertIn('GetInt("pwr_led_interval_s", PowerLedBlinkIntervalDefaultSeconds)', self.hal_pmic)
self.assertIn('SetInt("pwr_led_interval_s"', self.hal_pmic)
self.assertIn("power_led_blink_interval_ms(_power_led_blink_interval_seconds)", self.hal_pmic)
```

Replace the old assertion for a fixed `_power_led_blink_period_ms = 2000` constant with assertions for the default interval helper and configurable schedule expression.

- [ ] **Step 2: Run the contract test and verify RED**

Run:

```bash
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_power_led_settings_firmware_contract.py'
```

Expected: the new interval API and persistence assertions fail.

- [ ] **Step 3: Add the HAL interval API and state**

Add to `Hal`:

```cpp
void setPowerLedBlinkIntervalSeconds(int seconds, bool saveToSettings = false);
int getPowerLedBlinkIntervalSeconds(bool loadFromSettings = false);
int _power_led_blink_interval_seconds = PowerLedBlinkIntervalDefaultSeconds;
```

Include `power_led_blink_config.h` from `hal.h`.

- [ ] **Step 4: Implement load, save, clamping, and schedule restart**

Use `pwr_led_interval_s` in the existing `system` namespace. Load the interval before loading the mode in `pmic_init()`. Replace the fixed 2000ms deadline with:

```cpp
_power_led_next_blink_ms = now + power_led_blink_interval_ms(_power_led_blink_interval_seconds);
```

When the interval changes while mode is Blink, call `setPowerLedMode(PowerLedMode::Blink, false)` to restart the preview schedule.

- [ ] **Step 5: Run host and contract tests**

Run:

```bash
/tmp/power_led_blink_config_test
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_power_led_settings_firmware_contract.py'
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add main/hal/hal.h main/hal/hal_pmic.cpp mac-codex-agent/tests/test_power_led_settings_firmware_contract.py
git commit -m "feat: persist power LED blink interval"
```

### Task 3: Add The Conditional Interval Slider

**Files:**
- Modify: `mac-codex-agent/tests/test_power_led_settings_firmware_contract.py`
- Modify: `main/apps/app_setup/workers/workers.h`
- Modify: `main/apps/app_setup/workers/device.cpp`

- [ ] **Step 1: Add failing UI and worker contract assertions**

Require these behaviors:

```python
self.assertIn('createModeRow(160, "Blink", Hal::PowerLedMode::Blink)', self.device_worker)
self.assertIn("_interval_slider->setRange(1, 10, false);", self.device_worker)
self.assertIn('std::snprintf(buffer, sizeof(buffer), "Interval: %ds",', self.device_worker)
self.assertIn("setHidden(_current_mode != Hal::PowerLedMode::Blink)", self.device_worker)
self.assertIn("GetHAL().setPowerLedBlinkIntervalSeconds", self.device_worker)
self.assertIn("_original_interval_seconds", self.workers_header)
self.assertIn("_applied_interval_seconds", self.workers_header)
```

- [ ] **Step 2: Run the contract test and verify RED**

Run the Task 2 Python command.

Expected: slider, visibility, and worker-state assertions fail.

- [ ] **Step 3: Build the compact Power LED layout**

Use three 374x60 mode rows at y=28, 94, and 160. Add a label at y=232 and a 374x20 slider centered at y=278. Style the slider with the same dark track, green indicator, and white knob used by `PercentageAdjustView`. Keep the existing 374x130 OK button at center y=175.

- [ ] **Step 4: Implement conditional visibility and live label updates**

Set range 1-10, initialize from HAL, update `Interval: Ns` on value change, and hide both label and slider unless the selected mode is Blink. Hidden controls must not receive touch input.

- [ ] **Step 5: Extend preview, save, and cancel restoration**

Capture original/applied interval values in `PowerLedWorker`. Apply interval changes without saving during preview. On OK, persist interval and mode. In the destructor, restore interval and mode unless `_committed` is true.

- [ ] **Step 6: Run the focused tests and verify GREEN**

Run the host test and Power LED Python contract test.

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add main/apps/app_setup/workers/workers.h main/apps/app_setup/workers/device.cpp mac-codex-agent/tests/test_power_led_settings_firmware_contract.py
git commit -m "feat: add power LED blink interval slider"
```

### Task 4: Regression, Firmware, And Hardware Verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Run the complete Python suite**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass.

- [ ] **Step 2: Build the firmware**

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
idf.py -B build-flash-latest build
```

Expected: firmware links, image generation succeeds, and App partition capacity check passes.

- [ ] **Step 3: Flash the connected watch**

```bash
idf.py -B build-flash-latest -p /dev/cu.usbmodem1101 flash
```

Expected: bootloader, App, and partition-table writes pass hash verification and the device hard-resets.

- [ ] **Step 4: Verify hardware behavior**

Confirm from serial timestamps and the physical watch:

- Blink at 1 second repeats approximately every 1000ms.
- Blink at 2 seconds repeats approximately every 2000ms.
- Blink at 10 seconds repeats approximately every 10000ms.
- Each pulse remains approximately 160ms.
- The selected interval survives reboot.
- Off and On remain unchanged.

- [ ] **Step 5: Check final repository state**

```bash
git diff --check
git status --short
git log -4 --oneline
```

Expected: no uncommitted tracked changes; local build and hardware-evidence directories may remain untracked.
