# Power LED and WiFi OK Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persisted Off/On/Blink Power LED modes and make the WiFi confirmation button match the existing Settings `OK` control.

**Architecture:** Extend `Hal` with one cached M5PM1-backed mode stored in the existing `system` NVS namespace and update a 160 ms pulse every 2 seconds from the main loop. Add a focused Settings worker that previews the mode and commits only on `OK`; keep WiFi runtime behavior intact while replacing only its bottom confirmation control.

**Tech Stack:** ESP-IDF 5.5.4, C++17, LVGL/Smooth UI Toolkit, M5PM1, NVS, Python unittest firmware contracts.

---

### Task 1: Add failing firmware contracts

**Files:**
- Create: `mac-codex-agent/tests/test_power_led_settings_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_wifi_settings_layout_firmware_contract.py`

- [ ] **Step 1: Add a Power LED contract test**

Assert that `Hal` exposes `PowerLedMode`, `setPowerLedMode`, `getPowerLedMode`, and `updatePowerLed`, uses the `pwr_led_mode` NVS key with `Off` as the default, applies `setLedEnLevel`, registers `Power LED` in `app_setup.cpp`, and defines `PowerLedWorker` with preview, persist, and restore paths.

- [ ] **Step 2: Add an exact WiFi OK style contract**

Assert that `wifi.cpp` labels the bottom action `OK` and applies size `374 x 130`, center offset `175`, radius `77`, green `0x4AD78C`, Montserrat 28, and label color `0x0F5831`.

- [ ] **Step 3: Run contracts and verify failure**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_power_led_settings_firmware_contract \
  mac-codex-agent.tests.test_wifi_settings_layout_firmware_contract
```

Expected: failures for missing Power LED APIs/worker and the current compact WiFi `Done` button.

### Task 2: Implement HAL persistence and hardware control

**Files:**
- Modify: `main/hal/hal.h`
- Modify: `main/hal/hal_pmic.cpp`

- [ ] **Step 1: Add the HAL API and cached state**

Add:

```cpp
enum class PowerLedMode : uint8_t { Off = 0, On = 1, Blink = 2 };
void setPowerLedMode(PowerLedMode mode, bool saveToSettings = false);
PowerLedMode getPowerLedMode(bool loadFromSettings = false);
void updatePowerLed();
```

Add private mode, output, next-pulse, and pulse-end state.

- [ ] **Step 2: Load and apply the saved value during PMIC initialization**

After M5PM1 initialization, read `Settings("system").GetInt("pwr_led_mode", 0)`, sanitize it to the enum range, and apply the mode.

- [ ] **Step 3: Implement preview and persistence**

`setPowerLedMode` previews Off/On immediately and starts Blink with an immediate pulse. `updatePowerLed` ends the pulse after 160 ms and starts another at each 2-second deadline. Persist only when `saveToSettings` is true.

- [ ] **Step 4: Re-run the Power LED HAL contract**

Run:

```bash
python3 -m unittest mac-codex-agent.tests.test_power_led_settings_firmware_contract
```

Expected: UI-related assertions still fail; HAL assertions pass.

### Task 3: Add the Power LED Settings page

**Files:**
- Modify: `main/apps/app_setup/workers/workers.h`
- Modify: `main/apps/app_setup/workers/device.cpp`
- Modify: `main/apps/app_setup/app_setup.cpp`

- [ ] **Step 1: Define `PowerLedWorker`**

Give the worker a nested view, the original mode, the currently previewed mode, an `update()` implementation, and a destructor that restores the original mode unless `OK` committed the change.

- [ ] **Step 2: Build the Button-style view**

Use three Button-style exclusive option rows labeled `Off`, `On`, and `Blink 2s`, with Montserrat 24 white labels and one selected indicator, plus the `374 x 130` green `OK` button centered at offset `175`.

- [ ] **Step 3: Register the menu entry**

Add `Power LED` under `Device` and construct `PowerLedWorker` when selected.

- [ ] **Step 4: Run the Power LED contract**

Run:

```bash
python3 -m unittest mac-codex-agent.tests.test_power_led_settings_firmware_contract
```

Expected: PASS.

### Task 4: Unify the WiFi confirmation button

**Files:**
- Modify: `main/apps/app_setup/workers/wifi.cpp`
- Test: `mac-codex-agent/tests/test_wifi_settings_layout_firmware_contract.py`

- [ ] **Step 1: Replace the compact Done control**

Keep its completion callback but rename it `OK` and apply the exact shared Settings confirmation geometry and colors documented in Task 1.

- [ ] **Step 2: Prevent overlap**

Move the Add/Stop Pairing action above the unified `OK` button without changing pairing behavior or saved-network selection.

- [ ] **Step 3: Run WiFi layout and behavior tests**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_wifi_settings_layout_firmware_contract \
  tests.wifi_settings_controller_test \
  tests.wifi_settings_model_test
```

If the C++ test targets are not Python-importable, run their existing CMake/CTest target instead. Expected: all available WiFi tests PASS.

### Task 5: Verify the integrated firmware

**Files:**
- No source changes expected.

- [ ] **Step 1: Run focused Python contracts**

```bash
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*settings*_firmware_contract.py'
```

Expected: PASS.

- [ ] **Step 2: Build the unified firmware in an isolated directory**

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
idf.py -B build-power-led \
  -D "SDKCONFIG=$PWD/sdkconfig.unified" \
  -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults;$PWD/sdkconfig.unified.defaults" \
  reconfigure
idf.py -B build-power-led build
```

Expected: build completes and partition size checks pass.

- [ ] **Step 3: Review the final diff**

Confirm only the new spec/plan/tests and the intended HAL, Settings worker, menu, and WiFi files changed. Preserve unrelated Codex status worktree changes.
