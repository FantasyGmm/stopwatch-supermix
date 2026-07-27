# Display Protection Off Options and Layout Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Display Protection Settings page fit the round screen and add independent `Off` positions for automatic dimming and automatic screen-off without changing existing saved timeout meanings.

**Architecture:** Preserve the existing timeout-choice indices and add explicit per-stage enable booleans. The settings model maps UI position zero to disabled and later positions to the existing time choices, while the runtime state machine evaluates enabled stages independently. The global service owns compatibility defaults, persistence, power policy, and diagnostics; the LVGL view remains a draft-only client that applies once through OK.

**Tech Stack:** C++17, ESP-IDF 5.5.4, LVGL, StopWatch Settings/NVS wrapper, Python `unittest` source contracts, host-compiled assertion tests, esptool backup/flash scripts.

---

## File Structure

- `main/runtime/display/display_protection_config.h`: persistent configuration shape and choice counts.
- `main/runtime/display/display_protection_config.cpp`: sanitization and linked-time rules.
- `tests/display_protection_config_test.cpp`: executable configuration and compatibility behavior.
- `main/runtime/display/display_protection_logic.h`: independent-stage policy API and timing snapshot.
- `main/runtime/display/display_protection_logic.cpp`: inactivity state transitions.
- `tests/display_protection_logic_test.cpp`: executable four-combination state-machine coverage.
- `main/apps/app_setup/workers/display_protection_settings_model.h`: slider-position mapping and draft changes.
- `tests/display_protection_settings_model_test.cpp`: executable mapping and linkage coverage.
- `main/runtime/display/global_display_protection_service.cpp`: NVS migration defaults, persistence, and policy application.
- `main/runtime/display/display_protection_diagnostics.cpp`: serial metadata for enabled stages.
- `tests/display_protection_diagnostics_test.cpp`: exact diagnostic output coverage.
- `mac-codex-agent/tests/test_display_protection_firmware_contract.py`: service ownership, NVS keys, and runtime integration contract.
- `main/apps/app_setup/workers/display_protection.cpp`: approved two-line charging layout and `Off` labels.
- `mac-codex-agent/tests/test_display_protection_settings_firmware_contract.py`: round-screen-safe geometry and UI/model wiring contract.

### Task 1: Add compatible per-stage configuration flags

**Files:**
- Modify: `tests/display_protection_config_test.cpp`
- Modify: `main/runtime/display/display_protection_config.h`
- Modify: `main/runtime/display/display_protection_config.cpp`

- [ ] **Step 1: Write failing configuration tests**

Extend the default assertions and add disabled-stage cases:

```cpp
assert(defaults.dim_enabled);
assert(defaults.screen_off_enabled);

auto dim_disabled = defaults;
dim_disabled.dim_enabled = false;
dim_disabled.dim_choice = 5;
dim_disabled.screen_off_choice = 0;
const auto preserved_direct_off =
    sanitizeDisplayProtectionConfig(dim_disabled);
assert(!preserved_direct_off.dim_enabled);
assert(preserved_direct_off.screen_off_enabled);
assert(preserved_direct_off.dim_choice == 5);
assert(preserved_direct_off.screen_off_choice == 0);

auto off_disabled = defaults;
off_disabled.screen_off_enabled = false;
off_disabled.dim_choice = 5;
off_disabled.screen_off_choice = 0;
const auto preserved_dim_only =
    sanitizeDisplayProtectionConfig(off_disabled);
assert(preserved_dim_only.dim_enabled);
assert(!preserved_dim_only.screen_off_enabled);
assert(preserved_dim_only.dim_choice == 5);
assert(preserved_dim_only.screen_off_choice == 0);

auto both_enabled_invalid = defaults;
both_enabled_invalid.dim_choice = 5;
both_enabled_invalid.screen_off_choice = 0;
const auto repaired_enabled =
    sanitizeDisplayProtectionConfig(both_enabled_invalid);
assert(repaired_enabled.screen_off_choice == 4);
```

- [ ] **Step 2: Run the configuration test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_config_test.cpp \
  main/runtime/display/display_protection_config.cpp \
  -o /tmp/display_protection_config_test && \
  /tmp/display_protection_config_test
```

Expected: compilation fails because `DisplayProtectionConfig` has no `dim_enabled` or `screen_off_enabled` members.

- [ ] **Step 3: Add explicit compatible fields**

Append the fields so existing three-value aggregate initializers retain their meaning:

```cpp
struct DisplayProtectionConfig {
    uint8_t dim_choice = 1;
    uint8_t screen_off_choice = 1;
    bool protect_while_charging = false;
    bool dim_enabled = true;
    bool screen_off_enabled = true;
};
```

Change every ordering repair in `sanitizeDisplayProtectionConfig`, `withDisplayProtectionDimChoice`, and `withDisplayProtectionScreenOffChoice` to run only when both stages are enabled:

```cpp
if (config.dim_enabled && config.screen_off_enabled &&
    kOffMs[config.screen_off_choice] <= kDimMs[config.dim_choice]) {
    config.screen_off_choice = firstScreenOffChoiceAfter(
        kDimMs[config.dim_choice]);
}
```

For the screen-off-choice setter, keep its direction-specific repair:

```cpp
if (config.dim_enabled && config.screen_off_enabled &&
    kDimMs[config.dim_choice] >= kOffMs[config.screen_off_choice]) {
    config.dim_choice = lastDimChoiceBefore(
        kOffMs[config.screen_off_choice]);
}
```

- [ ] **Step 4: Run the configuration test and verify GREEN**

Run the Step 2 command again.

Expected: exit code 0 with no warnings or output.

- [ ] **Step 5: Commit the configuration model**

```bash
git add main/runtime/display/display_protection_config.h \
  main/runtime/display/display_protection_config.cpp \
  tests/display_protection_config_test.cpp
git commit -m "feat: allow independent display protection stages"
```

### Task 2: Teach the state machine independent dim and screen-off stages

**Files:**
- Modify: `tests/display_protection_logic_test.cpp`
- Modify: `main/runtime/display/display_protection_logic.h`
- Modify: `main/runtime/display/display_protection_logic.cpp`

- [ ] **Step 1: Write failing four-combination state-machine tests**

Add tests using the desired policy API:

```cpp
logic.reset(0);
logic.setPolicy({false, true, 15000U, 60000U}, 0);
assert(logic.update(59999U).action == DisplayProtectionAction::None);
assert(logic.state() == DisplayProtectionState::Active);
assert(logic.update(60000U).action == DisplayProtectionAction::ScreenOff);

logic.reset(0);
logic.setPolicy({true, false, 15000U, 60000U}, 0);
assert(logic.update(15000U).action == DisplayProtectionAction::Dim);
assert(logic.update(600000U).action == DisplayProtectionAction::None);
assert(logic.state() == DisplayProtectionState::Dimmed);

logic.reset(0);
logic.setPolicy({false, false, 15000U, 60000U}, 0);
assert(logic.update(UINT32_MAX).action == DisplayProtectionAction::None);
assert(logic.state() == DisplayProtectionState::Active);
const auto disabled_timing = logic.timingSnapshot(70000U);
assert(disabled_timing.dim_remaining_ms == 0U);
assert(disabled_timing.screen_off_remaining_ms == 0U);

const auto disabled_manual_off = logic.screenOffNow(70001U);
assert(disabled_manual_off.action == DisplayProtectionAction::ScreenOff);
assert(logic.userActivity(70002U).consume_input);
```

Add an invalid-order test proving only a fully enabled policy is repaired:

```cpp
logic.reset(0);
logic.setPolicy({true, true, 60000U, 30000U}, 0);
const auto repaired_policy = logic.timingSnapshot(0);
assert(repaired_policy.dim_after_ms == 60000U);
assert(repaired_policy.screen_off_after_ms == 60001U);
```

- [ ] **Step 2: Run the state-machine test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_logic_test.cpp \
  main/runtime/display/display_protection_logic.cpp \
  -o /tmp/display_protection_logic_test && \
  /tmp/display_protection_logic_test
```

Expected: compilation fails because `DisplayProtectionLogic::setPolicy` and `DisplayProtectionPolicy` do not exist.

- [ ] **Step 3: Add the policy type and API**

In the header, define:

```cpp
struct DisplayProtectionPolicy {
    bool dim_enabled = true;
    bool screen_off_enabled = true;
    uint32_t dim_after_ms = 15000U;
    uint32_t screen_off_after_ms = 60000U;
};
```

Add:

```cpp
DisplayProtectionDecision setPolicy(DisplayProtectionPolicy policy,
                                    uint32_t now_ms);
```

Store:

```cpp
DisplayProtectionPolicy _policy{};
```

Keep `setTimeouts` as a compatibility wrapper for existing callers during the incremental commits:

```cpp
DisplayProtectionDecision DisplayProtectionLogic::setTimeouts(
    uint32_t dim_after_ms, uint32_t screen_off_after_ms, uint32_t now_ms)
{
    return setPolicy(
        {true, true, dim_after_ms, screen_off_after_ms}, now_ms);
}
```

- [ ] **Step 4: Implement independent evaluation and timing**

Implement policy sanitization without sentinels:

```cpp
DisplayProtectionDecision DisplayProtectionLogic::setPolicy(
    DisplayProtectionPolicy policy, uint32_t now_ms)
{
    if (policy.dim_enabled && policy.screen_off_enabled &&
        policy.screen_off_after_ms <= policy.dim_after_ms) {
        if (policy.dim_after_ms == std::numeric_limits<uint32_t>::max()) {
            policy.dim_after_ms -= 1U;
            policy.screen_off_after_ms =
                std::numeric_limits<uint32_t>::max();
        } else {
            policy.screen_off_after_ms = policy.dim_after_ms + 1U;
        }
    }
    _policy = policy;
    _inactivity_origin_ms = now_ms;
    if (_state != DisplayProtectionState::Active) {
        return restore(now_ms, false);
    }
    return {};
}
```

Gate transitions in `update`:

```cpp
if (_policy.screen_off_enabled &&
    elapsed_ms >= _policy.screen_off_after_ms &&
    _state != DisplayProtectionState::ScreenOff) {
    _state = DisplayProtectionState::ScreenOff;
    return {DisplayProtectionAction::ScreenOff, false};
}
if (_policy.dim_enabled && elapsed_ms >= _policy.dim_after_ms &&
    _state == DisplayProtectionState::Active) {
    _state = DisplayProtectionState::Dimmed;
    return {DisplayProtectionAction::Dim, false};
}
```

Return zero remaining time for disabled stages:

```cpp
const uint32_t dim_remaining =
    !_policy.dim_enabled || inactivity_ms >= _policy.dim_after_ms
        ? 0U
        : _policy.dim_after_ms - inactivity_ms;
const uint32_t screen_off_remaining =
    !_policy.screen_off_enabled ||
            inactivity_ms >= _policy.screen_off_after_ms
        ? 0U
        : _policy.screen_off_after_ms - inactivity_ms;
return {inactivity_ms, _policy.dim_after_ms,
        _policy.screen_off_after_ms, dim_remaining,
        screen_off_remaining};
```

- [ ] **Step 5: Run the state-machine test and verify GREEN**

Run the Step 2 command again.

Expected: exit code 0 with no warnings or output.

- [ ] **Step 6: Commit the runtime policy**

```bash
git add main/runtime/display/display_protection_logic.h \
  main/runtime/display/display_protection_logic.cpp \
  tests/display_protection_logic_test.cpp
git commit -m "feat: evaluate display protection stages independently"
```

### Task 3: Map slider position zero to Off in the draft model

**Files:**
- Modify: `tests/display_protection_settings_model_test.cpp`
- Modify: `main/apps/app_setup/workers/display_protection_settings_model.h`

- [ ] **Step 1: Write failing slider-position tests**

Replace direct choice-only coverage with position behavior:

```cpp
DisplayProtectionSettingsModel model(defaultDisplayProtectionConfig());
assert(model.dimSliderPosition() == 2);
assert(model.screenOffSliderPosition() == 2);
assert(model.dimLabel() == "15s");
assert(model.screenOffLabel() == "1m");

model.setDimSliderPosition(0);
assert(!model.config().dim_enabled);
assert(model.config().dim_choice == 1);
assert(model.dimSliderPosition() == 0);
assert(model.dimLabel() == "Off");
assert(model.config().screen_off_enabled);

model.setScreenOffSliderPosition(0);
assert(!model.config().screen_off_enabled);
assert(model.config().screen_off_choice == 1);
assert(model.screenOffSliderPosition() == 0);
assert(model.screenOffLabel() == "Off");

model.setDimSliderPosition(6);
assert(model.config().dim_enabled);
assert(model.config().dim_choice == 5);
assert(!model.config().screen_off_enabled);
assert(model.config().screen_off_choice == 1);

model.setScreenOffSliderPosition(1);
assert(model.config().screen_off_enabled);
assert(model.config().screen_off_choice == 0);
assert(model.config().dim_choice == 1);

model.setDimSliderPosition(5);
assert(model.config().dim_choice == 4);
assert(model.config().screen_off_choice == 3);

model.setProtectWhileCharging(true);
assert(model.config().protect_while_charging);
```

- [ ] **Step 2: Run the settings-model test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_settings_model_test.cpp \
  main/runtime/display/display_protection_config.cpp \
  -o /tmp/display_protection_settings_model_test && \
  /tmp/display_protection_settings_model_test
```

Expected: compilation fails because the slider-position and label methods do not exist.

- [ ] **Step 3: Implement position mapping in the model**

Add these constants and methods:

```cpp
static constexpr int DimSliderMax = DisplayProtectionDimChoiceCount;
static constexpr int ScreenOffSliderMax =
    DisplayProtectionScreenOffChoiceCount;

int dimSliderPosition() const
{
    return _config.dim_enabled ? _config.dim_choice + 1 : 0;
}

int screenOffSliderPosition() const
{
    return _config.screen_off_enabled ? _config.screen_off_choice + 1 : 0;
}

std::string_view dimLabel() const
{
    return _config.dim_enabled
        ? displayProtectionDimLabel(_config.dim_choice)
        : std::string_view{"Off"};
}

std::string_view screenOffLabel() const
{
    return _config.screen_off_enabled
        ? displayProtectionScreenOffLabel(_config.screen_off_choice)
        : std::string_view{"Off"};
}
```

Replace the choice setters used by the view with:

```cpp
void setDimSliderPosition(int position)
{
    if (position <= 0) {
        _config.dim_enabled = false;
        return;
    }
    _config.dim_enabled = true;
    _config = withDisplayProtectionDimChoice(_config, position - 1);
}

void setScreenOffSliderPosition(int position)
{
    if (position <= 0) {
        _config.screen_off_enabled = false;
        return;
    }
    _config.screen_off_enabled = true;
    _config = withDisplayProtectionScreenOffChoice(_config, position - 1);
}
```

- [ ] **Step 4: Run the settings-model test and verify GREEN**

Run the Step 2 command again.

Expected: exit code 0 with no warnings or output.

- [ ] **Step 5: Commit the settings model**

```bash
git add main/apps/app_setup/workers/display_protection_settings_model.h \
  tests/display_protection_settings_model_test.cpp
git commit -m "feat: map display protection sliders to off states"
```

### Task 4: Persist, apply, and diagnose independent stages

**Files:**
- Modify: `mac-codex-agent/tests/test_display_protection_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_debug_command_firmware_contract.py`
- Modify: `tests/display_protection_diagnostics_test.cpp`
- Modify: `main/runtime/display/global_display_protection_service.cpp`
- Modify: `main/runtime/display/display_protection_diagnostics.cpp`

- [ ] **Step 1: Write failing service and diagnostics tests**

In the firmware contract, require the new compatibility reads and writes:

```python
for token in (
    'GetBool("dim_on", defaults.dim_enabled)',
    'GetBool("off_on", defaults.screen_off_enabled)',
    'SetBool("dim_on", sanitized.dim_enabled)',
    'SetBool("off_on", sanitized.screen_off_enabled)',
    "DisplayProtectionPolicy{",
):
    self.assertIn(token, service)

self.assertEqual(
    {"dim_idx", "off_idx", "chg_guard", "dim_on", "off_on"},
    set(nvs_keys),
)
```

Require automatic policy to be inactive when both stages are off:

```python
self.assertIn("_config.dim_enabled || _config.screen_off_enabled", service)
```

In the debug-command contract, add required fields:

```python
"dim_enabled=%u",
"off_enabled=%u",
```

Update the native diagnostic setup to exercise a disabled dim stage:

```cpp
logic.setPolicy({false, true, 10000U, 30000U},
                UINT32_MAX - 5000U);
assert(wrapped_timing.dim_remaining_ms == 0U);
.config = {2, 4, true, false, true},
```

```text
DISPLAY PROTECTION state=dimmed effective=20 user=73 automatic=1 power=external power_valid=1 dim_enabled=0 off_enabled=1 dim_choice=2 off_choice=4 dim_ms=10000 off_ms=30000 idle_ms=7000 dim_remaining_ms=0 off_remaining_ms=23000 charging_guard=1 reasons=0x05 last_wake=power_inserted
```

- [ ] **Step 2: Run focused tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_firmware_contract \
  mac-codex-agent.tests.test_debug_command_firmware_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_diagnostics_test.cpp \
  main/runtime/display/display_protection_diagnostics.cpp \
  main/runtime/display/display_protection_logic.cpp \
  -o /tmp/display_protection_diagnostics_test && \
  /tmp/display_protection_diagnostics_test
```

Expected: Python assertions fail for missing NVS/diagnostic tokens and the native diagnostic comparison fails.

- [ ] **Step 3: Load and save compatibility booleans**

In `init`, add:

```cpp
loaded.dim_enabled =
    settings.GetBool("dim_on", defaults.dim_enabled);
loaded.screen_off_enabled =
    settings.GetBool("off_on", defaults.screen_off_enabled);
```

In the persistent branch of `applyConfig`, add:

```cpp
settings.SetBool("dim_on", sanitized.dim_enabled);
settings.SetBool("off_on", sanitized.screen_off_enabled);
```

Include both booleans in committed/applied configuration logs.

- [ ] **Step 4: Apply the explicit runtime policy**

Replace both service calls to `setTimeouts` with:

```cpp
_logic.setPolicy(
    DisplayProtectionPolicy{
        _config.dim_enabled,
        _config.screen_off_enabled,
        displayProtectionDimMs(_config),
        displayProtectionScreenOffMs(_config),
    },
    now_ms)
```

Make the effective automatic policy require at least one enabled stage:

```cpp
return (_config.dim_enabled || _config.screen_off_enabled) &&
       (_config.protect_while_charging ||
        _external_power.state != ExternalPowerState::ExternalPower);
```

- [ ] **Step 5: Add stage flags to diagnostics**

Insert the fields before the choice indices:

```cpp
"power=%s power_valid=%u dim_enabled=%u off_enabled=%u "
"dim_choice=%u off_choice=%u "
```

Pass:

```cpp
static_cast<unsigned>(diagnostics.config.dim_enabled),
static_cast<unsigned>(diagnostics.config.screen_off_enabled),
```

- [ ] **Step 6: Run focused tests and verify GREEN**

Run the Step 2 commands again.

Expected: both Python modules and the native diagnostic executable pass with no warnings.

- [ ] **Step 7: Commit service persistence and diagnostics**

```bash
git add main/runtime/display/global_display_protection_service.cpp \
  main/runtime/display/display_protection_diagnostics.cpp \
  tests/display_protection_diagnostics_test.cpp \
  mac-codex-agent/tests/test_display_protection_firmware_contract.py \
  mac-codex-agent/tests/test_debug_command_firmware_contract.py
git commit -m "feat: persist display protection stage states"
```

### Task 5: Implement the approved two-line round-screen layout

**Files:**
- Modify: `mac-codex-agent/tests/test_display_protection_settings_firmware_contract.py`
- Modify: `main/apps/app_setup/workers/display_protection.cpp`

- [ ] **Step 1: Write the failing layout and wiring contract**

Replace the old geometry assertions with the approved B layout:

```python
self.assertIn('createChargingSection("Charging Protection")', self.worker)
self.assertIn("setSize(320, 88)", self.worker)
self.assertIn("align(LV_ALIGN_TOP_MID, 0, 46)", self.worker)
self.assertIn("_charging_label->align(LV_ALIGN_TOP_MID, 0, 0)", self.worker)
self.assertIn("_charging_switch->align(LV_ALIGN_TOP_MID, 0, 35)", self.worker)

self.assertIn("align(LV_ALIGN_TOP_MID, 0, 144)", self.worker)
self.assertIn("align(LV_ALIGN_TOP_MID, 0, 180)", self.worker)
self.assertIn("align(LV_ALIGN_TOP_MID, 0, 222)", self.worker)
self.assertIn("align(LV_ALIGN_TOP_MID, 0, 258)", self.worker)

self.assertIn("_ok_button->align(LV_ALIGN_TOP_MID, 0, 335)", self.worker)
self.assertIn("_ok_button->setSize(334, 86)", self.worker)
self.assertIn("_ok_button->setRadius(43)", self.worker)
```

Require the new slider mapping and Off labels:

```python
self.assertIn(
    "_dim_slider->setRange(0, DisplayProtectionSettingsModel::DimSliderMax, false)",
    self.worker,
)
self.assertIn(
    "_screen_off_slider->setRange(0, DisplayProtectionSettingsModel::ScreenOffSliderMax, false)",
    self.worker,
)
self.assertIn("_model.setDimSliderPosition", self.worker)
self.assertIn("_model.setScreenOffSliderPosition", self.worker)
self.assertIn("_model.dimSliderPosition()", self.worker)
self.assertIn("_model.screenOffSliderPosition()", self.worker)
self.assertIn("_model.dimLabel()", self.worker)
self.assertIn("_model.screenOffLabel()", self.worker)
```

In `test_edits_only_mutate_the_draft_model`, replace the old choice-setter assertions with:

```python
self.assertIn("_model.setDimSliderPosition", view)
self.assertIn("_model.setScreenOffSliderPosition", view)
```

- [ ] **Step 2: Run the UI contract and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_settings_firmware_contract -v
```

Expected: geometry and slider-wiring assertions fail against the old one-line layout.

- [ ] **Step 3: Implement the two-line charging section**

Replace the call with:

```cpp
createChargingSection("Charging Protection");
```

Rename the helper and set the approved geometry:

```cpp
void createChargingSection(const char* title)
{
    _charging_row = std::make_unique<Container>(_panel->get());
    _charging_row->setSize(320, 88);
    _charging_row->align(LV_ALIGN_TOP_MID, 0, 46);
    _charging_row->setBgOpa(LV_OPA_TRANSP);
    _charging_row->setBorderWidth(0);
    _charging_row->setShadowWidth(0);
    _charging_row->setPaddingAll(0);
    _charging_row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _charging_label = std::make_unique<Label>(_charging_row->get());
    _charging_label->setText(title);
    _charging_label->setTextFont(&lv_font_montserrat_22);
    _charging_label->setTextColor(lv_color_hex(0xFFFFFF));
    _charging_label->align(LV_ALIGN_TOP_MID, 0, 0);

    _charging_switch = std::make_unique<Switch>(_charging_row->get());
    _charging_switch->setSize(80, 44);
    _charging_switch->align(LV_ALIGN_TOP_MID, 0, 35);
```

Keep the existing switch styling and draft-model callback after this block.

- [ ] **Step 4: Reposition the controls and safe-area OK button**

Use these vertical positions:

```cpp
_dim_label->align(LV_ALIGN_TOP_MID, 0, 144);
_dim_slider->align(LV_ALIGN_TOP_MID, 0, 180);
_screen_off_label->align(LV_ALIGN_TOP_MID, 0, 222);
_screen_off_slider->align(LV_ALIGN_TOP_MID, 0, 258);

_ok_button->align(LV_ALIGN_TOP_MID, 0, 335);
_ok_button->setSize(334, 86);
_ok_button->setRadius(43);
```

Keep the existing fonts and colors.

- [ ] **Step 5: Wire the Off-aware positions and labels**

Use model maxima and position setters:

```cpp
_dim_slider->setRange(
    0, DisplayProtectionSettingsModel::DimSliderMax, false);
_model.setDimSliderPosition(static_cast<int>(value));

_screen_off_slider->setRange(
    0, DisplayProtectionSettingsModel::ScreenOffSliderMax, false);
_model.setScreenOffSliderPosition(static_cast<int>(value));
```

Synchronize positions:

```cpp
_dim_slider->setValue(_model.dimSliderPosition());
_screen_off_slider->setValue(_model.screenOffSliderPosition());
```

Format labels from the model so `Off` is displayed without a special case in the view:

```cpp
const auto dim_label = _model.dimLabel();
const auto screen_off_label = _model.screenOffLabel();
```

- [ ] **Step 6: Run focused UI/model tests and verify GREEN**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent.tests.test_display_protection_settings_firmware_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/display_protection_settings_model_test.cpp \
  main/runtime/display/display_protection_config.cpp \
  -o /tmp/display_protection_settings_model_test && \
  /tmp/display_protection_settings_model_test
```

Expected: both focused suites pass with no warnings.

- [ ] **Step 7: Commit the revised Settings page**

```bash
git add main/apps/app_setup/workers/display_protection.cpp \
  mac-codex-agent/tests/test_display_protection_settings_firmware_contract.py
git commit -m "fix: fit display protection settings to round screen"
```

### Task 6: Full verification, backup-gated flash, and physical acceptance

**Files:**
- Create: timestamped evidence under `/Users/bytedance/Projects.localized/Codex/StopWatch/hardware-evidence/`
- Use: `scripts/run_unified_offline_validation.sh`
- Use: `scripts/backup_watch_flash.sh`
- Use: `scripts/flash_unified_candidate.sh`
- Use: `scripts/validate_watch_boot.py`
- Use: `scripts/watch_debug.py`

- [ ] **Step 1: Run the complete exact-head software gate**

```bash
PYTHON="$PWD/mac-codex-agent/.venv/bin/python" \
IDF_COMPONENT_CACHE_PATH="$PWD/.idf-component-cache" \
./scripts/run_unified_offline_validation.sh
```

Expected: all Python tests, all 32 offline stages, the clean unified firmware build, capacity gate, and `git diff --check` pass. Record the application size, headroom, commit, and SHA-256 from the generated manifest.

- [ ] **Step 2: Confirm repository scope**

```bash
git diff --check 80ad5b4...HEAD
git status --short
git log --oneline -8
```

Expected: only the known untracked cache/brainstorm directories remain; all production/test changes are committed on `codex/configurable-global-display-protection`.

- [ ] **Step 3: Detect one watch and mute audio**

```bash
setopt null_glob
PORTS=(/dev/cu.usbmodem*)
(( ${#PORTS[@]} == 1 ))
PORT="${PORTS[1]}"
python3 scripts/watch_debug.py --port "$PORT" volume 0
```

Expected: one port and `DEBUG VOLUME 0 OK`.

- [ ] **Step 4: Capture and verify a fresh full-flash backup**

```bash
source scripts/resolve_esptool_python.sh
BUILD_DIR="$PWD/build-unified"
ESPTOOL_PYTHON="$(resolve_esptool_python "$BUILD_DIR")"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP="/Users/bytedance/Projects.localized/Codex/StopWatch/hardware-evidence/firmware-backups/${TIMESTAMP}-before-display-protection-off-layout.bin"
mkdir -p "$(dirname "$BACKUP")"
PYTHON="$ESPTOOL_PYTHON" ./scripts/backup_watch_flash.sh \
  --port "$PORT" --output "$BACKUP"
test "$(stat -f %z "$BACKUP")" -eq 16777216
shasum -a 256 -c "$BACKUP.sha256"
```

Expected: exactly 16,777,216 bytes and a valid SHA-256 sidecar.

- [ ] **Step 5: Flash the verified isolated build**

```bash
./scripts/flash_unified_candidate.sh \
  --port "$PORT" --backup "$BACKUP" --build-dir "$BUILD_DIR"
```

Expected: bootloader, partition table, application, and assets each report verified hashes.

- [ ] **Step 6: Validate boot, re-mute, and restore the Mac Agent**

```bash
BOOT_LOG="/Users/bytedance/Projects.localized/Codex/StopWatch/hardware-evidence/${TIMESTAMP}-display-protection-off-layout-boot.log"
BOOT_SUMMARY="/Users/bytedance/Projects.localized/Codex/StopWatch/hardware-evidence/${TIMESTAMP}-display-protection-off-layout-boot.json"
"$ESPTOOL_PYTHON" scripts/validate_watch_boot.py \
  --port "$PORT" --timeout 90 --log "$BOOT_LOG" --summary "$BOOT_SUMMARY"
python3 scripts/watch_debug.py --port "$PORT" volume 0
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Expected: Launcher opens with no reset, panic, or watchdog; volume is zero; the Mac Agent process is running.

- [ ] **Step 7: Execute physical acceptance checks**

Confirm on the watch:

1. `Charging Protection` and its switch are fully visible in two centered lines.
2. Both sliders show `Off` at the leftmost position and all previous time choices remain present.
3. Dim Off + screen-off timed stays bright and then turns off directly.
4. Dim timed + screen-off Off dims and remains dim.
5. Both Off produces no automatic transition.
6. Both timed produces dim followed by a strictly later screen-off.
7. Moving one timed slider across the other preserves strict ordering only when both are enabled.
8. Canceling the page preserves prior runtime/NVS state; OK applies once and survives reset.
9. External power with Charging Protection disabled stays awake; enabling it uses the selected stage combination.
10. Manual Codex screen-off, first-touch consumption, insertion wake, keep-awake reasons, and brightness restoration remain correct.

- [ ] **Step 8: Record evidence and finish only after physical results**

Create a timestamped Markdown report under `/Users/bytedance/Projects.localized/Codex/StopWatch/hardware-evidence/` containing exact artifact hashes, backup path/checksum, boot summary, automated logs, and each physical observation. Keep unobserved visual behavior marked pending rather than inferring it from source or boot success.
