# UI-Independent Voice Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make watch voice capture and encrypted BLE delivery continue independently when the Codex UI/main loop stalls.

**Architecture:** Keep the existing capture producer, add a dedicated transmitter owned by `VoiceInputController`, and inject a narrow transport interface implemented by `CodexBleTransport`. The UI publishes link state and button events but never drains voice packets. `AgentConnectionRouter` supplies one synchronized active-voice snapshot so the transmitter cannot combine handle, MTU, subscription, or generation values from different connection epochs.

**Tech Stack:** C++17, ESP-IDF/FreeRTOS, NimBLE GATT notifications, host C++ tests, Python firmware contract tests, ESP32-S3 hardware validation.

---

## File Structure

- `main/apps/app_codex_status/voice_input_controller.h`: transport interface, TX worker lifecycle, diagnostics, and host-test seams.
- `main/apps/app_codex_status/voice_input_controller.cpp`: independent TX loop, elapsed-time congestion policy, terminal finalization, and bounded worker shutdown.
- `main/apps/app_codex_status/codex_ble_transport.h`: implement the production voice transport interface.
- `main/apps/app_codex_status/codex_ble_transport.cpp`: map one synchronized active voice-link snapshot to NimBLE notification results.
- `main/apps/app_codex_status/agent_connection_router.h`: define the immutable active voice-link snapshot.
- `main/apps/app_codex_status/agent_connection_router.cpp`: return the snapshot under the router's existing mutex.
- `main/apps/app_codex_status/app_codex_status.cpp`: wire controller to transport and remove UI-driven packet draining.
- `tests/voice_input_controller_test.cpp`: deterministic 60-second UI-stall, ordering, retry, cleanup, and diagnostics tests.
- `tests/agent_connection_router_test.cpp`: coherent voice-link snapshot tests across promotion, subscription, MTU update, and disconnect.
- `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`: assert production UI code cannot consume the voice queue.
- `docs/superpowers/reports/multi-agent-secure-pairing-validation.md`: record non-secret automated and physical acceptance results.

### Task 1: Lock Down the UI Independence Contract

**Files:**
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`

- [ ] **Step 1: Write the failing firmware contract test**

Add this test beside `test_production_controller_has_bounded_task_and_encrypted_queue`:

```python
def test_production_voice_delivery_is_not_driven_by_codex_ui_loop(self):
    app = (ROOT / "main/apps/app_codex_status/app_codex_status.cpp").read_text()
    controller = (
        ROOT / "main/apps/app_codex_status/voice_input_controller.cpp"
    ).read_text()
    running = app.split("void AppCodexStatus::onRunning()", 1)[1].split(
        "void AppCodexStatus::onClose()", 1
    )[0]

    self.assertNotIn("take_notification", running)
    self.assertNotIn("record_notification_result", running)
    self.assertNotIn("notify_voice(", running)
    self.assertIn("transmitter_loop", controller)
    self.assertIn("xTaskCreate", controller)
```

- [ ] **Step 2: Run the contract test and verify RED**

Run:

```bash
cd mac-codex-agent
.venv/bin/python -m unittest \
  tests.test_ble_voice_probe_firmware_contract.BleVoiceProbeFirmwareContractTests.test_production_voice_delivery_is_not_driven_by_codex_ui_loop
```

Expected: FAIL because `AppCodexStatus::onRunning()` still calls
`take_notification`, `notify_voice`, and `record_notification_result`.

- [ ] **Step 3: Keep the red test uncommitted until production is green**

Do not commit a knowingly broken tree. The test is committed with the Task 4
implementation after the focused contract and host suite pass.

### Task 2: Add a Coherent Active Voice-Link Snapshot

**Files:**
- Modify: `main/apps/app_codex_status/agent_connection_router.h`
- Modify: `main/apps/app_codex_status/agent_connection_router.cpp`
- Modify: `tests/agent_connection_router_test.cpp`

- [ ] **Step 1: Write the failing router snapshot test**

Add a test that uses the real router:

```cpp
void test_active_voice_link_is_one_coherent_snapshot()
{
    AgentConnectionRouter router;
    AgentFingerprint fingerprint = {};
    fingerprint[0] = 0x44;

    assert(router.connected(31, 100) == AgentConnectionAdmission::Pending);
    assert(router.authorize_pending(31, fingerprint));
    assert(router.promote_pending(31));
    assert(router.update_mtu(31, 256));
    assert(router.update_voice_subscription(31, true));

    const AgentVoiceLink active = router.active_voice_link();
    assert(active.handle == 31);
    assert(active.generation != 0);
    assert(active.mtu == 256);
    assert(active.subscribed);
    assert(active.authorized);

    assert(router.disconnected(31) == AgentConnectionRole::Active);
    const AgentVoiceLink disconnected = router.active_voice_link();
    assert(disconnected.handle == kNoAgentConnection);
    assert(disconnected.generation != active.generation);
    assert(!disconnected.subscribed);
    assert(!disconnected.authorized);
}
```

Call it from the test executable's `main()`.

- [ ] **Step 2: Run host validation and verify RED**

```bash
IDF_PATH=/Users/luke/esp/esp-idf \
  ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: compilation fails at the router test because `AgentVoiceLink` and
`active_voice_link()` do not exist.

- [ ] **Step 3: Implement the immutable snapshot**

Add to the router header:

```cpp
struct AgentVoiceLink {
    uint16_t handle = kNoAgentConnection;
    uint32_t generation = 0;
    uint16_t mtu = kDefaultAgentMtu;
    bool subscribed = false;
    bool authorized = false;
};

AgentVoiceLink active_voice_link() const;
```

Implement it under the existing router mutex:

```cpp
AgentVoiceLink AgentConnectionRouter::active_voice_link() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const AgentConnection* connection =
        role_locked(AgentConnectionRole::Active);
    if (connection == nullptr) {
        return AgentVoiceLink{
            kNoAgentConnection,
            _active_generation,
            kDefaultAgentMtu,
            false,
            false};
    }
    return AgentVoiceLink{
        connection->handle,
        _active_generation,
        connection->mtu,
        connection->voice_subscribed,
        connection->identity_authorized};
}
```

- [ ] **Step 4: Run host validation and verify GREEN**

Run the host validation command from Step 2. Expected: `53/53` and
`Unified host-test validation passed.`

- [ ] **Step 5: Commit the router snapshot**

```bash
git add main/apps/app_codex_status/agent_connection_router.h \
  main/apps/app_codex_status/agent_connection_router.cpp \
  tests/agent_connection_router_test.cpp
git commit -m "feat: snapshot active voice link atomically"
```

### Task 3: Specify the Independent Transmitter with a 60-Second Test

**Files:**
- Modify: `tests/voice_input_controller_test.cpp`
- Modify: `main/apps/app_codex_status/voice_input_controller.h`

- [ ] **Step 1: Add the fake transport and long-running failing test**

Add this fake after `FakeEntropy`:

```cpp
class FakeNotificationTransport final : public VoiceNotificationTransport {
public:
    VoiceNotificationSendResult send_voice_notification(
        const voice_input::Notification& notification) override
    {
        sent.push_back(notification);
        if (results.empty()) {
            return VoiceNotificationSendResult::Accepted;
        }
        const VoiceNotificationSendResult result = results.front();
        results.erase(results.begin());
        return result;
    }

    std::vector<voice_input::Notification> sent;
    std::vector<VoiceNotificationSendResult> results;
};
```

Add the regression:

```cpp
void test_sixty_seconds_of_voice_does_not_depend_on_ui_polling()
{
    FakeBackend backend;
    FakeEntropy entropy;
    FakeNotificationTransport transport;
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_notification_transport(&transport);
    controller.set_ready(true, 256);
    controller.handle_button(true, 1000);

    for (uint32_t block = 0; block < 3000; ++block) {
        const uint32_t now = 1001 + block * 20;
        controller.run_capture_worker_once_for_test(now);
        controller.run_transmit_worker_until_idle_for_test(now);
        assert(controller.ui_state() == VoiceInputUiState::Active);
    }

    controller.handle_button(false, 61001);
    controller.run_capture_worker_once_for_test(61002);
    controller.run_transmit_worker_until_idle_for_test(61002);

    assert(controller.ui_state() == VoiceInputUiState::Ready);
    assert(transport.sent.front().kind ==
           voice_input::NotificationKind::PttBegin);
    assert(transport.sent.back().kind ==
           voice_input::NotificationKind::PttEnd);
    assert(controller.queued_audio_ms_for_test() == 0);
}
```

The loop intentionally never calls an application/UI update method.

- [ ] **Step 2: Add only the interface declaration and host-test method declarations**

Add to `voice_input_controller.h`:

```cpp
class VoiceNotificationTransport {
public:
    virtual ~VoiceNotificationTransport() = default;
    virtual VoiceNotificationSendResult send_voice_notification(
        const voice_input::Notification& notification) = 0;
};
```

Add `set_notification_transport()` publicly and, under
`STOPWATCH_HOST_TEST`, add the two worker-driving methods used by the test.
Use these exact declarations; implementation remains absent for the RED run:

```cpp
void set_notification_transport(VoiceNotificationTransport* transport);

#if defined(STOPWATCH_HOST_TEST)
void run_capture_worker_once_for_test(uint32_t now_ms);
void run_transmit_worker_until_idle_for_test(uint32_t now_ms);
#endif
```

- [ ] **Step 3: Run host validation and verify RED**

```bash
IDF_PATH=/Users/luke/esp/esp-idf \
  ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: link failure for the three newly declared, unimplemented controller
methods. This proves the test reaches the missing independent-worker behavior.

- [ ] **Step 4: Keep the red controller test uncommitted until Task 4 is green**

Do not commit the missing-method link failure. Commit the test, interface, and
working implementation together after the focused and complete host checks.

### Task 4: Implement the TX Worker and Remove UI Queue Draining

**Files:**
- Modify: `main/apps/app_codex_status/voice_input_controller.h`
- Modify: `main/apps/app_codex_status/voice_input_controller.cpp`
- Modify: `main/apps/app_codex_status/codex_ble_transport.h`
- Modify: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] **Step 1: Add TX worker state to the controller**

Add private methods and state:

```cpp
static void transmitter_entry(void* context);
void transmitter_loop();
bool transmitter_step(uint32_t now_ms);
void wake_transmitter();
void stop_transmitter_bounded();

VoiceNotificationTransport* _notification_transport = nullptr;
std::atomic<bool> _transmitter_shutdown_requested{false};
void* _transmitter_task = nullptr;
void* _transmitter_stopped = nullptr;
uint32_t _congestion_started_ms = 0;
```

Use a 4096-byte TX stack, a 250 ms congestion deadline, a 2 ms retry delay,
and the existing bounded stop/grace pattern. Initialize both worker semaphores
before creating either task; on partial creation failure, stop and delete all
objects already created.

- [ ] **Step 2: Implement transport injection and worker wakeups**

`set_notification_transport()` accepts a non-owning transport before
`initialize()`. Every successful enqueue, producer completion, button release,
failure, disconnect, and shutdown wakes the transmitter. No mutex may be held
when the interface method enters NimBLE.

The core TX step is:

```cpp
bool VoiceInputController::transmitter_step(uint32_t now_ms)
{
    finalize_graceful_stop(now_ms);
    voice_input::Notification notification = {};
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_outgoing.empty()) {
            return false;
        }
        notification = _outgoing.front();
    }
    const VoiceNotificationSendResult result =
        _notification_transport == nullptr
            ? VoiceNotificationSendResult::PermanentFailure
            : _notification_transport->send_voice_notification(notification);
    record_notification_result(notification, result, now_ms);
    return result == VoiceNotificationSendResult::Accepted;
}
```

The device loop repeatedly calls this step while progress is made, delays 2 ms
after congestion, and otherwise blocks on task notification. The host helper
uses the same step until the queue is empty or congested.

- [ ] **Step 3: Implement the transport adapter with one router snapshot**

Make `CodexBleTransport` derive from `VoiceNotificationTransport` and add:

```cpp
VoiceNotificationSendResult CodexBleTransport::send_voice_notification(
    const voice_input::Notification& notification)
{
    const secure_pairing::AgentVoiceLink link =
        _connection_router.active_voice_link();
    if (!link.authorized || !link.subscribed ||
        notification.size == 0 || link.mtu <= 3 ||
        notification.size > static_cast<size_t>(link.mtu - 3)) {
        return VoiceNotificationSendResult::PermanentFailure;
    }
    const CodexBleNotifyResult result = notify_voice_for_link(
        link, notification.bytes.data(), notification.size);
    return result == CodexBleNotifyResult::Accepted
               ? VoiceNotificationSendResult::Accepted
           : result == CodexBleNotifyResult::Congested
               ? VoiceNotificationSendResult::Congested
               : VoiceNotificationSendResult::PermanentFailure;
}
```

`notify_voice_for_link()` uses the snapshot handle directly and retains the
existing NimBLE mbuf ownership rules. A concurrent disconnect is reported by
NimBLE and becomes `PermanentFailure`; it cannot redirect the packet to a new
generation.

- [ ] **Step 4: Wire ownership in the Codex page**

After creating both objects and before controller initialization:

```cpp
_voice_input = std::make_unique<VoiceInputController>();
_voice_input->set_notification_transport(_ble_transport.get());
if (!_voice_input->initialize()) {
    _voice_input.reset();
}
```

Delete the entire UI loop that calls `poll`, `take_notification`,
`notify_voice`, and `record_notification_result`. Keep READY/MTU/epoch updates
and UI-state rendering.

- [ ] **Step 5: Run the focused contract test and host suite**

```bash
cd mac-codex-agent
.venv/bin/python -m unittest \
  tests.test_ble_voice_probe_firmware_contract.BleVoiceProbeFirmwareContractTests.test_production_voice_delivery_is_not_driven_by_codex_ui_loop
cd ..
IDF_PATH=/Users/luke/esp/esp-idf \
  ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: focused test passes and host validation reports `53/53`.

- [ ] **Step 6: Commit the independent transport**

```bash
git add main/apps/app_codex_status/voice_input_controller.h \
  main/apps/app_codex_status/voice_input_controller.cpp \
  main/apps/app_codex_status/codex_ble_transport.h \
  main/apps/app_codex_status/codex_ble_transport.cpp \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/voice_input_controller_test.cpp \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py
git commit -m "fix: decouple voice transport from UI updates"
```

### Task 5: Make Congestion and Cleanup Deterministic

**Files:**
- Modify: `tests/voice_input_controller_test.cpp`
- Modify: `main/apps/app_codex_status/voice_input_controller.h`
- Modify: `main/apps/app_codex_status/voice_input_controller.cpp`

- [ ] **Step 1: Write failing elapsed-time congestion tests**

Add a host diagnostics accessor declaration and these two tests:

```cpp
void test_transient_congestion_retries_same_packet_and_recovers()
{
    FakeBackend backend;
    FakeEntropy entropy;
    FakeNotificationTransport transport;
    transport.results = {
        VoiceNotificationSendResult::Congested,
        VoiceNotificationSendResult::Congested,
        VoiceNotificationSendResult::Accepted};
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_notification_transport(&transport);
    controller.set_ready(true, 256);
    controller.handle_button(true, 2000);

    controller.run_transmit_worker_until_idle_for_test(2001);
    controller.run_transmit_worker_until_idle_for_test(2010);
    controller.run_transmit_worker_until_idle_for_test(2020);

    assert(transport.sent.size() == 3);
    assert(transport.sent[0].kind == voice_input::NotificationKind::PttBegin);
    assert(transport.sent[0].session == transport.sent[1].session);
    assert(transport.sent[0].sequence == transport.sent[1].sequence);
    assert(transport.sent[1].sequence == transport.sent[2].sequence);
    const auto diagnostics = controller.transport_diagnostics_for_test();
    assert(diagnostics.congestion_retries == 2);
    assert(diagnostics.failure_code == 0);
}

void test_sustained_congestion_fails_after_elapsed_deadline()
{
    FakeBackend backend;
    FakeEntropy entropy;
    FakeNotificationTransport transport;
    transport.results.assign(
        64, VoiceNotificationSendResult::Congested);
    voice_input::crypto::CcmContext crypto(kKey);
    VoiceInputController controller(backend, crypto, entropy);
    controller.set_notification_transport(&transport);
    controller.set_ready(true, 256);
    controller.handle_button(true, 3000);

    controller.run_transmit_worker_until_idle_for_test(3001);
    controller.run_transmit_worker_until_idle_for_test(3249);
    assert(controller.ui_state() == VoiceInputUiState::Active);
    controller.run_transmit_worker_until_idle_for_test(3252);

    assert(controller.ui_state() == VoiceInputUiState::Error);
    const auto diagnostics = controller.transport_diagnostics_for_test();
    assert(diagnostics.failure_code != 0);
    assert(diagnostics.failure_detail != 0);
    assert(backend.release_calls == 1);
}
```

Call both from the test executable's `main()`.

- [ ] **Step 2: Run host validation and verify RED**

Run the host command from Task 4. Expected: the elapsed-time assertions fail
because the current controller still converts three immediate congestion
results into permanent failure.

- [ ] **Step 3: Replace retry-count failure with elapsed-time policy**

Track the first congestion timestamp. Keep the exact queue head until either
acceptance or 250 ms have elapsed. Reset the timestamp on acceptance, new
session, disconnect, and terminal cleanup. Retain a retry counter only for
diagnostics.

- [ ] **Step 4: Add bounded diagnostics**

Expose this host-readable snapshot:

```cpp
struct VoiceTransportDiagnostics {
    uint32_t max_queued_audio_ms = 0;
    uint32_t longest_accepted_gap_ms = 0;
    uint32_t congestion_retries = 0;
    uint16_t failure_code = 0;
    uint16_t failure_detail = 0;
    bool terminal_delivered = false;
};
```

Update it under the controller mutex without logging notification bodies. Add
assertions for the recovery and sustained-failure cases.

- [ ] **Step 5: Run host validation and verify GREEN**

Expected: all `53/53` stages pass.

- [ ] **Step 6: Commit congestion and diagnostics**

```bash
git add tests/voice_input_controller_test.cpp \
  main/apps/app_codex_status/voice_input_controller.h \
  main/apps/app_codex_status/voice_input_controller.cpp
git commit -m "fix: bound voice congestion by elapsed time"
```

### Task 6: Verify, Build, and Flash the Application Partition

**Files:**
- Verification only; no source modification is expected in this task.

- [ ] **Step 1: Run final source checks**

```bash
git diff --check
rg -n "take_notification|record_notification_result|notify_voice\(" \
  main/apps/app_codex_status/app_codex_status.cpp
```

Expected: no whitespace errors and no production voice draining calls in the
Codex page.

- [ ] **Step 2: Run complete host validation**

```bash
IDF_PATH=/Users/luke/esp/esp-idf \
  ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: `Unified host-test validation passed.`

- [ ] **Step 3: Build the firmware candidate and enforce capacity gates**

Use the normal candidate script when the upstream optional-component Kconfig
graph is healthy:

```bash
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/build_unified_candidate.sh
```

If the already documented `CODEC_UAC_SUPPORT` optional Kconfig defect blocks
reconfiguration, rebuild only the changed objects, archive, ELF, and image
from the existing coherent Ninja graph, then run:

```bash
python3 scripts/report_firmware_metrics.py \
  --repo-root . \
  --build-dir build-unified \
  --partition-table partitions.csv \
  --assets-bin build-unified/generated_assets.bin \
  --max-app-bytes 0x660000 \
  --max-assets-bytes 0xA0000 \
  --source ui-independent-voice-transport \
  --output build-unified/unified-runtime-metrics.json
```

Expected: application hash changes, `capacity_gate_passed` is true, and the
application remains below both release and partition limits.

- [ ] **Step 4: Flash only the application partition**

Stop the Agent and serial monitor, then run:

```bash
source /Users/luke/esp/esp-idf/export.sh
python -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodem21201 --baud 460800 \
  write_flash 0x020000 build-unified/StopWatch-UserDemo.bin
```

Expected: flash hash verified and hard reset. Do not erase or write NVS,
bootloader, partition table, FAT, or assets.

### Task 7: Physical Stress Acceptance and Report

**Files:**
- Modify: `docs/superpowers/reports/multi-agent-secure-pairing-validation.md`

- [ ] **Step 1: Reconnect and verify baseline**

Open the installed Agent, open the Codex watch app, and confirm logs reach
`Bluetooth state: connected` and `Voice state: ready` without re-pairing.

- [ ] **Step 2: Run ten 60-second sessions**

For each session, capture Agent and USB timestamps. During selected sessions,
trigger a task alert, carousel update, Agent-panel pull-down, and normal page
interaction. Each session must transition `recording -> ready`, with no error
5, disconnect, or premature Typeless release.

- [ ] **Step 3: Exercise reconnect and Agent lifecycle boundaries**

Reconnect the existing Agent and repeat one 60-second session. If a second
authorized Agent is available, switch away and back, then repeat. Verify no
packet from the prior connection epoch is delivered after promotion.

- [ ] **Step 4: Record redacted evidence**

Update the validation report with pass/fail counts, firmware commit/hash,
maximum queued milliseconds, longest accepted-send gap, and failure codes if
any. Do not include audio, keys, verification codes, packet bodies, or public
key material.

- [ ] **Step 5: Run completion verification and commit**

```bash
git diff --check
IDF_PATH=/Users/luke/esp/esp-idf \
  ./scripts/run_unified_offline_validation.sh --host-tests-only
git add docs/superpowers/reports/multi-agent-secure-pairing-validation.md
git commit -m "docs: validate independent voice transport"
git status --short
```

Expected: host validation passes, the physical matrix records ten successful
sessions, and the worktree is clean.
