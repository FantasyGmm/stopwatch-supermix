# Assistant Lifecycle Trace and Graceful Close Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the physical two-button `Closing` hang and make every Assistant lifecycle run reproducible through correlated debug traces and an automated graceful-close command.

**Architecture:** The HAL button path asks a pure ownership policy before playing feedback, so supervised playback cannot be reopened by legacy SFX. Assistant lifecycle boundaries write allocation-free records to a debug-only ring buffer, while physical and USB graceful-close triggers converge on one `AppAssistant` method. The USB host tool can request status, dump traces, and run the same stop path as the watch gesture.

**Tech Stack:** ESP-IDF 5.5, C++17, Mooncake, Brookesia/XiaoZhi, USB Serial/JTAG, host C++ tests, Python `unittest`.

---

### Task 1: Protect supervised playback from button SFX

**Files:**
- Create: `main/hal/button_feedback_policy.h`
- Create: `main/hal/button_feedback_policy.cpp`
- Modify: `main/hal/hal_button.cpp`
- Create: `tests/button_feedback_policy_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing policy test**

```cpp
#include "hal/button_feedback_policy.h"
#include <cassert>

int main()
{
    assert(ShouldPlayButtonSfx(true, RuntimeOwner::None));
    assert(!ShouldPlayButtonSfx(false, RuntimeOwner::None));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::Assistant));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::Codex));
    assert(!ShouldPlayButtonSfx(true, RuntimeOwner::System));
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/button_feedback_policy_test.cpp main/hal/button_feedback_policy.cpp -o /tmp/button_feedback_policy_test
```

Expected: compilation fails because the policy files or symbol do not exist.

- [ ] **Step 3: Implement the pure policy**

```cpp
// button_feedback_policy.h
#pragma once
#include "runtime/resource_supervisor.h"
bool ShouldPlayButtonSfx(bool configured, RuntimeOwner playback_owner);

// button_feedback_policy.cpp
#include "button_feedback_policy.h"
bool ShouldPlayButtonSfx(bool configured, RuntimeOwner playback_owner)
{
    return configured && playback_owner == RuntimeOwner::None;
}
```

In `Hal::updateButtonStates()`, capture `GetResourceSupervisor().snapshot().playback_owner` once and replace both `config.sfxEnabled` checks with `ShouldPlayButtonSfx(config.sfxEnabled, playback_owner)`. Keep vibration unchanged. Log one `HAL-BTN` informational suppression record on the press edge with the numeric owner.

- [ ] **Step 4: Add the test to offline validation and verify GREEN**

Compile and run the new test immediately after the Resource Supervisor test. Renumber the script progress labels from 9 to 10. Expected: the focused test and full pre-build host suite pass.

- [ ] **Step 5: Commit the ownership fix**

```bash
git add main/hal/button_feedback_policy.* main/hal/hal_button.cpp tests/button_feedback_policy_test.cpp scripts/run_unified_offline_validation.sh
git commit -m "Prevent button SFX from stealing supervised playback"
```

### Task 2: Add the allocation-free Assistant trace core

**Files:**
- Create: `main/runtime/assistant/assistant_trace.h`
- Create: `main/runtime/assistant/assistant_trace.cpp`
- Create: `tests/assistant_trace_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing ring-buffer test**

Test a small `AssistantTraceBuffer<3>` with records for Open, StartRequest, StateTransition, and Release. Assert sequence order, overwrite of the oldest record, `dropped()==1`, `clear()` behavior, and `AssistantTraceName(...)` values for every public enum used in serial output.

```cpp
AssistantTraceBuffer<3> trace;
trace.record({.session_id = 7, .event = AssistantTraceEvent::Open});
trace.record({.session_id = 7, .event = AssistantTraceEvent::StartRequest});
trace.record({.session_id = 7, .event = AssistantTraceEvent::StateTransition});
trace.record({.session_id = 7, .event = AssistantTraceEvent::Release});
assert(trace.size() == 3);
assert(trace.dropped() == 1);
assert(trace.at(0).event == AssistantTraceEvent::StartRequest);
assert(trace.at(2).sequence == 4);
trace.clear();
assert(trace.size() == 0 && trace.dropped() == 0);
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/assistant_trace_test.cpp main/runtime/assistant/assistant_trace.cpp -o /tmp/assistant_trace_test
```

Expected: compilation fails because the trace types do not exist.

- [ ] **Step 3: Implement fixed records and buffer**

Define compact enums `AssistantTraceComponent`, `AssistantTraceEvent`, `AssistantTraceDetail`, and `AssistantTraceResult`. Define `AssistantTraceRecord` with sequence, session, timestamp, component, event, state, detail, result, and four signed diagnostic values. Implement the capacity as a template backed by `std::array`, with no heap allocation. Provide a 64-record `GetAssistantTrace()` singleton only when `CONFIG_STOPWATCH_DEBUG_COMMANDS=1`; disabled builds expose inline no-op record/dump/clear functions and no record array.

The renderer must emit one stable line:

```cpp
ESP_LOGI("AssistantTrace",
         "ASTRACE sid=%llu seq=%u t=%u comp=%s event=%s state=%s detail=%s result=%s v0=%ld v1=%ld v2=%ld v3=%ld",
         record.session_id, record.sequence, record.timestamp_ms,
         AssistantTraceName(record.component), AssistantTraceName(record.event),
         AssistantTraceName(record.state), AssistantTraceName(record.detail),
         AssistantTraceName(record.result), record.value0, record.value1,
         record.value2, record.value3);
```

- [ ] **Step 4: Add the host test and verify GREEN**

Compile with `-DCONFIG_STOPWATCH_DEBUG_COMMANDS=1`, add it before the USB parser test, and renumber the script. Expected: ordering and overflow assertions pass.

- [ ] **Step 5: Commit the trace core**

```bash
git add main/runtime/assistant/assistant_trace.* tests/assistant_trace_test.cpp scripts/run_unified_offline_validation.sh
git commit -m "Add bounded Assistant lifecycle trace"
```

### Task 3: Converge physical and debug close triggers

**Files:**
- Create: `main/apps/app_assistant/assistant_close_controller.h`
- Create: `main/apps/app_assistant/assistant_close_controller.cpp`
- Create: `tests/assistant_close_controller_test.cpp`
- Modify: `main/apps/app_assistant/app_assistant.h`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing one-shot controller test**

```cpp
AssistantCloseController close;
assert(close.request(AssistantCloseTrigger::PhysicalHome));
assert(close.requested());
assert(close.trigger() == AssistantCloseTrigger::PhysicalHome);
assert(!close.request(AssistantCloseTrigger::DebugHome));
close.reset();
assert(close.request(AssistantCloseTrigger::DebugHome));
assert(close.trigger() == AssistantCloseTrigger::DebugHome);
```

- [ ] **Step 2: Run the focused test and verify RED**

Expected: compilation fails because `AssistantCloseController` is missing.

- [ ] **Step 3: Implement the controller and the single app path**

The controller stores one trigger and rejects later requests until reset. Replace `_close_requested` with it. Add:

```cpp
bool AppAssistant::requestGracefulClose(AssistantCloseTrigger trigger)
{
    if (!_close_controller.request(trigger)) return false;
    GetAssistantTrace().recordCloseRequest(GetAssistantRuntime().sessionId(), trigger);
    { LvglLockGuard lock; _view->setState(AssistantViewState::Closing); }
    const bool accepted = GetRuntimeSupervisor().requestStopAssistant();
    GetAssistantTrace().recordStopRequestResult(GetAssistantRuntime().sessionId(), accepted);
    return accepted;
}
```

The physical `GoHome` branch calls `requestGracefulClose(PhysicalHome)`. A debug request sets a main-thread flag, and the next `onRunning()` calls `requestGracefulClose(DebugHome)`. All existing close/event filtering reads `close_controller.requested()`.

- [ ] **Step 4: Verify the focused and existing host tests**

Add the controller test to offline validation. Expected: both triggers are one-shot and reset independently.

- [ ] **Step 5: Commit the unified close path**

```bash
git add main/apps/app_assistant/assistant_close_controller.* main/apps/app_assistant/app_assistant.* tests/assistant_close_controller_test.cpp scripts/run_unified_offline_validation.sh
git commit -m "Unify Assistant graceful close triggers"
```

### Task 4: Instrument runtime lifecycle and timeout evidence

**Files:**
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `main/runtime/runtime_supervisor.cpp`
- Modify: `main/hal/hal_button.cpp`
- Modify: `tests/assistant_trace_test.cpp`

- [ ] **Step 1: Extend trace assertions before integration**

Add records that encode state transition, action submission, lifecycle callback, resource decision, timeout, and release diagnostics. Assert names and retained values exactly. Run the test; expected RED until all enum names and fields exist.

- [ ] **Step 2: Record only lifecycle boundaries**

Add records at these exact points:

- `RuntimeSupervisor::startAssistant`: before request and after result;
- `AssistantRuntime::start`: accepted session, Wi-Fi decision, capture decision, playback decision;
- `submitAction`: submit accepted/rejected and async failure;
- lifecycle callback: Activated, Started, Stopped, unexpected, and stale rejection;
- `requestStop`: state transition, duplicate request, submit result;
- `update`: stop timeout with elapsed time;
- `releaseSession`: before and after disconnect/release, with binding/subscription counts and final owners;
- `Hal::updateButtonStates`: suppressed SFX owner;
- `RuntimeSupervisor::finishStoppedBoundary`: heap, PSRAM, task delta, and leak-gate result.

Do not trace audio packets or text payloads. Record only conversation kind and payload byte count.

- [ ] **Step 3: Keep callback writes allocation-free**

Callback code writes numeric records only. It must not render with `ESP_LOGI`; `RuntimeSupervisor::update()` drains and renders newly recorded entries on the main thread. Verify that the trace test can record from a second `std::thread` without corrupting sequence order; protect the short ring mutation with the existing ESP-IDF critical-section primitive in firmware and a mutex in `STOPWATCH_HOST_TEST`.

- [ ] **Step 4: Run the focused and complete host suite**

Expected: trace concurrency, ordering, and existing state/mailbox tests pass under `-Wall -Wextra -Werror`.

- [ ] **Step 5: Commit runtime instrumentation**

```bash
git add main/runtime/assistant/assistant_runtime.cpp main/runtime/runtime_supervisor.cpp main/hal/hal_button.cpp tests/assistant_trace_test.cpp
git commit -m "Trace Assistant lifecycle boundaries"
```

### Task 5: Add Assistant USB debug commands

**Files:**
- Create: `main/debug/debug_assistant_control.h`
- Create: `main/debug/debug_assistant_control.cpp`
- Modify: `main/debug/debug_command_parser.h`
- Modify: `main/debug/debug_command_parser.cpp`
- Modify: `main/debug/debug_command_service.cpp`
- Modify: `main/apps/app_assistant/app_assistant.h`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `tests/debug_command_test.cpp`

- [ ] **Step 1: Extend parser/control tests and verify RED**

Assert parsing and dispatch for:

```text
debug assistant home
debug assistant status
debug assistant trace
debug assistant trace clear
```

Assert that an unregistered target returns BUSY, home calls the target once, status/trace call their exact methods, and clear resets the trace. Expected: enum and control symbols are missing.

- [ ] **Step 2: Add a narrowly scoped debug target**

Define:

```cpp
class DebugAssistantTarget {
public:
    virtual ~DebugAssistantTarget() = default;
    virtual bool requestDebugGracefulHome() = 0;
    virtual void logDebugAssistantStatus() = 0;
};
```

Register `AppAssistant` in `onOpen()` and unregister it in `onClose()`. Trace dump/clear call the global trace directly; home/status require the active target.

- [ ] **Step 3: Emit deterministic acknowledgements**

Use exactly:

```text
DEBUG ASSISTANT HOME OK|BUSY
DEBUG ASSISTANT STATUS OK|BUSY
DEBUG ASSISTANT TRACE OK
DEBUG ASSISTANT TRACE CLEAR OK
```

Keep `debug app home` unchanged as emergency force-close and add one trace record identifying it as `ForceHome`.

- [ ] **Step 4: Run parser/control tests**

Expected: every command accepts optional carriage return, malformed suffixes are rejected, and legacy app commands still pass.

- [ ] **Step 5: Commit debug firmware control**

```bash
git add main/debug main/apps/app_assistant/app_assistant.* tests/debug_command_test.cpp
git commit -m "Add Assistant trace and graceful-home debug commands"
```

### Task 6: Extend host automation and failure capture

**Files:**
- Modify: `scripts/watch_debug.py`
- Modify: `mac-codex-agent/tests/test_watch_debug.py`
- Create: `scripts/validate_assistant_graceful_close.py`
- Create: `mac-codex-agent/tests/test_assistant_graceful_close_validation.py`

- [ ] **Step 1: Write failing Python command tests**

Assert byte payloads and response classification for every new command. Add a fake serial transcript test in which the validator sees first-running, close-request, Stopping, Stopped, Release, Launcher open, and zero owners. Add failure cases for missing Stopped, reset markers, nonzero playback owner, and timeout; each must request a trace dump before returning failure.

- [ ] **Step 2: Run Python tests and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_watch_debug mac-codex-agent.tests.test_assistant_graceful_close_validation
```

Expected: unsupported Assistant scope/validator symbols fail.

- [ ] **Step 3: Implement CLI commands and validator**

Add `assistant home|status|trace|trace-clear` subcommands. The validator performs two sessions, keys every wait by `sid`, rejects `rst:`/panic/watchdog lines, validates the ordered `ASTRACE` milestones, and checks the final diagnostic values. On any exception it sends `debug assistant trace` once and prints the captured transcript path.

- [ ] **Step 4: Run Python tests and offline validation**

Expected: all Python tests pass, followed by every C++ host test.

- [ ] **Step 5: Commit host automation**

```bash
git add scripts/watch_debug.py scripts/validate_assistant_graceful_close.py mac-codex-agent/tests/test_watch_debug.py mac-codex-agent/tests/test_assistant_graceful_close_validation.py
git commit -m "Automate Assistant graceful-close diagnostics"
```

### Task 7: Build, flash, and verify the watch

**Files:**
- Modify: `docs/superpowers/reports/unified-runtime-validation.md`

- [ ] **Step 1: Run complete offline verification**

Run:

```bash
./scripts/run_unified_offline_validation.sh
```

Expected: all Python/C++ tests, clean unified build, capacity gate, and `git diff --check` pass.

- [ ] **Step 2: Flash the exact candidate with backup guard**

Run:

```bash
PYTHON=/Users/bytedance/.espressif/python_env/idf5.5_py3.9_env/bin/python \
./scripts/flash_unified_candidate.sh \
  --port /dev/cu.usbmodem1101 \
  --backup backups/watch-full-flash-20260718-173922.bin \
  --build-dir build-unified
```

Expected: backup digest guard passes, all candidate partitions verify, and the watch boots without reset loop.

- [ ] **Step 3: Run two automated graceful-close cycles**

Run:

```bash
mac-codex-agent/.venv/bin/python scripts/validate_assistant_graceful_close.py --port /dev/cu.usbmodem1101 --cycles 2
```

Expected: each cycle reaches Running, uses `debug assistant home`, observes the real `Stopped` callback, opens Launcher, and reports no bindings, subscriptions, Wi-Fi leases, capture owner, or playback owner. No reset marker appears.

- [ ] **Step 4: Verify the physical gesture once**

Open Assistant through USB, press both side buttons long enough to generate `GoHome`, and capture the same ordered trace. Expected: `detail=physical-home`, button SFX is suppressed with `playback_owner=Assistant`, Launcher opens, and status is clean. If no person is available, leave only this one physical-input check explicitly pending; do not substitute the USB check for it.

- [ ] **Step 5: Record evidence and commit**

Update the validation report with source commit, app SHA-256, size/capacity, device MAC, session IDs, resource deltas, and whether the physical-input check was performed.

```bash
git add docs/superpowers/reports/unified-runtime-validation.md
git commit -m "Record Assistant graceful-close hardware validation"
```
