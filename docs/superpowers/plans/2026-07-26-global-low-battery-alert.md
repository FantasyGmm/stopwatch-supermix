# Global Low-Battery Alert Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one non-blocking low-battery sound and text alert that works across every StopWatch application, remains dark while the screen is off, and alerts once per low-battery episode.

**Architecture:** Keep threshold, hysteresis, screen-off, and delivery decisions in a dependency-free `LowBatteryAlertLogic`. A `GlobalLowBatteryAlertService` samples HAL outside the LVGL lock, applies a presentation-only overlay on `lv_layer_top()` under the lock, and starts a short generated melody only when playback is free. HAL gains read-only battery-sample, display-visible, and audio-playing observations.

**Tech Stack:** C++17, ESP-IDF 5.5.4, LVGL 9, Smooth UI Toolkit, existing HAL and `ResourceSupervisor`, native `assert` tests, Python `unittest` firmware contracts.

---

## File Map

- Create `main/runtime/power/low_battery_alert_logic.h` and `.cpp`: pure alert policy.
- Create `main/runtime/power/global_low_battery_alert_service.h` and `.cpp`: HAL sampling, UI queues, and playback ownership.
- Create `main/apps/common/low_battery/low_battery_alert_layout.h`: circular-safe constants.
- Create `main/apps/common/low_battery/low_battery_alert_overlay.h` and `.cpp`: touch-transparent global banner.
- Modify `main/hal/hal.h`, `main/hal/hal_pmic.cpp`, `main/hal/hal_display.cpp`, and `main/hal/hal_audio.cpp`: read-only hardware observations.
- Modify `main/main.cpp`: global initialization and update ordering.
- Create `tests/low_battery_alert_logic_test.cpp`: native policy/layout coverage.
- Create `mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py`: integration contracts.
- Modify `scripts/run_unified_offline_validation.sh`: standard test gate.

### Task 1: Pure Low-Battery Policy

**Files:**
- Create: `main/runtime/power/low_battery_alert_logic.h`
- Create: `main/runtime/power/low_battery_alert_logic.cpp`
- Create: `tests/low_battery_alert_logic_test.cpp`

- [ ] **Step 1: Write the failing policy test**

Create a native test with these helpers:

```cpp
LowBatteryAlertInput sample(uint32_t now_ms, uint8_t percent,
                            bool valid = true, bool external_power = false,
                            bool display_visible = true,
                            bool playback_available = true,
                            bool speaker_audible = true)
{
    return {now_ms, true, valid, percent, external_power, display_visible,
            playback_available, speaker_audible};
}

LowBatteryAlertInput tick(uint32_t now_ms, bool display_visible = true,
                          bool playback_available = true,
                          bool speaker_audible = true)
{
    return {now_ms, false, true, 0, false, display_visible,
            playback_available, speaker_audible};
}
```

Cover the basic transition with these exact assertions:

```cpp
LowBatteryAlertLogic logic;
assert(!logic.update(sample(0, 0, false)).play_sound);
assert(!logic.update(sample(1000, 29)).play_sound);
auto entered = logic.update(sample(2000, 28));
assert(entered.play_sound && entered.show_text);
assert(entered.battery_percent == 28);
assert(!logic.update(sample(3000, 27)).play_sound);

logic.update(sample(4000, 35));
assert(logic.phase() == LowBatteryAlertPhase::Armed);
assert(!logic.update(sample(5000, 29)).play_sound);
assert(logic.update(sample(6000, 29)).play_sound);
```

Add these independent blocks:

```cpp
{
    LowBatteryAlertLogic policy;
    policy.update(sample(0, 29));
    policy.update(sample(1000, 31));
    assert(!policy.update(sample(2000, 29)).play_sound);
    assert(policy.update(sample(3000, 29)).play_sound);
    assert(!policy.update(sample(4000, 34)).play_sound);
}

{
    LowBatteryAlertLogic policy;
    policy.update(sample(0, 28));
    assert(policy.update(sample(1000, 28)).play_sound);
    auto charging = policy.update(sample(2000, 28, true, true));
    assert(charging.hide_text);
    assert(policy.phase() == LowBatteryAlertPhase::Armed);
    assert(!policy.update(sample(3000, 28)).play_sound);
    assert(policy.update(sample(4000, 28)).play_sound);
}

{
    LowBatteryAlertLogic policy;
    policy.update(sample(0, 29, true, false, false));
    auto off = policy.update(sample(1000, 29, true, false, false));
    assert(off.play_sound && !off.show_text);
    assert(policy.phase() == LowBatteryAlertPhase::LowPending);
    auto wake = policy.update(tick(2000, true));
    assert(wake.show_text && !wake.play_sound);
    assert(!policy.update(tick(5999)).hide_text);
    assert(policy.update(tick(6000)).hide_text);
}

{
    LowBatteryAlertLogic policy;
    policy.update(sample(0, 29, true, false, true, false));
    auto busy = policy.update(sample(1000, 29, true, false, true, false));
    assert(!busy.play_sound && busy.show_text);
    assert(policy.phase() == LowBatteryAlertPhase::LowPending);
    assert(policy.update(tick(1500, true, true)).play_sound);
    policy.deferSound();
    assert(policy.update(tick(1600, true, true)).play_sound);
}

{
    LowBatteryAlertLogic policy;
    policy.update(sample(0, 29, true, false, true, true, false));
    auto muted = policy.update(sample(1000, 29, true, false, true, true, false));
    assert(!muted.play_sound && muted.show_text);
    assert(policy.phase() == LowBatteryAlertPhase::LowAlerted);
}

{
    LowBatteryAlertLogic policy;
    policy.update(sample(0, 29));
    policy.update(sample(1000, 29));
    assert(policy.update(tick(1500, false)).hide_text);
    assert(policy.update(tick(9000, true)).show_text);
    assert(!policy.update(tick(12999, true)).hide_text);
    assert(policy.update(tick(13000, true)).hide_text);
}

{
    LowBatteryAlertLogic policy;
    const uint32_t near_wrap = UINT32_MAX - 1000U;
    policy.update(sample(near_wrap - 1000U, 29));
    policy.update(sample(near_wrap, 29));
    assert(!policy.update(tick(2998U)).hide_text);
    assert(policy.update(tick(2999U)).hide_text);
}
```

- [ ] **Step 2: Compile and verify red**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/low_battery_alert_logic_test.cpp \
  main/runtime/power/low_battery_alert_logic.cpp \
  -o /tmp/low_battery_alert_logic_test
```

Expected: compilation fails because the policy files do not exist.

- [ ] **Step 3: Add the policy API**

Create `main/runtime/power/low_battery_alert_logic.h` with this interface:

```cpp
#pragma once
#include <cstdint>

enum class LowBatteryAlertPhase : uint8_t { Armed, LowPending, LowAlerted };

struct LowBatteryAlertInput {
    uint32_t now_ms = 0;
    bool battery_sample_due = false;
    bool battery_sample_valid = false;
    uint8_t battery_percent = 0;
    bool external_power = false;
    bool display_visible = true;
    bool playback_available = false;
    bool speaker_audible = true;
};

struct LowBatteryAlertActions {
    bool show_text = false;
    bool hide_text = false;
    bool play_sound = false;
    uint8_t battery_percent = 0;
};

class LowBatteryAlertLogic {
public:
    static constexpr uint8_t lowThreshold = 30;
    static constexpr uint8_t rearmThreshold = 35;
    static constexpr uint8_t confirmationSamples = 2;
    static constexpr uint32_t textVisibleMs = 4000;

    LowBatteryAlertActions update(const LowBatteryAlertInput& input);
    void deferSound();
    LowBatteryAlertPhase phase() const { return _phase; }

private:
    LowBatteryAlertPhase _phase = LowBatteryAlertPhase::Armed;
    uint8_t _consecutive_low_samples = 0;
    uint8_t _alert_percent = 0;
    bool _episode_active = false;
    bool _sound_pending = false;
    bool _text_pending = false;
    bool _text_visible = false;
    uint32_t _text_visible_since_ms = 0;

    void rearm(LowBatteryAlertActions& actions);
    void refreshPhase();
};
```

- [ ] **Step 4: Implement the exact transition order**

In `low_battery_alert_logic.cpp`, process in this order:

```cpp
if (input.battery_sample_due) {
    if (input.external_power) {
        rearm(actions);
    } else if (input.battery_sample_valid) {
        if (input.battery_percent >= rearmThreshold) {
            rearm(actions);
        } else if (input.battery_percent < lowThreshold && !_episode_active) {
            ++_consecutive_low_samples;
            if (_consecutive_low_samples >= confirmationSamples) {
                _episode_active = true;
                _sound_pending = true;
                _text_pending = true;
                _alert_percent = input.battery_percent;
            }
        } else if (!_episode_active) {
            _consecutive_low_samples = 0;
        }
    }
}

if (_text_visible && !input.display_visible) {
    actions.hide_text = true;
    _text_visible = false;
    _text_pending = _episode_active;
} else if (_text_visible &&
           static_cast<uint32_t>(input.now_ms - _text_visible_since_ms) >=
               textVisibleMs) {
    actions.hide_text = true;
    _text_visible = false;
}

if (_episode_active && _text_pending && input.display_visible) {
    actions.show_text = true;
    actions.battery_percent = _alert_percent;
    _text_pending = false;
    _text_visible = true;
    _text_visible_since_ms = input.now_ms;
}

if (_episode_active && _sound_pending) {
    if (!input.speaker_audible) {
        _sound_pending = false;
    } else if (input.playback_available) {
        actions.play_sound = true;
        _sound_pending = false;
    }
}
refreshPhase();
```

Clamp the confirmation count at two, make `rearm()` hide visible text and clear
all pending state, and make `deferSound()` restore `_sound_pending` only while an
episode is active.

- [ ] **Step 5: Run and commit**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/low_battery_alert_logic_test.cpp \
  main/runtime/power/low_battery_alert_logic.cpp \
  -o /tmp/low_battery_alert_logic_test
/tmp/low_battery_alert_logic_test
git add main/runtime/power/low_battery_alert_logic.h \
  main/runtime/power/low_battery_alert_logic.cpp \
  tests/low_battery_alert_logic_test.cpp
git commit -m "feat: add global low-battery alert policy"
```

### Task 2: Read-Only HAL Observability

**Files:**
- Modify: `main/hal/hal.h`
- Modify: `main/hal/hal_pmic.cpp`
- Modify: `main/hal/hal_display.cpp`
- Modify: `main/hal/hal_audio.cpp`
- Create: `mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py`

- [ ] **Step 1: Write failing HAL contracts**

Create a Python contract that reads the four HAL files and requires:

```python
for token in ("batterySampleRevision()", "isDisplayVisible()", "isAudioPlaying()"):
    self.assertIn(token, header)
self.assertIn("_bat_sample_revision++", pmic)
self.assertIn("uint32_t Hal::batterySampleRevision()", pmic)
self.assertIn("bool Hal::isDisplayVisible()", display)
self.assertIn("return _bl_brightness > 0", display)
self.assertIn("bool isPlaying()", audio)
self.assertIn("bool Hal::isAudioPlaying()", audio)
```

Run:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent.tests.test_low_battery_alert_firmware_contract -v
```

Expected: failure because the observations are absent.

- [ ] **Step 2: Add valid battery-sample state**

Add `uint32_t batterySampleRevision();` beside `getBatteryLevel()` in `hal.h`.
In `hal_pmic.cpp`, protect the revision with the existing mutex:

```cpp
uint32_t _bat_sample_revision = 0;

void update_bat_level(uint8_t level)
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    _bat_level = level;
    _bat_sample_revision++;
}

uint32_t Hal::batterySampleRevision()
{
    std::lock_guard<std::mutex> lock(_bat_level_mutex);
    return _bat_sample_revision;
}
```

Do not increment the revision on a failed `readVbat()`. Revision zero means no
valid sample has completed.

- [ ] **Step 3: Add effective display visibility**

Add `bool isDisplayVisible();` beside the brightness getters and implement:

```cpp
bool Hal::isDisplayVisible()
{
    return _bl_brightness > 0;
}
```

If global AMOLED protection has been merged before execution, return its
effective physical output state instead; do not add a second brightness owner.

- [ ] **Step 4: Add thread-safe playback activity**

Add to the internal `AudioCodec`:

```cpp
bool isPlaying()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _is_playing;
}
```

Add `bool isAudioPlaying();` beside `audioPlay()` in `hal.h` and delegate it to
`_audio_codec.isPlaying()` in `hal_audio.cpp`.

- [ ] **Step 5: Verify and commit**

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent.tests.test_low_battery_alert_firmware_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/audio_backend_lifecycle_test.cpp \
  main/hal/audio/audio_backend_lifecycle.cpp \
  -o /tmp/audio_backend_lifecycle_test
/tmp/audio_backend_lifecycle_test
git add main/hal/hal.h main/hal/hal_pmic.cpp main/hal/hal_display.cpp \
  main/hal/hal_audio.cpp \
  mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py
git commit -m "feat: expose low-battery hardware observations"
```

### Task 3: Circular-Safe Notification Overlay

**Files:**
- Create: `main/apps/common/low_battery/low_battery_alert_layout.h`
- Create: `main/apps/common/low_battery/low_battery_alert_overlay.h`
- Create: `main/apps/common/low_battery/low_battery_alert_overlay.cpp`
- Modify: `tests/low_battery_alert_logic_test.cpp`
- Modify: `mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py`

- [ ] **Step 1: Add failing layout and overlay contracts**

Add compile-time checks to the native test:

```cpp
static_assert(low_battery_alert_layout::screenSize == 466);
static_assert(low_battery_alert_layout::bannerWidth == 260);
static_assert(low_battery_alert_layout::bannerHeight == 96);
static_assert(low_battery_alert_layout::bannerWidth <=
              low_battery_alert_layout::safeDiameter);
```

Require these source tokens in the Python contract:

```python
for token in (
    "font_puhui_14_1", "MontserratSemiBold26", "电量低", "请及时充电",
    "removeFlag(LV_OBJ_FLAG_CLICKABLE)",
    "removeFlag(LV_OBJ_FLAG_SCROLLABLE)", "moveForeground()",
):
    self.assertIn(token, overlay_source)
self.assertNotIn("GetHAL()", overlay_header + overlay_source)
```

- [ ] **Step 2: Create layout and overlay interfaces**

Use these layout values:

```cpp
namespace low_battery_alert_layout {
constexpr int screenSize = 466;
constexpr int safeDiameter = 420;
constexpr int bannerWidth = 260;
constexpr int bannerHeight = 96;
constexpr int centerYOffset = -48;
constexpr int cornerRadius = 8;
}
```

Expose this presentation-only interface:

```cpp
class LowBatteryAlertOverlay {
public:
    ~LowBatteryAlertOverlay();
    bool init(lv_obj_t* parent);
    void show(int battery_percent);
    void hide();
    void shutdown();
    bool visible() const;
};
```

- [ ] **Step 3: Implement the fixed banner**

Build a transparent 466x466 root on the supplied parent. Remove `CLICKABLE` and
`SCROLLABLE`, add `FLOATING`, and hide it initially. Add one 260x96 child at
`LV_ALIGN_CENTER, 0, -48`, radius 8, background `0x171717`, border `0xF05A3C`.
Add these exact labels:

```cpp
_title->setTextFont(&font_puhui_14_1);
_title->align(LV_ALIGN_TOP_LEFT, 22, 18);
_title->setText("电量低");

_percent->setTextFont(&MontserratSemiBold26);
_percent->setTextColor(lv_color_hex(0xFF8066));
_percent->align(LV_ALIGN_TOP_RIGHT, -22, 10);

_message->setTextFont(&font_puhui_14_1);
_message->align(LV_ALIGN_BOTTOM_MID, 0, -15);
_message->setText("请及时充电");
```

`show()` formats `%d%%`, unhides the root, and calls `moveForeground()`.
`shutdown()` resets labels before their banner and root parents.

- [ ] **Step 4: Verify and commit**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/low_battery_alert_logic_test.cpp \
  main/runtime/power/low_battery_alert_logic.cpp \
  -o /tmp/low_battery_alert_logic_test
/tmp/low_battery_alert_logic_test
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent.tests.test_low_battery_alert_firmware_contract -v
git add main/apps/common/low_battery tests/low_battery_alert_logic_test.cpp \
  mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py
git commit -m "feat: add global low-battery notification overlay"
```

### Task 4: Global Service and Main-Loop Integration

**Files:**
- Create: `main/runtime/power/global_low_battery_alert_service.h`
- Create: `main/runtime/power/global_low_battery_alert_service.cpp`
- Modify: `main/main.cpp`
- Modify: `mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py`

- [ ] **Step 1: Add failing service contracts**

Require the service to contain:

```python
for token in (
    "LowBatteryAlertLogic _logic", "LowBatteryAlertOverlay _overlay",
    "GetHAL().batterySampleRevision()", "GetHAL().isBatteryCharging(false)",
    "GetHAL().isDisplayVisible()", "GetHAL().isAudioPlaying()",
    "RuntimeOwner::System", "audio::play_melody", "_logic.deferSound()",
):
    self.assertIn(token, service_header + service_source)
```

Require `main.cpp` ordering:

```python
policy = main.index("GetGlobalLowBatteryAlertService().update()")
overlay = main.index("GetGlobalLowBatteryAlertService().updateOverlay()")
edge = main.index("GetGlobalEdgeControlService().update()")
audio = main.index("GetGlobalLowBatteryAlertService().applyPendingAudio()")
mooncake = main.index("GetMooncake().update()")
self.assertLess(policy, overlay)
self.assertLess(overlay, edge)
self.assertLess(edge, audio)
self.assertLess(audio, mooncake)
```

Also read `main/apps/app_setup/app_setup.cpp` and assert that it does not contain
`GetGlobalLowBatteryAlertService`; Settings may disable edge gestures but must
not disable this service.

- [ ] **Step 2: Create the service interface**

```cpp
class GlobalLowBatteryAlertService {
public:
    bool init();
    void update();
    void updateOverlay();
    void applyPendingAudio();
    void shutdown();

private:
    static constexpr uint32_t soundOwnershipTimeoutMs = 600;
    LowBatteryAlertLogic _logic;
    LowBatteryAlertOverlay _overlay;
    bool _initialized = false;
    bool _overlay_available = false;
    uint32_t _last_battery_revision = 0;
    bool _show_pending = false;
    bool _hide_pending = false;
    uint8_t _show_percent = 0;
    bool _audio_start_pending = false;
    bool _system_playback_owned = false;
    uint32_t _sound_started_ms = 0;
};

GlobalLowBatteryAlertService& GetGlobalLowBatteryAlertService();
```

- [ ] **Step 3: Implement policy sampling outside LVGL**

`update()` runs every main-loop iteration. Read the battery revision once and
mark an observation due only when a new nonzero revision appears. Build
`LowBatteryAlertInput` from HAL and resources:

```cpp
const auto resources = GetResourceSupervisor().snapshot();
const uint32_t battery_revision = GetHAL().batterySampleRevision();
const bool sample_due = battery_revision != 0 &&
    battery_revision != _last_battery_revision;
if (sample_due) _last_battery_revision = battery_revision;
const bool playback_available = !_system_playback_owned &&
    !resources.blocked && resources.playback_owner == RuntimeOwner::None &&
    !GetHAL().isAudioPlaying();

LowBatteryAlertInput input{
    .now_ms = now_ms,
    .battery_sample_due = sample_due,
    .battery_sample_valid = sample_due,
    .battery_percent = sample_due ? GetHAL().getBatteryLevel() : 0,
    .external_power = sample_due && GetHAL().isBatteryCharging(false),
    .display_visible = GetHAL().isDisplayVisible(),
    .playback_available = playback_available,
    .speaker_audible = GetHAL().getSpeakerVolume() > 0,
};
```

Queue `show_text`, `hide_text`, and `play_sound` actions. Do not call LVGL from
this method.

- [ ] **Step 4: Implement overlay and audio phases**

`updateOverlay()` applies hide before show under the caller's LVGL lock.
`applyPendingAudio()` runs outside that lock and follows this exact gate:

```cpp
if (GetHAL().getSpeakerVolume() <= 0) return;
const auto resources = GetResourceSupervisor().snapshot();
if (resources.blocked || resources.playback_owner != RuntimeOwner::None ||
    GetHAL().isAudioPlaying()) {
    _logic.deferSound();
    return;
}
if (GetResourceSupervisor().acquirePlayback(RuntimeOwner::System) !=
    AcquireResult::Granted) {
    _logic.deferSound();
    return;
}
_system_playback_owned = true;
_sound_started_ms = GetHAL().millis();
audio::play_melody({76, 72}, 0.09f, 0.8f);
```

Release System ownership when HAL reports idle or after 600 ms. The deadline is
a guard against an unrelated sound starting after the warning and extending
ownership indefinitely. `shutdown()` must release owned playback.

- [ ] **Step 5: Integrate `main.cpp`**

Initialize the alert service before edge controls under the existing startup
LVGL lock. Use this loop order:

```cpp
GetRuntimeSupervisor().update(GetHAL().millis());
GetGlobalLowBatteryAlertService().update();
{
    LvglLockGuard lock;
    GetGlobalLowBatteryAlertService().updateOverlay();
    GetGlobalEdgeControlService().update();
}
GetGlobalLowBatteryAlertService().applyPendingAudio();
GetGlobalEdgeControlService().applyPendingHardwareUpdate();
GetMooncake().update();
```

If global AMOLED protection exists at execution time, keep its update before
low-battery sampling so physical visibility is current.

- [ ] **Step 6: Verify, build, and commit**

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent.tests.test_low_battery_alert_firmware_contract -v
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null
idf.py -B build-unified reconfigure
idf.py -B build-unified build
git add main/runtime/power/global_low_battery_alert_service.h \
  main/runtime/power/global_low_battery_alert_service.cpp main/main.cpp \
  mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py
git commit -m "feat: integrate global low-battery alerts"
```

### Task 5: Standard Validation Gate

**Files:**
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py`

- [ ] **Step 1: Add the failing runner contract**

```python
runner = (ROOT / "scripts" / "run_unified_offline_validation.sh").read_text()
self.assertIn("[19/21] Global low-battery alert host test", runner)
self.assertIn("tests/low_battery_alert_logic_test.cpp", runner)
self.assertIn("main/runtime/power/low_battery_alert_logic.cpp", runner)
self.assertIn('"$HOST_TEST_DIR/low_battery_alert_logic_test"', runner)
```

- [ ] **Step 2: Add stage 19 of 21**

Change all denominators from 20 to 21, then insert after edge controls:

```bash
echo "[19/21] Global low-battery alert host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/low_battery_alert_logic_test.cpp \
    main/runtime/power/low_battery_alert_logic.cpp \
    -o "$HOST_TEST_DIR/low_battery_alert_logic_test"
"$HOST_TEST_DIR/low_battery_alert_logic_test"
```

Renumber Codex UI to 20 and the clean build to 21.

- [ ] **Step 3: Run focused gates and commit**

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent.tests.test_low_battery_alert_firmware_contract -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/low_battery_alert_logic_test.cpp \
  main/runtime/power/low_battery_alert_logic.cpp \
  -o /tmp/low_battery_alert_logic_test
/tmp/low_battery_alert_logic_test
bash -n scripts/run_unified_offline_validation.sh
git add scripts/run_unified_offline_validation.sh \
  mac-codex-agent/tests/test_low_battery_alert_firmware_contract.py
git commit -m "test: gate global low-battery alerts"
```

### Task 6: Complete Verification and Hardware Acceptance

**Files:**
- Verify: all files changed by Tasks 1-5
- Generated only: `build-unified/`

- [ ] **Step 1: Run the complete offline gate**

```bash
git diff --check main...HEAD
PYTHON=/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  ./scripts/run_unified_offline_validation.sh
```

Expected: 21 stages pass and the final line reports `Unified offline validation
passed`.

- [ ] **Step 2: Check capacity and scope**

Read `build-unified/unified-runtime-metrics.json`. Compare `app_bytes` with the
design baseline of 6,052,784 bytes and require the existing `0x660000` capacity
gate to pass. Verify that no bitmap or encoded audio asset was added.

```bash
git status --short --branch
git diff --stat main...HEAD
git diff --check main...HEAD
```

- [ ] **Step 3: Detect and flash available hardware**

```bash
ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null
./scripts/flash_unified_candidate.sh --port /dev/cu.usbmodem1101
```

Replace the example port with the detected device. Try automatic reset first;
request manual download mode only if it fails. If manual mode is used, tell the
user immediately after flashing so they can exit it.

- [ ] **Step 4: Perform hardware acceptance**

Verify and report each result independently:

1. Two samples below 30 percent produce one tone and one four-second banner.
2. Launcher, Assistant, Settings, and Clock do not retrigger or clip the banner.
3. Screen-off stays dark, the tone plays, and wake shows a fresh four seconds.
4. Assistant speech is not interrupted; the tone plays after playback releases.
5. External power suppresses pending delivery and rearms.
6. Recovery to 35 percent allows one later low episode.

- [ ] **Step 5: Final review without merging**

```bash
git status --short --branch
git log --oneline --decorate main..HEAD
git diff --check main...HEAD
```

Expected: clean feature branch with focused policy, HAL, overlay, integration,
and gate commits. Do not merge into `main` until hardware behavior is confirmed.
