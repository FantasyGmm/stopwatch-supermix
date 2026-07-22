# Assistant Signal Core UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Assistant text-only screen with the approved A2 Signal Core interface while preserving wake gating, update metadata, resource ownership, graceful close, and hardware navigation.

**Architecture:** Add one ESP-IDF-free presentation reducer that converts ordered Assistant signals into visual states. Forward the existing typed listening/speaking callbacks through the bounded mailbox, let `AppAssistant` reduce events and sample telemetry, and keep `AssistantView` responsible only for LVGL objects and deterministic waveform frames.

**Tech Stack:** C++17, ESP-IDF 5.5.4, LVGL 9, Smooth UI Toolkit wrappers, Python `unittest` source contracts, native C++ host tests, unified candidate build and size gates.

---

## File Map

**Create**

- `main/apps/app_assistant/assistant_presentation_model.h`: ESP-IDF-free typed visual-state reducer.
- `tests/assistant_presentation_model_test.cpp`: ordering and precedence tests for the reducer.
- `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py`: firmware-source contracts for layout, event routing, assets, animation ownership, and noninteractive update metadata.

**Modify**

- `main/runtime/assistant/assistant_event_mailbox.h`: add typed listening/speaking status events and their boolean payload.
- `main/runtime/assistant/assistant_event_mailbox.cpp`: retain the latest listening/speaking status in bounded persistent slots.
- `main/runtime/assistant/assistant_runtime.cpp`: publish real listening/speaking changes after existing session/source validation.
- `tests/assistant_event_mailbox_test.cpp`: verify ordering, payloads, persistence, and stale-session removal.
- `main/apps/app_assistant/view/assistant_view.h`: expose Signal Core state, content, telemetry, update, and animation methods.
- `main/apps/app_assistant/view/assistant_view.cpp`: implement the fixed round-screen layout and native nine-bar waveform.
- `main/apps/app_assistant/app_assistant.h`: own the reducer and telemetry interval state.
- `main/apps/app_assistant/app_assistant.cpp`: route runtime events, drive animation, and update battery/connectivity.
- `mac-codex-agent/tests/test_firmware_update_notice_contract.py`: update the expected notice placement while retaining metadata-only behavior.
- `scripts/run_unified_offline_validation.sh`: compile the new reducer host test and renumber validation stages.

No image, GIF, font, task, timer, transcript-history, or audio-analysis file is added.

## Task 1: Add The Pure Presentation Reducer

**Files:**

- Create: `main/apps/app_assistant/assistant_presentation_model.h`
- Create: `tests/assistant_presentation_model_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing host test**

Create `tests/assistant_presentation_model_test.cpp`:

```cpp
#include "apps/app_assistant/assistant_presentation_model.h"

#include <cassert>

int main()
{
    AssistantPresentationModel model;
    assert(model.state() == AssistantVisualState::Connecting);

    model.apply(AssistantPresentationSignal::Activating);
    assert(model.state() == AssistantVisualState::Activating);

    model.apply(AssistantPresentationSignal::Running);
    assert(model.state() == AssistantVisualState::WaitingWake);

    model.apply(AssistantPresentationSignal::ListeningChanged, false);
    assert(model.state() == AssistantVisualState::WaitingWake);
    model.apply(AssistantPresentationSignal::ListeningChanged, true);
    assert(model.state() == AssistantVisualState::Listening);

    model.apply(AssistantPresentationSignal::UserText);
    assert(model.state() == AssistantVisualState::Thinking);
    model.apply(AssistantPresentationSignal::ListeningChanged, false);
    assert(model.state() == AssistantVisualState::Thinking);

    model.apply(AssistantPresentationSignal::AgentText);
    assert(model.state() == AssistantVisualState::Speaking);
    model.apply(AssistantPresentationSignal::SpeakingChanged, false);
    assert(model.state() == AssistantVisualState::WaitingWake);

    model.apply(AssistantPresentationSignal::Closing);
    assert(model.state() == AssistantVisualState::Closing);
    model.apply(AssistantPresentationSignal::ListeningChanged, true);
    assert(model.state() == AssistantVisualState::Closing);
    model.apply(AssistantPresentationSignal::AgentText);
    assert(model.state() == AssistantVisualState::Closing);

    model.reset();
    model.apply(AssistantPresentationSignal::Failed);
    assert(model.state() == AssistantVisualState::Failed);
    model.apply(AssistantPresentationSignal::SpeakingChanged, false);
    assert(model.state() == AssistantVisualState::Failed);
    model.apply(AssistantPresentationSignal::Connecting);
    assert(model.state() == AssistantVisualState::Connecting);
    model.apply(AssistantPresentationSignal::Failed);
    model.apply(AssistantPresentationSignal::StopTimedOut);
    assert(model.state() == AssistantVisualState::StopTimedOut);
    return 0;
}
```

- [ ] **Step 2: Run the host test to verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_presentation_model_test.cpp \
  -o /tmp/assistant_presentation_model_test
```

Expected: compilation fails because `assistant_presentation_model.h` does not exist.

- [ ] **Step 3: Implement the reducer as a small header-only state machine**

Create `main/apps/app_assistant/assistant_presentation_model.h`:

```cpp
#pragma once

#include <cstdint>

enum class AssistantVisualState : std::uint8_t {
    Connecting,
    Activating,
    WaitingWake,
    Listening,
    Thinking,
    Speaking,
    Failed,
    Closing,
    StopTimedOut,
};

enum class AssistantPresentationSignal : std::uint8_t {
    Connecting,
    Activating,
    Running,
    ListeningChanged,
    UserText,
    AgentText,
    SpeakingChanged,
    Failed,
    Closing,
    StopTimedOut,
};

class AssistantPresentationModel {
public:
    AssistantVisualState state() const { return _state; }

    void reset() { _state = AssistantVisualState::Connecting; }

    void apply(AssistantPresentationSignal signal, bool active = true)
    {
        if (_state == AssistantVisualState::Failed) {
            if (signal == AssistantPresentationSignal::Connecting) {
                _state = AssistantVisualState::Connecting;
            } else if (signal == AssistantPresentationSignal::StopTimedOut) {
                _state = AssistantVisualState::StopTimedOut;
            }
            return;
        }
        if (_state == AssistantVisualState::Closing ||
            _state == AssistantVisualState::StopTimedOut) {
            if (signal == AssistantPresentationSignal::StopTimedOut) {
                _state = AssistantVisualState::StopTimedOut;
            }
            return;
        }

        switch (signal) {
            case AssistantPresentationSignal::Connecting:
                _state = AssistantVisualState::Connecting;
                break;
            case AssistantPresentationSignal::Activating:
                _state = AssistantVisualState::Activating;
                break;
            case AssistantPresentationSignal::Running:
                _state = AssistantVisualState::WaitingWake;
                break;
            case AssistantPresentationSignal::ListeningChanged:
                if (active) {
                    _state = AssistantVisualState::Listening;
                }
                break;
            case AssistantPresentationSignal::UserText:
                _state = AssistantVisualState::Thinking;
                break;
            case AssistantPresentationSignal::AgentText:
                _state = AssistantVisualState::Speaking;
                break;
            case AssistantPresentationSignal::SpeakingChanged:
                _state = active ? AssistantVisualState::Speaking
                                : AssistantVisualState::WaitingWake;
                break;
            case AssistantPresentationSignal::Failed:
                _state = AssistantVisualState::Failed;
                break;
            case AssistantPresentationSignal::Closing:
                _state = AssistantVisualState::Closing;
                break;
            case AssistantPresentationSignal::StopTimedOut:
                _state = AssistantVisualState::StopTimedOut;
                break;
        }
    }

private:
    AssistantVisualState _state = AssistantVisualState::Connecting;
};
```

- [ ] **Step 4: Run the test to verify GREEN**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_presentation_model_test.cpp \
  -o /tmp/assistant_presentation_model_test && \
  /tmp/assistant_presentation_model_test
```

Expected: exit code `0` with no output.

- [ ] **Step 5: Add the test to offline validation**

In `scripts/run_unified_offline_validation.sh`, change the stage total from `16` to `17`, insert the presentation-model test immediately after the state-machine test, and increment subsequent stage numbers:

```bash
echo "[3/17] Assistant presentation model host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_presentation_model_test.cpp \
    -o "$HOST_TEST_DIR/assistant_presentation_model_test"
"$HOST_TEST_DIR/assistant_presentation_model_test"
```

Expected: the new stage runs without requiring ESP-IDF headers.

- [ ] **Step 6: Commit the reducer**

```bash
git add main/apps/app_assistant/assistant_presentation_model.h \
  tests/assistant_presentation_model_test.cpp \
  scripts/run_unified_offline_validation.sh
git commit -m "Add Assistant presentation state model"
```

## Task 2: Forward Real Listening And Speaking Status

**Files:**

- Modify: `main/runtime/assistant/assistant_event_mailbox.h`
- Modify: `main/runtime/assistant/assistant_event_mailbox.cpp`
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `tests/assistant_event_mailbox_test.cpp`
- Create: `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py`

- [ ] **Step 1: Add failing mailbox and source-contract assertions**

Extend `tests/assistant_event_mailbox_test.cpp` with a separate persistent-status case after the existing assertions:

```cpp
AssistantEventMailbox statuses(0);
statuses.publish({1, AssistantEventKind::State, "Running"});
statuses.publish({1, AssistantEventKind::ListeningStatus, {}, true});
statuses.publish({1, AssistantEventKind::SpeakingStatus, {}, true});
statuses.publish({1, AssistantEventKind::ListeningStatus, {}, false});
events = drain(statuses);
assert(events.size() == 3);
assert(events[0].kind == AssistantEventKind::State);
assert(events[1].kind == AssistantEventKind::SpeakingStatus);
assert(events[1].active);
assert(events[2].kind == AssistantEventKind::ListeningStatus);
assert(!events[2].active);

statuses.publish({1, AssistantEventKind::ListeningStatus, {}, true});
statuses.publish({1, AssistantEventKind::SpeakingStatus, {}, true});
statuses.publish({2, AssistantEventKind::State, "Starting"});
statuses.discardSessionsBefore(2);
events = drain(statuses);
assert(events.size() == 1);
assert(events[0].session_id == 2);
assert(events[0].kind == AssistantEventKind::State);
```

Create `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py` with the initial runtime contract:

```python
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantSignalCoreUiContractTests(unittest.TestCase):
    def test_runtime_forwards_real_listening_and_speaking_status(self):
        mailbox = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text()
        runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text()

        self.assertIn("ListeningStatus", mailbox)
        self.assertIn("SpeakingStatus", mailbox)
        self.assertRegex(
            runtime,
            r"ListeningStatusChanged.*?mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::ListeningStatus,\s*\{\},\s*listening\}\)",
        )
        self.assertRegex(
            runtime,
            r"SpeakingStatusChanged.*?mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::SpeakingStatus,\s*\{\},\s*speaking\}\)",
        )


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run focused tests to verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py -v
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test
```

Expected: Python fails because the new event kinds are absent; C++ fails because the aggregate payload and enum values are absent.

- [ ] **Step 3: Add typed bounded mailbox events**

In `main/runtime/assistant/assistant_event_mailbox.h`, extend the enum and payload:

```cpp
enum class AssistantEventKind : uint8_t {
    State,
    ActivationCode,
    Error,
    UserText,
    AgentText,
    Emotion,
    UpdateAvailable,
    ListeningStatus,
    SpeakingStatus,
};

struct AssistantEvent {
    uint64_t session_id = 0;
    AssistantEventKind kind = AssistantEventKind::State;
    std::string text;
    bool active = false;
};
```

Increase `_persistent` from four to six slots. In `persistentIndex()` map `ListeningStatus` to slot `4` and `SpeakingStatus` to slot `5`; keep user text, agent text, and emotion transient.

- [ ] **Step 4: Publish status from the existing validated callbacks**

Inside each successful `callbackIsCurrentXiaozhi(...)` block in `AssistantRuntime::Impl::subscribe`, keep the trace call and append:

```cpp
mailbox.publish({session_id, AssistantEventKind::ListeningStatus, {}, listening});
```

and:

```cpp
mailbox.publish({session_id, AssistantEventKind::SpeakingStatus, {}, speaking});
```

Do not call LVGL, allocate a new queue, or publish outside the existing source/session guard.

Add exhaustive compatibility cases to `AppAssistant::applyRuntimeEvent()` so this task remains buildable before the reducer is integrated:

```cpp
case AssistantEventKind::ListeningStatus:
    if (event.active) {
        _view->setState(AssistantViewState::Listening);
    }
    break;
case AssistantEventKind::SpeakingStatus:
    _view->setState(event.active ? AssistantViewState::Speaking
                                 : AssistantViewState::Listening);
    break;
```

- [ ] **Step 5: Run focused tests to verify GREEN**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py -v
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test && \
  /tmp/assistant_event_mailbox_test
```

Expected: both commands pass.

- [ ] **Step 6: Run update-notice exhaustiveness contract**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Expected: the exhaustive event-switch contract passes without a `default` branch.

- [ ] **Step 7: Commit runtime event forwarding**

```bash
git add main/runtime/assistant/assistant_event_mailbox.h \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  main/runtime/assistant/assistant_runtime.cpp \
  main/apps/app_assistant/app_assistant.cpp \
  tests/assistant_event_mailbox_test.cpp \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py
git commit -m "Forward Assistant voice status events"
```

## Task 3: Build The Signal Core View

**Files:**

- Modify: `main/apps/app_assistant/view/assistant_view.h`
- Modify: `main/apps/app_assistant/view/assistant_view.cpp`
- Modify: `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py`
- Modify: `mac-codex-agent/tests/test_firmware_update_notice_contract.py`

- [ ] **Step 1: Extend the source contract for the approved view**

Add these tests to `test_assistant_signal_core_ui_contract.py`:

```python
    def test_view_uses_fixed_signal_core_layout_without_new_assets(self):
        header = (ROOT / "main/apps/app_assistant/view/assistant_view.h").read_text()
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        combined = header + source

        self.assertIn("std::array<lv_obj_t*, 9>", header)
        self.assertIn("void update(uint32_t now_ms)", header)
        self.assertIn("setBattery", header)
        self.assertIn("setConnected", header)
        self.assertIn("font_puhui_14_1", source)
        self.assertNotIn("实时字幕", combined)
        self.assertNotRegex(combined, r"lv_timer_create|xTaskCreate|TaskHandle_t|FFT|gif")
        self.assertNotRegex(combined, r"LV_ALIGN_BOTTOM_MID,\s*0,\s*-[0-3][0-9]\b")

    def test_waiting_state_is_visually_distinct_and_silent(self):
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        self.assertIn("AssistantVisualState::WaitingWake", source)
        self.assertIn("等待唤醒", source)
        self.assertIn("你好小智", source)
        self.assertIn("inactive_wave", source)
```

Update `test_assistant_page_renders_exact_noninteractive_notice()` in `test_firmware_update_notice_contract.py` to require the compact top-row update label rather than `LV_ALIGN_BOTTOM_MID`, while retaining all forbidden install strings.

- [ ] **Step 2: Run the view contracts to verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Expected: failures report the old text-only view, old bottom update placement, and missing waveform/telemetry methods.

- [ ] **Step 3: Replace the view interface**

In `assistant_view.h`, remove the old local `AssistantViewState` enum, include `assistant_presentation_model.h`, alias the old type for a buildable intermediate commit, and expose this interface:

```cpp
using AssistantViewState = AssistantVisualState;

class AssistantView {
public:
    void init(lv_obj_t* parent);
    void update(uint32_t now_ms);
    void setState(AssistantVisualState state);
    void setConversationText(const char* text);
    void setUpdateVersion(const char* canonical_version);
    void setBattery(uint8_t percent, bool charging);
    void setConnected(bool connected);
    void setPrimaryText(const char* text);
    void setSecondaryText(const char* text);
    void destroy();

private:
    void applyDefaultCopy();
    void applyWaveFrame(uint8_t frame_index);
    void applyWaveColor(uint32_t color);

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _network_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _update_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _phase_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _content_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _hint_label;
    std::array<lv_obj_t*, 9> _wave_bars{};
    lv_obj_t* _wave_container = nullptr;
    lv_obj_t* _divider = nullptr;
    lv_obj_t* _network_dot = nullptr;
    lv_obj_t* _battery_body = nullptr;
    lv_obj_t* _battery_tip = nullptr;
    lv_obj_t* _battery_label = nullptr;
    AssistantVisualState _state = AssistantVisualState::Connecting;
    uint32_t _last_animation_ms = 0;
    uint8_t _frame_index = 0;
};
```

For this intermediate commit, `setPrimaryText()` sets `_title_label` and `setSecondaryText()` delegates to `setConversationText()`. Task 4 removes these compatibility methods after `AppAssistant` uses the final interface.

- [ ] **Step 4: Implement fixed geometry and native objects**

In `assistant_view.cpp`, keep one `466 x 466` black panel. Create raw LVGL objects with `lv_obj_remove_style_all()` for the nine bars, divider, network dot, battery outline, and battery tip. Use these geometry constants:

```cpp
constexpr int panel_size = 466;
constexpr int safe_text_width = 370;
constexpr int status_y = 34;
constexpr int phase_y = 68;
constexpr int wave_y = 88;
constexpr int wave_height = 132;
constexpr int title_y = 228;
constexpr int divider_y = 276;
constexpr int content_y = 294;
constexpr int content_height = 60;
constexpr int hint_bottom = -48;
constexpr int wave_bar_width = 14;
constexpr int wave_bar_gap = 12;
```

Set `_content_label` to fixed width/height with centered wrap and dot/clipping behavior. Set the update label in the top-center zone. Keep battery percentage centered inside a `44 x 22` outline.

- [ ] **Step 5: Add deterministic frame tables and state copy**

Define small constant frame tables in the anonymous namespace:

```cpp
using WaveFrame = std::array<int16_t, 9>;
constexpr WaveFrame inactive_wave{22, 22, 35, 22, 48, 22, 35, 22, 22};
constexpr std::array<WaveFrame, 4> active_wave{{
    {24, 49, 80, 61, 103, 61, 80, 49, 24},
    {42, 77, 45, 92, 64, 101, 52, 70, 31},
    {61, 35, 96, 54, 84, 47, 103, 42, 65},
    {31, 68, 52, 101, 59, 88, 43, 75, 26},
}};
constexpr std::array<WaveFrame, 4> thinking_wave{{
    {9, 16, 25, 35, 45, 35, 25, 16, 9},
    {12, 21, 32, 44, 55, 44, 32, 21, 12},
    {16, 27, 39, 52, 63, 52, 39, 27, 16},
    {12, 21, 32, 44, 55, 44, 32, 21, 12},
}};
```

`setState()` resets the frame index and applies the exact phase/title/default content, colors, hint visibility, and animation interval from the approved design. `setConversationText()` replaces only the content region. `update()` advances at `120 ms` for listening/speaking and `170 ms` for thinking/transition states; waiting/failure/timeout remain static.

- [ ] **Step 6: Implement compact status telemetry**

`setConnected(false)` renders a gray dot with `连接中`; `setConnected(true)` renders cyan with `在线`. `setBattery()` clamps to `0..100`, formats `%u%%` inside the outline, and colors the native outline/tip green while charging without adding a symbol or asset. `setUpdateVersion()` formats `v%s 可用`, remains noninteractive, and never creates a button or installer action.

- [ ] **Step 7: Run contracts and compile the view through ESP-IDF**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
idf.py -B build-unified reconfigure
idf.py -B build-unified build
```

Expected: source contracts pass and the complete firmware links. The compatibility alias/methods keep the existing app buildable until Task 4.

- [ ] **Step 8: Commit the view**

```bash
git add main/apps/app_assistant/view/assistant_view.h \
  main/apps/app_assistant/view/assistant_view.cpp \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py
git commit -m "Build Assistant signal core view"
```

## Task 4: Integrate Presentation State And Telemetry

**Files:**

- Modify: `main/apps/app_assistant/app_assistant.h`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py`

- [ ] **Step 1: Add failing integration contracts**

Append to `test_assistant_signal_core_ui_contract.py`:

```python
    def test_app_routes_running_to_waiting_and_status_events_exhaustively(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        self.assertIn("AssistantPresentationSignal::Running", app)
        self.assertIn("AssistantEventKind::ListeningStatus", app)
        self.assertIn("event.active", app)
        self.assertIn("AssistantEventKind::SpeakingStatus", app)
        self.assertNotRegex(
            app,
            r'event\.text == "Running"[\s\S]{0,180}AssistantVisualState::Listening',
        )

    def test_app_samples_battery_outside_the_lvgl_lock(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        function = app[app.index("void AppAssistant::updateTelemetry") :]
        sample = function.index("GetHAL().getBatteryLevel()")
        lock = function.index("LvglLockGuard")
        self.assertLess(sample, lock)
        self.assertIn("_view->setBattery", function[lock:])

    def test_signal_core_adds_no_task_timer_asset_or_full_font(self):
        paths = [
            ROOT / "main/apps/app_assistant/app_assistant.cpp",
            ROOT / "main/apps/app_assistant/view/assistant_view.cpp",
        ]
        combined = "\n".join(path.read_text() for path in paths)
        self.assertNotRegex(combined, r"xTaskCreate|lv_timer_create|esp_timer_create|FFT")
        self.assertNotIn("LV_IMG_DECLARE", combined)
```

- [ ] **Step 2: Run integration contracts to verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Expected: failures identify missing reducer ownership, status-event cases, and telemetry update.

- [ ] **Step 3: Add reducer and telemetry members**

In `app_assistant.h`, include `assistant_presentation_model.h` and add:

```cpp
AssistantPresentationModel _presentation;
uint32_t _last_telemetry_ms = 0;
bool _has_conversation_text = false;

void applyPresentationSignal(AssistantPresentationSignal signal, bool active = true);
void updateTelemetry(uint32_t now_ms);
```

Reset the model, telemetry timestamp, and conversation-text flag in `onOpen()`. Clear the timestamp and flag in `onClose()`.

- [ ] **Step 4: Drive view animation and battery from the app loop**

At the start of `onRunning()`, read `const uint32_t now = GetHAL().millis();`, then under a short LVGL lock call `_view->update(now)`. Once per `1000 ms`, call `updateTelemetry(now)`.

Implement telemetry with hardware reads before locking:

```cpp
void AppAssistant::updateTelemetry(uint32_t now_ms)
{
    if (_view == nullptr ||
        static_cast<uint32_t>(now_ms - _last_telemetry_ms) < 1000) {
        return;
    }
    _last_telemetry_ms = now_ms;
    const uint8_t battery = GetHAL().getBatteryLevel();
    const bool charging = GetHAL().isBatteryCharging();
    LvglLockGuard lock;
    if (_view) {
        _view->setBattery(battery, charging);
    }
}
```

- [ ] **Step 5: Route every event through the reducer**

Implement the reducer/view bridge:

```cpp
void AppAssistant::applyPresentationSignal(AssistantPresentationSignal signal, bool active)
{
    _presentation.apply(signal, active);
    if (!_view) {
        return;
    }
    _view->setState(_presentation.state());
    if (signal == AssistantPresentationSignal::Connecting) {
        _view->setConnected(false);
    } else if (signal == AssistantPresentationSignal::Activating ||
               signal == AssistantPresentationSignal::Running) {
        _view->setConnected(true);
    }
}
```

Replace the old state-to-view logic in `applyRuntimeEvent()` with this exhaustive switch while retaining the existing early close/session guards:

```cpp
switch (event.kind) {
    case AssistantEventKind::State:
        if (event.text == "Stopped" && _retry_available &&
            !_close_controller.requested()) {
            return;
        }
        if (_close_controller.requested() && event.text != "Stopping" &&
            event.text != "Stopped") {
            return;
        }
        if (event.text == "Starting" || event.text == "Connecting") {
            _has_conversation_text = false;
            applyPresentationSignal(AssistantPresentationSignal::Connecting);
        } else if (event.text == "Activating" || event.text == "Activated") {
            applyPresentationSignal(AssistantPresentationSignal::Activating);
        } else if (event.text == "Running") {
            _has_conversation_text = false;
            applyPresentationSignal(AssistantPresentationSignal::Running);
        } else if (event.text == "Stopping") {
            applyPresentationSignal(AssistantPresentationSignal::Closing);
        }
        break;
    case AssistantEventKind::ActivationCode:
        applyPresentationSignal(AssistantPresentationSignal::Activating);
        _view->setConversationText(event.text.c_str());
        break;
    case AssistantEventKind::Error:
        if (runtime_state == AssistantRuntimeState::StopTimedOut) {
            applyPresentationSignal(AssistantPresentationSignal::StopTimedOut);
        } else if (_close_controller.requested()) {
            applyPresentationSignal(AssistantPresentationSignal::Closing);
        } else {
            applyPresentationSignal(AssistantPresentationSignal::Failed);
            _retry_available = runtime_state == AssistantRuntimeState::Stopped;
        }
        _view->setConversationText(event.text.c_str());
        break;
    case AssistantEventKind::UserText:
        _has_conversation_text = true;
        applyPresentationSignal(AssistantPresentationSignal::UserText);
        _view->setConversationText(event.text.c_str());
        break;
    case AssistantEventKind::AgentText:
        _has_conversation_text = true;
        applyPresentationSignal(AssistantPresentationSignal::AgentText);
        _view->setConversationText(event.text.c_str());
        break;
    case AssistantEventKind::Emotion:
        if (!_has_conversation_text && !event.text.empty()) {
            _view->setConversationText(event.text.c_str());
        }
        break;
    case AssistantEventKind::UpdateAvailable:
        _view->setUpdateVersion(event.text.c_str());
        break;
    case AssistantEventKind::ListeningStatus:
        applyPresentationSignal(AssistantPresentationSignal::ListeningChanged,
                                event.active);
        break;
    case AssistantEventKind::SpeakingStatus:
        applyPresentationSignal(AssistantPresentationSignal::SpeakingChanged,
                                event.active);
        if (!event.active) {
            _has_conversation_text = false;
        }
        break;
}
```

Add `ListeningStatus` and `SpeakingStatus` to `isConversationEvent()` so the existing close suppression applies to them. Remove `setPrimaryText()`, `setSecondaryText()`, and the `AssistantViewState` compatibility alias from the view now that no app call depends on them.

In the existing right-button retry block, replace the direct old view-state call with:

```cpp
_has_conversation_text = false;
applyPresentationSignal(AssistantPresentationSignal::Connecting);
```

This explicitly exits the reducer's Failed state before `runtime.startAssistant()` publishes the new session events.

- [ ] **Step 6: Run focused and exhaustiveness tests to verify GREEN**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py -v
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_presentation_model_test.cpp \
  -o /tmp/assistant_presentation_model_test && \
  /tmp/assistant_presentation_model_test
```

Expected: all focused tests pass, the event switch handles every enum value without `default`, and `Running` no longer renders Listening.

- [ ] **Step 7: Build the integrated firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
idf.py -B build-unified build
```

Expected: successful link and partition-size check.

- [ ] **Step 8: Commit app integration**

```bash
git add main/apps/app_assistant/app_assistant.h \
  main/apps/app_assistant/app_assistant.cpp \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py
git commit -m "Integrate Assistant signal core states"
```

## Task 5: Validate Size, Lifecycle, And Hardware

**Files:**

- Modify only if evidence finds a defect: files already listed in Tasks 1-4
- Generated and untracked: `build-unified/*`

- [ ] **Step 1: Capture a clean preimplementation size baseline before Task 1 when executing this plan**

From commit `8db8bbf` in a temporary detached worktree, run:

```bash
./scripts/build_unified_candidate.sh --clean
cp build-unified/unified-runtime-metrics.json \
  /tmp/assistant-signal-core-baseline.json
```

Expected: the JSON records the baseline application bytes, assets bytes, hashes, and capacity gate. If this step was not run before implementation, report that no compatible before/after delta is available; do not invent one from an older build.

- [ ] **Step 2: Run the full automated gate on the completed branch**

Run:

```bash
PYTHON=/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  ./scripts/run_unified_offline_validation.sh
```

Expected: all 17 stages pass, including 250+ Python tests, every native host test, clean unified build, capacity gates, and `git diff --check`.

- [ ] **Step 3: Compare binary and assets sizes**

Run:

```bash
python3 - <<'PY'
import json
from pathlib import Path

before = json.loads(Path('/tmp/assistant-signal-core-baseline.json').read_text())
after = json.loads(Path('build-unified/unified-runtime-metrics.json').read_text())
for key in ('app_bytes', 'assets_bytes'):
    print(f"{key}: before={before[key]} after={after[key]} delta={after[key] - before[key]}")
print(f"capacity_gate_passed={after['capacity_gate_passed']}")
PY
```

Expected: assets delta is `0`; application remains below both `0x660000` release limit and the actual application partition capacity. Record the measured application delta in the branch handoff.

- [ ] **Step 4: Inspect commit scope and working tree**

Run:

```bash
git diff main...HEAD --stat
git log --oneline main..HEAD
git status --short --branch
```

Expected: only the design, plan, Signal Core implementation, tests, and validation runner changes are present. The feature worktree is clean.

- [ ] **Step 5: Request download mode before any device write**

Tell the user that the candidate is ready and explicitly ask them to enter download mode. Do not execute `idf.py flash`, `esptool`, or a hardware validation script before they confirm.

- [ ] **Step 6: Flash only after confirmation and report completion**

Use the repository's backup-gated full-candidate workflow, exact one-port discovery, and the newest complete local backup that still passes the script's checksum-sidecar validation:

```bash
PORT="$(PYTHONPATH=scripts python3 -c \
  'from watch_debug import discover_port; print(discover_port())')"
BACKUP="$(find backups -type f -name '*.bin' -size 16777216c -print | sort | tail -1)"
test -n "$BACKUP"
./scripts/run_unified_hardware_validation.sh \
  --port "$PORT" --backup "$BACKUP"
```

The validation script must reject a missing or mismatched backup sidecar before writing. After the write finishes, explicitly tell the user flashing is complete and ask them to exit download mode.

- [ ] **Step 7: Perform manual watch acceptance after the user exits download mode**

Verify and record each observation:

1. Assistant starts on Connecting with no clipped edge text.
2. Waiting For Wake remains silent and uses low gray bars.
3. Saying “你好小智” changes to Listening.
4. User recognition changes to Thinking and displays at most two lines.
5. Assistant output changes to Speaking and displays answer text without “实时字幕”.
6. Speaking completion returns to Waiting For Wake.
7. Wi-Fi, update version, and battery percentage remain inside the round safe area.
8. Two-button long press returns to Launcher without offset, freeze, or stale overlay.
9. Reopening Assistant does not preserve stale transcript or waveform state.
10. Retry and close-timeout behavior remain actionable.

- [ ] **Step 8: Fix only evidence-backed defects and repeat the affected gates**

For any failure, capture the exact visual state and serial trace, add or tighten the smallest automated regression test, apply a scoped fix, rerun the focused test plus unified build, then repeat only the relevant hardware step. Do not merge based on build-only evidence.

- [ ] **Step 9: Merge only after hardware acceptance**

After every automated and hardware item passes, merge `codex/assistant-ui-signal-core` into `main` using the repository's normal non-destructive merge workflow. Do not include unrelated worktree changes. Report the merge commit and retain the feature branch until the post-merge build is verified.
