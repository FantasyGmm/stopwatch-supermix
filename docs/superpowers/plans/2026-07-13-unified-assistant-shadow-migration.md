# Unified Assistant Shadow Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare every host-testable and build-verifiable part of the restart-free unified Assistant, including a non-default single-partition candidate and reversible flashing tools, so the later watch campaign is limited to hardware validation and corrective changes.

**Architecture:** Keep the checked-in legacy dual-OTA layout and recovery tools while building a separate `build-unified` candidate from `partitions_unified.csv`. Mooncake talks only to `RuntimeSupervisor`; `AssistantRuntime` owns the Brookesia Agent lifecycle; StopWatch HAL remains the sole hardware owner. Hardware status remains `Pending hardware` until the six-stage watch campaign passes.

**Tech Stack:** C++17, ESP-IDF 5.5.4, FreeRTOS, Mooncake, LVGL 9, ESP-Brookesia 0.7, Python `unittest`, host C++ tests, Bash, esptool

---

## Execution Rules

- Follow red-green-refactor for every behavior-bearing task.
- Run only the named focused test during RED; run the focused suite before each commit.
- Do not flash, erase, or access a serial port during Tasks 1–10.
- Keep `partitions.csv`, `scripts/flash_assistant_runtime.sh`, `scripts/flash_full_assistant_stack.sh`, and `scripts/force_boot_slot.py` until the hardware campaign passes.
- A successful host test or link is not hardware evidence.
- Use `build-unified/` and `sdkconfig.unified`; do not overwrite the current `build/` or tracked `sdkconfig`.
- The final candidate application must be at most `0x660000` bytes and `generated_assets.bin` at most `0x400000` bytes.

## File Map

### Pure lifecycle and host tests

- Create `main/runtime/assistant/assistant_state.h` and `.cpp`: legal Assistant lifecycle transitions and session generation.
- Create `main/runtime/assistant/assistant_event_mailbox.h` and `.cpp`: thread-safe persistent UI state plus bounded transient text/emotion events.
- Create `tests/assistant_state_machine_test.cpp`: transition, idempotence, timeout, and stale-session tests.
- Create `tests/assistant_event_mailbox_test.cpp`: coalescing, ordering, and overflow tests.

### Firmware runtime and UI

- Create `main/runtime/assistant/assistant_runtime.h` and `.cpp`: resource acquisition, Brookesia configuration, async actions, event subscriptions, stop acknowledgement, and release.
- Refactor `main/runtime/assistant/assistant_probe.h` and `.cpp`: thin debug wrapper around the production runtime.
- Create `main/runtime/runtime_supervisor.h` and `.cpp`: application-facing Assistant facade.
- Create `main/runtime/runtime_diagnostics.h` and `.cpp`: passive resource and memory snapshots.
- Create `main/apps/app_assistant/view/assistant_view.h` and `.cpp`: LVGL presentation only.
- Modify `main/apps/app_assistant/app_assistant.h` and `.cpp`: asynchronous runtime lifecycle; no partition switch or restart.
- Modify `main/main.cpp`: initialize the runtime supervisor after `ServiceRuntime`.

### Candidate build and reversible hardware tools

- Create `partitions_unified.csv`: non-default single-application candidate layout.
- Create `sdkconfig.unified.defaults`: candidate partition selection.
- Create `scripts/build_unified_candidate.sh`: isolated build and metrics gate.
- Create `scripts/backup_watch_flash.sh`: complete 16 MiB read and SHA256 sidecar.
- Create `scripts/restore_watch_flash.sh`: size/checksum validation and complete restore.
- Create `scripts/flash_unified_candidate.sh`: explicit-address candidate flash with mandatory verified backup.
- Modify `scripts/report_firmware_metrics.py`: hashes, assets, and hard capacity gates.
- Create `scripts/run_unified_offline_validation.sh`: one-command offline validation.
- Modify `.gitignore`: ignore `build-unified/`, `sdkconfig.unified`, and `backups/`.

### Contracts and reports

- Modify `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`.
- Modify `mac-codex-agent/tests/test_report_firmware_metrics.py`.
- Create `mac-codex-agent/tests/test_unified_flash_tools.py`.
- Modify `docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md`.
- Modify `docs/superpowers/reports/brookesia-xiaozhi-compatibility.md`.
- Create `docs/superpowers/reports/unified-runtime-validation.md`.

---

### Task 1: Reconcile Shadow-Mode Contracts

**Files:**
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`
- Modify: `docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md`
- Modify: `docs/superpowers/reports/brookesia-xiaozhi-compatibility.md`

- [x] **Step 1: Add the failing execution-order contract**

Add this test without changing the documents first:

```python
def test_shadow_mode_allows_offline_runtime_work_without_passing_hardware(self):
    plan = read(
        "docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md"
    )
    report = read(
        "docs/superpowers/reports/brookesia-xiaozhi-compatibility.md"
    )
    self.assertIn("Tasks 8, 9, 11, and Task 10 preparation", plan)
    self.assertIn("hardware status remains pending", plan)
    self.assertIn("Task 8 may proceed in offline shadow mode", report)
    self.assertIn("single-partition promotion remains gated", report)
```

- [x] **Step 2: Run the focused contract and confirm RED**

Run:

```bash
PYTHONPATH=mac-codex-agent/tests python3 -m unittest \
  test_unified_runtime_firmware_contract.UnifiedRuntimeFirmwareContractTest.test_shadow_mode_allows_offline_runtime_work_without_passing_hardware
```

Expected: `FAIL` because the exact shadow-mode statements do not exist.

- [x] **Step 3: Make the documents unambiguous**

Replace the old execution-order restriction with these exact statements:

```markdown
- Tasks 8, 9, 11, and Task 10 preparation may proceed in offline shadow mode.
- The hardware status remains pending until the concentrated watch campaign passes.
- Task 10 preparation includes candidate files and dry-run tooling only; promotion and deletion remain gated.
```

Append this sentence to the compatibility report:

```markdown
Task 8 may proceed in offline shadow mode; single-partition promotion remains gated by this hardware matrix and the complete regression campaign.
```

- [x] **Step 4: Run GREEN and commit**

```bash
PYTHONPATH=mac-codex-agent/tests python3 -m unittest \
  test_unified_runtime_firmware_contract.UnifiedRuntimeFirmwareContractTest.test_shadow_mode_allows_offline_runtime_work_without_passing_hardware
git add mac-codex-agent/tests/test_unified_runtime_firmware_contract.py \
  docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md \
  docs/superpowers/reports/brookesia-xiaozhi-compatibility.md
git commit -m "Allow offline unified Assistant preparation"
```

Expected: one passing test and a clean staged diff before commit.

---

### Task 2: Implement the Pure Assistant State Machine

**Files:**
- Create: `main/runtime/assistant/assistant_state.h`
- Create: `main/runtime/assistant/assistant_state.cpp`
- Create: `tests/assistant_state_machine_test.cpp`

- [x] **Step 1: Write the failing state test**

```cpp
#include "runtime/assistant/assistant_state.h"
#include <cassert>

int main()
{
    AssistantStateMachine state;
    assert(state.state() == AssistantRuntimeState::Stopped);
    assert(state.requestStart() == TransitionResult::Accepted);
    const auto first = state.sessionId();
    assert(first == 1);
    assert(state.requestStart() == TransitionResult::Rejected);
    assert(state.markRunning(first) == TransitionResult::Accepted);
    assert(state.requestStop() == TransitionResult::Accepted);
    assert(state.requestStop() == TransitionResult::AlreadyApplied);
    assert(state.markStopTimedOut(first) == TransitionResult::Accepted);
    assert(state.resourcesBlocked());
    assert(state.markStopped(first - 1) == TransitionResult::Rejected);
    assert(state.markStopped(first) == TransitionResult::Accepted);
    assert(!state.resourcesBlocked());

    assert(state.requestStart() == TransitionResult::Accepted);
    const auto second = state.sessionId();
    assert(second == 2);
    assert(state.markFailed(second) == TransitionResult::Accepted);
    assert(state.requestStop() == TransitionResult::Accepted);
    assert(state.markStopped(second) == TransitionResult::Accepted);
    return 0;
}
```

- [x] **Step 2: Compile and confirm RED**

```bash
c++ -std=c++17 -Imain tests/assistant_state_machine_test.cpp \
  main/runtime/assistant/assistant_state.cpp \
  -o /tmp/assistant_state_machine_test
```

Expected: compilation fails because the files and types do not exist.

- [x] **Step 3: Add the state-machine interface**

```cpp
#pragma once
#include <cstdint>

enum class AssistantRuntimeState : uint8_t {
    Stopped,
    Starting,
    Running,
    Failed,
    Stopping,
    StopTimedOut,
};

enum class TransitionResult : uint8_t { Accepted, AlreadyApplied, Rejected };

class AssistantStateMachine {
public:
    TransitionResult requestStart();
    TransitionResult markRunning(uint64_t session_id);
    TransitionResult markFailed(uint64_t session_id);
    TransitionResult requestStop();
    TransitionResult markStopTimedOut(uint64_t session_id);
    TransitionResult markStopped(uint64_t session_id);
    AssistantRuntimeState state() const { return _state; }
    uint64_t sessionId() const { return _session_id; }
    bool resourcesBlocked() const { return _state != AssistantRuntimeState::Stopped; }

private:
    bool current(uint64_t session_id) const { return session_id == _session_id; }
    AssistantRuntimeState _state = AssistantRuntimeState::Stopped;
    uint64_t _session_id = 0;
};
```

Implement these exact transition rules in `assistant_state.cpp`:

```cpp
#include "assistant_state.h"

TransitionResult AssistantStateMachine::requestStart()
{
    if (_state != AssistantRuntimeState::Stopped) return TransitionResult::Rejected;
    ++_session_id;
    _state = AssistantRuntimeState::Starting;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markRunning(uint64_t id)
{
    if (!current(id) || _state != AssistantRuntimeState::Starting) return TransitionResult::Rejected;
    _state = AssistantRuntimeState::Running;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markFailed(uint64_t id)
{
    if (!current(id) || _state != AssistantRuntimeState::Starting) return TransitionResult::Rejected;
    _state = AssistantRuntimeState::Failed;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::requestStop()
{
    if (_state == AssistantRuntimeState::Stopping || _state == AssistantRuntimeState::StopTimedOut)
        return TransitionResult::AlreadyApplied;
    if (_state != AssistantRuntimeState::Starting && _state != AssistantRuntimeState::Running &&
        _state != AssistantRuntimeState::Failed)
        return TransitionResult::Rejected;
    _state = AssistantRuntimeState::Stopping;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markStopTimedOut(uint64_t id)
{
    if (!current(id) || _state != AssistantRuntimeState::Stopping) return TransitionResult::Rejected;
    _state = AssistantRuntimeState::StopTimedOut;
    return TransitionResult::Accepted;
}

TransitionResult AssistantStateMachine::markStopped(uint64_t id)
{
    if (!current(id) || (_state != AssistantRuntimeState::Stopping &&
                         _state != AssistantRuntimeState::StopTimedOut))
        return TransitionResult::Rejected;
    _state = AssistantRuntimeState::Stopped;
    return TransitionResult::Accepted;
}
```

- [x] **Step 4: Run GREEN and commit**

```bash
c++ -std=c++17 -Imain tests/assistant_state_machine_test.cpp \
  main/runtime/assistant/assistant_state.cpp \
  -o /tmp/assistant_state_machine_test
/tmp/assistant_state_machine_test
git add main/runtime/assistant/assistant_state.h \
  main/runtime/assistant/assistant_state.cpp tests/assistant_state_machine_test.cpp
git commit -m "Add Assistant lifecycle state machine"
```

Expected: executable exits with status 0.

---

### Task 3: Add a Bounded UI Event Mailbox

**Files:**
- Create: `main/runtime/assistant/assistant_event_mailbox.h`
- Create: `main/runtime/assistant/assistant_event_mailbox.cpp`
- Create: `tests/assistant_event_mailbox_test.cpp`

- [x] **Step 1: Write the failing mailbox test**

```cpp
#include "runtime/assistant/assistant_event_mailbox.h"
#include <cassert>

int main()
{
    AssistantEventMailbox mailbox(2);
    mailbox.publish({1, AssistantEventKind::State, "Starting"});
    mailbox.publish({1, AssistantEventKind::State, "Running"});
    mailbox.publish({1, AssistantEventKind::UserText, "one"});
    mailbox.publish({1, AssistantEventKind::AgentText, "two"});
    mailbox.publish({1, AssistantEventKind::Emotion, "three"});
    assert(mailbox.droppedTransientCount() == 1);

    AssistantEvent event;
    assert(mailbox.poll(event));
    assert(event.kind == AssistantEventKind::State);
    assert(event.text == "Running");
    assert(mailbox.poll(event));
    assert(event.text == "two");
    assert(mailbox.poll(event));
    assert(event.text == "three");
    assert(!mailbox.poll(event));

    mailbox.publish({1, AssistantEventKind::Error, "old"});
    mailbox.publish({2, AssistantEventKind::Error, "new"});
    mailbox.discardSessionsBefore(2);
    assert(mailbox.poll(event));
    assert(event.session_id == 2);
    return 0;
}
```

- [x] **Step 2: Compile and confirm RED**

```bash
c++ -std=c++17 -pthread -Imain tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test
```

Expected: compilation fails because the mailbox does not exist.

- [x] **Step 3: Implement persistent and transient storage**

Use this public interface:

```cpp
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

enum class AssistantEventKind : uint8_t {
    State,
    ActivationCode,
    Error,
    UserText,
    AgentText,
    Emotion,
};

struct AssistantEvent {
    uint64_t session_id = 0;
    AssistantEventKind kind = AssistantEventKind::State;
    std::string text;
};

class AssistantEventMailbox {
public:
    explicit AssistantEventMailbox(std::size_t transient_capacity = 32);
    void publish(AssistantEvent event);
    bool poll(AssistantEvent& event);
    void discardSessionsBefore(uint64_t session_id);
    std::size_t droppedTransientCount() const;

private:
    struct Envelope { uint64_t sequence; AssistantEvent event; };
    static int persistentIndex(AssistantEventKind kind);
    mutable std::mutex _mutex;
    std::array<std::optional<Envelope>, 3> _persistent;
    std::deque<Envelope> _transient;
    std::size_t _capacity;
    std::size_t _dropped = 0;
    uint64_t _sequence = 0;
};
```

`publish()` replaces the persistent slot for State, ActivationCode, or Error. For transient events, it removes the oldest transient item when `_capacity` is full and increments `_dropped`. `poll()` returns the lowest sequence across occupied persistent slots and the transient front. `discardSessionsBefore()` removes every envelope whose session is older than the supplied session.

- [x] **Step 4: Run GREEN and commit**

```bash
c++ -std=c++17 -pthread -Imain tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test
/tmp/assistant_event_mailbox_test
git add main/runtime/assistant/assistant_event_mailbox.h \
  main/runtime/assistant/assistant_event_mailbox.cpp tests/assistant_event_mailbox_test.cpp
git commit -m "Add Assistant UI event mailbox"
```

Expected: executable exits with status 0 and the persistent state is `Running`.

---

### Task 4: Replace the Probe Lifecycle with AssistantRuntime

**Files:**
- Create: `main/runtime/assistant/assistant_runtime.h`
- Create: `main/runtime/assistant/assistant_runtime.cpp`
- Modify: `main/runtime/assistant/assistant_probe.h`
- Modify: `main/runtime/assistant/assistant_probe.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `mac-codex-agent/tests/test_xiaozhi_probe_firmware_contract.py`

- [x] **Step 1: Extend the firmware contract and confirm RED**

Add these assertions:

```python
runtime = read("main/runtime/assistant/assistant_runtime.cpp")
probe = read("main/runtime/assistant/assistant_probe.cpp")
self.assertIn("AssistantStateMachine", runtime)
self.assertIn("AssistantEventMailbox", runtime)
self.assertIn("GetSharedWifiService().acquireSta(RuntimeOwner::Assistant)", runtime)
self.assertIn("acquireCapture(RuntimeOwner::Assistant)", runtime)
self.assertIn("acquirePlayback(RuntimeOwner::Assistant)", runtime)
self.assertIn("ManagerHelper::GeneralAction::Activate", runtime)
self.assertIn("ManagerHelper::GeneralAction::Start", runtime)
self.assertIn("ManagerHelper::GeneralAction::Stop", runtime)
self.assertIn("ManagerHelper::GeneralEvent::Stopped", runtime)
self.assertIn("GetAssistantRuntime()", probe)
self.assertNotIn("std::vector<std::shared_ptr", probe)
```

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_xiaozhi_probe_firmware_contract.py
```

Expected: `FAIL` because lifecycle ownership still lives in the probe.

- [x] **Step 2: Define the production interface**

```cpp
#pragma once
#include "assistant_event_mailbox.h"
#include "assistant_state.h"
#include <cstddef>
#include <cstdint>

enum class AssistantStartResult : uint8_t {
    Submitted,
    AlreadyActive,
    ServiceUnavailable,
    ResourceBusy,
    ConfigurationFailed,
    SubmitFailed,
};

class AssistantRuntime {
public:
    AssistantStartResult start();
    bool requestStop();
    void update(uint32_t now_ms);
    AssistantRuntimeState state() const;
    uint64_t sessionId() const;
    bool pollEvent(AssistantEvent& event);
    std::size_t bindingCount() const;
    std::size_t subscriptionCount() const;

private:
    bool bindServices(uint64_t session_id);
    bool configureAudioAndAgent();
    bool subscribe(uint64_t session_id);
    bool submitActivate(uint64_t session_id);
    bool submitStart(uint64_t session_id);
    bool submitStop(uint64_t session_id);
    AssistantStartResult failAndReturn(uint64_t session_id,
                                       AssistantStartResult result,
                                       const char* message);
    void failStart(uint64_t session_id, const char* message);
    void releaseSession(uint64_t session_id);
};

AssistantRuntime& GetAssistantRuntime();
```

- [x] **Step 3: Move the validated probe mechanics into the runtime**

Move wake-model loading, the three service bindings, AFE configuration, volume mapping, identity metadata, target selection, subscriptions, and async action submission from `assistant_probe.cpp` into `assistant_runtime.cpp`.

Use this acquisition sequence in `start()`:

```cpp
if (_state.requestStart() != TransitionResult::Accepted) return AssistantStartResult::AlreadyActive;
const uint64_t session = _state.sessionId();
_mailbox.discardSessionsBefore(session);
_wifi = GetSharedWifiService().acquireSta(RuntimeOwner::Assistant);
if (!_wifi.valid()) return failAndReturn(session, AssistantStartResult::ResourceBusy, "Wi-Fi busy");
if (GetResourceSupervisor().acquireCapture(RuntimeOwner::Assistant) != AcquireResult::Granted)
    return failAndReturn(session, AssistantStartResult::ResourceBusy, "Microphone busy");
_capture_owned = true;
if (GetResourceSupervisor().acquirePlayback(RuntimeOwner::Assistant) != AcquireResult::Granted)
    return failAndReturn(session, AssistantStartResult::ResourceBusy, "Speaker busy");
_playback_owned = true;
if (!bindServices(session) || !configureAudioAndAgent() || !subscribe(session))
    return failAndReturn(session, AssistantStartResult::ConfigurationFailed, "Service setup failed");
if (!submitActivate(session))
    return failAndReturn(session, AssistantStartResult::SubmitFailed, "Activate submission failed");
return AssistantStartResult::Submitted;
```

Brookesia callbacks capture `session`. They reject any value different from `_state.sessionId()` and reject manager events whose agent name differs from `XiaozhiHelper::get_name()`. Callback threads publish text/emotion directly to the thread-safe mailbox and set atomic control bits for Activated, Started, Stopped, and async failure. `update()` consumes the control bits: Activated submits Start, Started calls `markRunning`, Stopped calls `releaseSession`, and async failure calls `failStart`.

`failAndReturn()` publishes Error, calls `markFailed(session_id)`, calls `requestStop()` to enter Stopping, releases every resource already acquired, calls `markStopped(session_id)`, and returns the supplied `AssistantStartResult`. `failStart()` performs the same transition and release after an asynchronous Activate or Start failure.

`requestStop()` calls `AssistantStateMachine::requestStop()`, publishes `Stopping`, records `_stop_started_ms`, and submits Stop once. On submission failure it clears `_stop_submitted`, allowing a later retry. `update()` marks `StopTimedOut` after 2,000 ms, calls `GetResourceSupervisor().block()`, publishes the timeout, and permits another Stop submission. It never calls `vTaskDelete()`.

`releaseSession()` must execute this order:

```cpp
disconnectSubscriptions();
_bindings.clear();
if (_playback_owned) GetResourceSupervisor().releasePlayback(RuntimeOwner::Assistant);
if (_capture_owned) GetResourceSupervisor().releaseCapture(RuntimeOwner::Assistant);
_playback_owned = false;
_capture_owned = false;
_wifi.reset();
GetResourceSupervisor().unblock();
_state.markStopped(session_id);
_mailbox.publish({session_id, AssistantEventKind::State, "Stopped"});
```

Before the release sequence, call `_state.requestStop()` when a spontaneous Stopped event arrives while the local state is Starting, Running, or Failed. This guarantees that `markStopped()` uses a legal transition while still accepting an authoritative server-side stop.

- [x] **Step 4: Make the probe a thin wrapper**

Use this complete wrapper API:

```cpp
bool StartAssistantCompatibilityProbe()
{
    return GetAssistantRuntime().start() == AssistantStartResult::Submitted;
}

bool StopAssistantCompatibilityProbe()
{
    return GetAssistantRuntime().requestStop();
}

void UpdateAssistantCompatibilityProbe(uint32_t now_ms)
{
    GetAssistantRuntime().update(now_ms);
}

bool AssistantCompatibilityProbeStopped()
{
    return GetAssistantRuntime().state() == AssistantRuntimeState::Stopped;
}
```

- [x] **Step 5: Run contracts and the full firmware link**

```bash
python3 -m unittest mac-codex-agent/tests/test_xiaozhi_probe_firmware_contract.py
idf.py reconfigure
ninja -C build StopWatch-UserDemo.bin generated_unified_assets
```

Expected: contracts pass and the explicit image targets compile, link, and produce `build/StopWatch-UserDemo.bin` without running the legacy OTA partition-size target.

- [x] **Step 6: Commit**

```bash
git add main/runtime/assistant main/CMakeLists.txt \
  mac-codex-agent/tests/test_xiaozhi_probe_firmware_contract.py
git commit -m "Add production Assistant runtime lifecycle"
```

---

### Task 5: Add RuntimeSupervisor and Passive Diagnostics

**Files:**
- Create: `main/runtime/runtime_supervisor.h`
- Create: `main/runtime/runtime_supervisor.cpp`
- Create: `main/runtime/runtime_diagnostics.h`
- Create: `main/runtime/runtime_diagnostics.cpp`
- Create: `tests/runtime_diagnostics_test.cpp`
- Modify: `main/main.cpp`

- [x] **Step 1: Write the failing diagnostic policy test**

```cpp
#include "runtime/runtime_diagnostics.h"
#include <cassert>

int main()
{
    RuntimeDiagnosticSnapshot before{100000, 90000, 2000000, 20};
    RuntimeDiagnosticSnapshot after{97000, 87000, 1999000, 20};
    const auto delta = CompareRuntimeSnapshots(before, after);
    assert(delta.internal_heap_loss == 3000);
    assert(delta.psram_loss == 1000);
    assert(delta.task_delta == 0);
    assert(PassesSingleSessionLeakGate(delta));
    after.internal_free_heap = 95000;
    assert(!PassesSingleSessionLeakGate(CompareRuntimeSnapshots(before, after)));
    return 0;
}
```

- [x] **Step 2: Add stable types and GREEN implementation**

```cpp
struct RuntimeDiagnosticSnapshot {
    std::size_t internal_free_heap = 0;
    std::size_t internal_min_free_heap = 0;
    std::size_t psram_free = 0;
    std::size_t task_count = 0;
    ResourceSnapshot resources;
    AssistantRuntimeState assistant_state = AssistantRuntimeState::Stopped;
    std::size_t assistant_bindings = 0;
    std::size_t assistant_subscriptions = 0;
    uint32_t captured_at_ms = 0;
};

struct RuntimeDiagnosticDelta {
    std::ptrdiff_t internal_heap_loss;
    std::ptrdiff_t psram_loss;
    std::ptrdiff_t task_delta;
};
```

The host-testable comparison is exactly:

```cpp
RuntimeDiagnosticDelta CompareRuntimeSnapshots(const RuntimeDiagnosticSnapshot& before,
                                                const RuntimeDiagnosticSnapshot& after)
{
    return {
        static_cast<std::ptrdiff_t>(before.internal_free_heap) -
            static_cast<std::ptrdiff_t>(after.internal_free_heap),
        static_cast<std::ptrdiff_t>(before.psram_free) -
            static_cast<std::ptrdiff_t>(after.psram_free),
        static_cast<std::ptrdiff_t>(after.task_count) -
            static_cast<std::ptrdiff_t>(before.task_count),
    };
}

bool PassesSingleSessionLeakGate(const RuntimeDiagnosticDelta& delta)
{
    return delta.internal_heap_loss <= 4096 && delta.task_delta == 0;
}
```

`CaptureRuntimeDiagnosticSnapshot()` uses `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`, `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`, `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`, `uxTaskGetNumberOfTasks()`, `GetResourceSupervisor().snapshot()`, and `GetAssistantRuntime()` counters. It is called only before start, after Running, after Stopped, or by an explicit diagnostic command.

- [x] **Step 3: Add the application facade**

```cpp
class RuntimeSupervisor {
public:
    bool start();
    AssistantStartResult startAssistant();
    bool requestStopAssistant();
    void update(uint32_t now_ms);
    AssistantRuntimeState assistantState() const;
    bool pollAssistantEvent(AssistantEvent& event);
    RuntimeDiagnosticSnapshot diagnostics() const;
};

RuntimeSupervisor& GetRuntimeSupervisor();
```

`start()` requires `GetServiceRuntime().isStarted()`. `startAssistant()` captures the before snapshot, `update()` delegates to AssistantRuntime and captures the first Running and Stopped snapshots for each session, and all snapshots are logged once.

- [x] **Step 4: Initialize the facade and run tests**

After `GetServiceRuntime().start()` succeeds in `main/main.cpp`, call:

```cpp
if (!GetRuntimeSupervisor().start()) {
    mclog::tagError("Main", "runtime supervisor unavailable; application startup aborted");
    return;
}
```

Run:

```bash
c++ -std=c++17 -Imain tests/runtime_diagnostics_test.cpp \
  main/runtime/runtime_diagnostics.cpp -DSTOPWATCH_HOST_TEST \
  -o /tmp/runtime_diagnostics_test
/tmp/runtime_diagnostics_test
idf.py reconfigure
ninja -C build gen_project_binary generated_unified_assets
```

The `STOPWATCH_HOST_TEST` branch excludes ESP heap/task collection but keeps comparison functions identical.

- [x] **Step 5: Commit**

```bash
git add main/runtime/runtime_supervisor.h main/runtime/runtime_supervisor.cpp \
  main/runtime/runtime_diagnostics.h main/runtime/runtime_diagnostics.cpp \
  main/main.cpp tests/runtime_diagnostics_test.cpp
git commit -m "Add unified runtime diagnostics facade"
```

---

### Task 6: Run Assistant Inside Mooncake

**Files:**
- Create: `main/apps/app_assistant/view/assistant_view.h`
- Create: `main/apps/app_assistant/view/assistant_view.cpp`
- Modify: `main/apps/app_assistant/app_assistant.h`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`

- [x] **Step 1: Add the failing Mooncake source contract**

```python
def test_assistant_uses_runtime_supervisor_and_never_switches_boot_slot(self):
    source = read("main/apps/app_assistant/app_assistant.cpp")
    self.assertIn("GetRuntimeSupervisor().startAssistant()", source)
    self.assertIn("GetRuntimeSupervisor().requestStopAssistant()", source)
    self.assertIn("runtime.update(GetHAL().millis())", source)
    for forbidden in (
        "esp_ota_ops.h",
        "esp_partition.h",
        "esp_system.h",
        "esp_ota_set_boot_partition",
        "esp_restart",
        "ota_1",
    ):
        self.assertNotIn(forbidden, source)
```

Run the test and expect `FAIL` against the current partition-switching application.

- [x] **Step 2: Add the presentation-only view**

```cpp
enum class AssistantViewState : uint8_t {
    Connecting,
    Activating,
    Listening,
    Thinking,
    Speaking,
    Failed,
    Closing,
    StopTimedOut,
};

class AssistantView {
public:
    void init(lv_obj_t* parent);
    void setState(AssistantViewState state);
    void setPrimaryText(const char* text);
    void setSecondaryText(const char* text);
    void destroy();
};
```

Use the existing black Assistant panel, Montserrat title, primary status label, secondary content label, and a bottom hint. The view contains no Brookesia, runtime, partition, or restart include.

- [x] **Step 3: Replace partition switching with asynchronous lifecycle**

`onOpen()` creates the view, clears `_close_requested` and `_retry_available`, and calls `startAssistant()` once. `onRunning()` executes this order:

```cpp
auto& runtime = GetRuntimeSupervisor();
runtime.update(GetHAL().millis());
drainRuntimeEvents();
const auto key = _key_manager->update();

if (key == input::KeyEvent::GoHome && !_close_requested) {
    _close_requested = true;
    _view->setState(AssistantViewState::Closing);
    runtime.requestStopAssistant();
}
if (key == input::KeyEvent::GoNext && _retry_available && !_close_requested) {
    _retry_available = false;
    runtime.startAssistant();
}
if (key == input::KeyEvent::GoNext &&
    runtime.assistantState() == AssistantRuntimeState::StopTimedOut) {
    runtime.requestStopAssistant();
}
if (_close_requested && runtime.assistantState() == AssistantRuntimeState::Stopped) {
    close();
}
```

`drainRuntimeEvents()` maps State, ActivationCode, Error, UserText, AgentText, and Emotion onto the view. It never holds `LvglLockGuard` while calling the runtime. `onClose()` destroys only UI and input objects; the app calls `close()` only after runtime state is Stopped.

- [x] **Step 4: Run contracts, host tests, and link**

```bash
PYTHONPATH=mac-codex-agent/tests python3 -m unittest \
  test_unified_runtime_firmware_contract.UnifiedRuntimeFirmwareContractTest.test_assistant_uses_runtime_supervisor_and_never_switches_boot_slot
/tmp/assistant_state_machine_test
/tmp/assistant_event_mailbox_test
idf.py reconfigure
ninja -C build StopWatch-UserDemo.bin generated_unified_assets
```

Expected: the focused Assistant source contract passes and the explicit image targets link successfully without evaluating the legacy OTA slot size.

- [x] **Step 5: Commit**

```bash
git add main/apps/app_assistant \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
git commit -m "Run Assistant inside Mooncake"
```

---

### Task 7: Add the Isolated Unified Candidate Build

**Files:**
- Create: `partitions_unified.csv`
- Create: `sdkconfig.unified.defaults`
- Create: `scripts/build_unified_candidate.sh`
- Modify: `.gitignore`
- Modify: `scripts/report_firmware_metrics.py`
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_report_firmware_metrics.py`

- [ ] **Step 1: Add failing partition and metrics tests**

The partition contract must parse `partitions_unified.csv` and assert this exact table while also asserting that `partitions.csv` still contains `ota_0` and `ota_1`:

```csv
# Name,   Type, SubType, Offset,    Size,      Flags
nvs,      data, nvs,     0x9000,    0x4000,
phy_init, data, phy,     0xf000,    0x1000,
app,      app,  factory, 0x20000,   0x780000,
storage,  data, fat,     0x7a0000,  0x100000,
assets,   data, spiffs,  0x8a0000,  0x400000,
coredump, data, coredump,0xca0000,  0x10000,
```

Replace the three old promotion-only contracts with shadow-mode contracts:

```python
def test_unified_candidate_has_one_factory_application(self):
    rows = parse_partition_csv("partitions_unified.csv")
    app_rows = [row for row in rows if len(row) >= 3 and row[1] == "app"]
    self.assertEqual([(row[0], row[2], row[3], row[4]) for row in app_rows],
                     [("app", "factory", "0x20000", "0x780000")])

def test_legacy_layout_and_recovery_tools_remain_during_shadow_mode(self):
    legacy = read("partitions.csv")
    self.assertIn("ota_0", legacy)
    self.assertIn("ota_1", legacy)
    self.assertTrue((ROOT / "scripts" / "flash_assistant_runtime.sh").is_file())
    self.assertTrue((ROOT / "scripts" / "flash_full_assistant_stack.sh").is_file())

def test_new_product_path_has_no_ota_slot_dependency(self):
    paths = [
        ROOT / "main" / "apps" / "app_assistant" / "app_assistant.cpp",
        ROOT / "scripts" / "build_unified_candidate.sh",
    ]
    offenders = [path.relative_to(ROOT).as_posix() for path in paths
                 if re.search(r"\bota_[01]\b", path.read_text(encoding="utf-8"))]
    self.assertEqual(offenders, [])
```

Delete the old assertions that require `partitions.csv` to be single-factory, require legacy scripts to be absent, or scan all legacy scripts for `ota_0`/`ota_1`.

Add a metrics test with a 100-byte app and 40-byte assets that asserts:

```python
self.assertEqual(metrics["schema_version"], 2)
self.assertEqual(metrics["app_sha256"], hashlib.sha256(b"a" * 100).hexdigest())
self.assertEqual(metrics["assets_bytes"], 40)
self.assertEqual(metrics["assets_sha256"], hashlib.sha256(b"c" * 40).hexdigest())
self.assertTrue(metrics["capacity_gate_passed"])
```

Run both files and expect failures because the candidate layout and schema do not exist.

- [ ] **Step 2: Add candidate configuration**

Create the exact partition CSV above. Create `sdkconfig.unified.defaults`:

```text
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_unified.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions_unified.csv"
```

Add these ignores:

```gitignore
/build-unified/
/sdkconfig.unified
/backups/
```

- [ ] **Step 3: Extend metrics with hard gates**

Add `sha256_file(path)`, `--assets-bin`, `--max-app-bytes`, and `--max-assets-bytes`. Schema 2 includes app/assets SHA256, sizes, both maxima, and `capacity_gate_passed`. Raise `MetricsError` if either maximum is exceeded; do not merely report a warning.

- [ ] **Step 4: Add the isolated candidate build script**

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-unified"
SDKCONFIG="$ROOT/sdkconfig.unified"
DEFAULTS="$ROOT/sdkconfig.defaults;$ROOT/sdkconfig.unified.defaults"
IDF_PY="${IDF_PY:-idf.py}"

if [[ "${1:-}" == "--clean" && -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    "$IDF_PY" -B "$BUILD_DIR" fullclean
    rm -f "$SDKCONFIG"
fi

"$IDF_PY" -B "$BUILD_DIR" \
    -D "SDKCONFIG=$SDKCONFIG" \
    -D "SDKCONFIG_DEFAULTS=$DEFAULTS" reconfigure
"$IDF_PY" -B "$BUILD_DIR" build
python3 "$ROOT/scripts/report_firmware_metrics.py" \
    --repo-root "$ROOT" \
    --build-dir "$BUILD_DIR" \
    --partition-table "$ROOT/partitions_unified.csv" \
    --assets-bin "$BUILD_DIR/generated_assets.bin" \
    --max-app-bytes 0x660000 \
    --max-assets-bytes 0x400000 \
    --source unified-shadow-candidate \
    --output "$BUILD_DIR/unified-runtime-metrics.json"
```

- [ ] **Step 5: Run GREEN and build the candidate**

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py \
  mac-codex-agent/tests/test_report_firmware_metrics.py
chmod +x scripts/build_unified_candidate.sh
./scripts/build_unified_candidate.sh --clean
```

Expected: tests pass; the build completes its partition size check; metrics report `capacity_gate_passed: true`.

- [ ] **Step 6: Commit**

```bash
git add .gitignore partitions_unified.csv sdkconfig.unified.defaults \
  scripts/build_unified_candidate.sh scripts/report_firmware_metrics.py \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py \
  mac-codex-agent/tests/test_report_firmware_metrics.py
git commit -m "Add isolated unified firmware candidate build"
```

---

### Task 8: Add Dry-Run-Tested Backup, Restore, and Candidate Flash Tools

**Files:**
- Create: `scripts/backup_watch_flash.sh`
- Create: `scripts/restore_watch_flash.sh`
- Create: `scripts/flash_unified_candidate.sh`
- Create: `mac-codex-agent/tests/test_unified_flash_tools.py`

- [ ] **Step 1: Write dry-run and preflight tests**

The Python test creates temporary app/assets/bootloader/partition files plus a 16 MiB backup and `.sha256` sidecar. Assert:

```python
backup = run("backup_watch_flash.sh", "--dry-run", "--port", "/dev/test")
self.assertIn("read_flash 0x0 0x1000000", backup.stdout)

restore = run("restore_watch_flash.sh", "--dry-run", "--port", "/dev/test",
              "--backup", str(self.backup))
self.assertEqual(restore.returncode, 0)
self.assertIn("write_flash 0x0", restore.stdout)

flash = run("flash_unified_candidate.sh", "--dry-run", "--port", "/dev/test",
            "--backup", str(self.backup), "--build-dir", str(self.build))
self.assertEqual(flash.returncode, 0)
self.assertIn("0x020000", flash.stdout)
self.assertIn("0x8a0000", flash.stdout)
```

Also assert nonzero exit for a backup of 16 MiB minus one byte, a mismatched SHA256 sidecar, an app larger than `0x660000`, or assets larger than `0x400000`.

- [ ] **Step 2: Implement backup with no erase operation**

The script accepts `--port`, optional `--output`, and `--dry-run`. Its real command is:

```bash
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  read_flash 0x0 0x1000000 "$OUTPUT"
```

After the read, require exactly 16,777,216 bytes and write `shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"`.

- [ ] **Step 3: Implement verified restore**

Before printing or executing esptool, require the backup file, sidecar, exact 16 MiB size, and matching SHA256. The only write command is:

```bash
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 "$BACKUP"
```

- [ ] **Step 4: Implement candidate flash with mandatory backup evidence**

Require a verified complete backup even in real mode. Validate these files and limits before esptool:

```text
build-unified/bootloader/bootloader.bin
build-unified/partition_table/partition-table.bin
build-unified/StopWatch-UserDemo.bin <= 0x660000
build-unified/generated_assets.bin <= 0x400000
```

The only candidate write map is:

```text
0x000000  bootloader.bin
0x008000  partition-table.bin
0x020000  StopWatch-UserDemo.bin
0x8a0000  generated_assets.bin
```

`--dry-run` performs every validation and prints the exact command without invoking esptool.

- [ ] **Step 5: Run GREEN and commit without connecting hardware**

```bash
chmod +x scripts/backup_watch_flash.sh scripts/restore_watch_flash.sh \
  scripts/flash_unified_candidate.sh
python3 -m unittest mac-codex-agent/tests/test_unified_flash_tools.py
git add scripts/backup_watch_flash.sh scripts/restore_watch_flash.sh \
  scripts/flash_unified_candidate.sh \
  mac-codex-agent/tests/test_unified_flash_tools.py
git commit -m "Add reversible unified firmware flash tools"
```

Expected: all tests use `--dry-run`; no serial device is opened.

---

### Task 9: Add the Offline Validation Runner and Hardware Report Skeleton

**Files:**
- Create: `scripts/run_unified_offline_validation.sh`
- Create: `docs/superpowers/reports/unified-runtime-validation.md`
- Modify: `docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md`

- [ ] **Step 1: Add one deterministic validation command**

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
c++ -std=c++17 -Imain tests/assistant_state_machine_test.cpp \
  main/runtime/assistant/assistant_state.cpp -o /tmp/assistant_state_machine_test
/tmp/assistant_state_machine_test
c++ -std=c++17 -pthread -Imain tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp -o /tmp/assistant_event_mailbox_test
/tmp/assistant_event_mailbox_test
c++ -std=c++17 -Imain tests/resource_supervisor_test.cpp \
  main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test
/tmp/resource_supervisor_test
c++ -std=c++17 -pthread -Imain tests/shared_wifi_policy_test.cpp \
  main/runtime/resource_supervisor.cpp \
  main/runtime/services/shared_wifi_policy.cpp \
  main/runtime/services/shared_wifi_service.cpp -o /tmp/shared_wifi_policy_test
/tmp/shared_wifi_policy_test
./scripts/build_unified_candidate.sh --clean
git diff --check
```

- [ ] **Step 2: Create a report that separates evidence classes**

The report starts with these statuses:

```markdown
# Unified Runtime Validation

- Automated tests: Passed only when the offline runner succeeds
- Unified clean build: Passed only when candidate build and capacity gates succeed
- Hardware compatibility: Pending hardware
- Lifecycle coexistence: Pending hardware
- 100-cycle stress: Pending hardware
- Full product regression: Pending hardware
```

Include tables for firmware Git SHA, image SHA256, app/assets bytes, partition headroom, resolved Brookesia versions, hardware matrix, 10-cycle targeted test, 100-cycle stress, and application regression. Hardware cells remain `Pending hardware` and contain the required evidence text from the approved design.

- [ ] **Step 3: Update plan tracking semantics**

For Tasks 8–12 in the original plan, split status notes into:

```markdown
- Code status: Not started / Implemented
- Automated status: Not run / Passed / Failed
- Hardware status: Pending hardware / Passed / Failed
```

Do not check hardware steps based on offline output.

- [ ] **Step 4: Run the offline command and commit**

```bash
chmod +x scripts/run_unified_offline_validation.sh
./scripts/run_unified_offline_validation.sh
git add scripts/run_unified_offline_validation.sh \
  docs/superpowers/reports/unified-runtime-validation.md \
  docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md
git commit -m "Add unified Assistant offline validation gate"
```

Expected: all automated tests pass, unified clean build passes, report retains every hardware row as `Pending hardware`.

---

### Task 10: Produce the Pre-Hardware Candidate Handoff

**Files:**
- Modify: `docs/superpowers/reports/unified-runtime-validation.md`
- Modify: `docs/superpowers/reports/brookesia-xiaozhi-compatibility.md`

- [ ] **Step 1: Run final offline verification from a clean candidate build**

```bash
./scripts/run_unified_offline_validation.sh
python3 - <<'PY'
import json
from pathlib import Path
m = json.loads(Path('build-unified/unified-runtime-metrics.json').read_text())
assert m['capacity_gate_passed'] is True
assert m['app_bytes'] <= 0x660000
assert m['assets_bytes'] <= 0x400000
print(json.dumps(m, indent=2, sort_keys=True))
PY
```

- [ ] **Step 2: Record exact generated evidence**

Copy the exact Git SHA, app/assets sizes, hashes, partition headroom, DIRAM bytes, and locked Brookesia versions from generated artifacts into the automated-evidence section. Keep all hardware tables unchanged at `Pending hardware`.

- [ ] **Step 3: Verify the repository boundary**

```bash
git diff --check
git status --short
rg -n "esp_ota_set_boot_partition|esp_restart|\bota_1\b" main/apps/app_assistant
rg -n "erase_flash" scripts/backup_watch_flash.sh \
  scripts/restore_watch_flash.sh scripts/flash_unified_candidate.sh
```

Expected: no Assistant product-path partition switch or restart; no flash helper invokes `erase_flash`; only the two report files may remain modified before the handoff commit.

- [ ] **Step 4: Commit the build-verified handoff**

```bash
git add docs/superpowers/reports/unified-runtime-validation.md \
  docs/superpowers/reports/brookesia-xiaozhi-compatibility.md
git commit -m "Prepare unified Assistant hardware candidate"
git status --short
```

Expected final status: clean. The handoff message must say that no flash occurred and list Stage 2 backup as the first future hardware action.

---

## Deferred Hardware Campaign

After the watch is available, execute the six stages in `docs/superpowers/specs/2026-07-13-unified-assistant-shadow-migration-design.md` without combining stages:

1. rerun offline validation;
2. create and verify a complete Flash backup, then full-flash the candidate;
3. pass the Xiaozhi compatibility matrix;
4. pass targeted lifecycle and coexistence tests;
5. pass the 100-cycle leak gate;
6. pass complete product regression, promote unified defaults, and only then remove legacy dual-OTA paths.

Any failure stops the campaign. Record firmware SHA, serial log, reset reason, runtime snapshot, reproduction steps, and expected/actual behavior before changing code.
