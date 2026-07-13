# Unified Runtime Assistant Merge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge Launcher and Xiaozhi Assistant into one ESP-IDF firmware so Assistant opens and closes without rebooting, releases all session resources on exit, and runs from one application partition.

**Architecture:** Keep ESP-IDF/FreeRTOS and Mooncake as the application shell, add a small `RuntimeSupervisor` for ownership policy, use ESP-Brookesia 0.7 services and Xiaozhi Agent behind StopWatch-specific HAL adapters, and keep StopWatch HAL as the only board-level hardware owner. The implementation is gated: prove the selected Brookesia Xiaozhi path on hardware before removing the current dual-image fallback.

**Tech Stack:** C++17, ESP-IDF 5.5.4, FreeRTOS, Mooncake, LVGL 9, ESP-Brookesia 0.7, ESP-SR/GMF audio stack, Python unittest source contracts, host C++ tests, ESP32-S3 hardware tests

---

## Delivery Boundaries

This plan implements the confirmed first phase only:

- one Launcher/Assistant firmware;
- one main application partition;
- Assistant open and close without reboot;
- complete Assistant session shutdown;
- USB full-flash installation;
- reusable audio, Wi-Fi, and lifecycle boundaries for a later Codex voice project.

It does not implement Recovery OTA, Codex-to-Mac audio transport, Mac STT, BLE credential exchange, or a different operating system.

## Refined Work Estimate

| Task | Work | Person-days |
|---|---|---:|
| 1 | Contracts and fresh baselines | 1–2 |
| 2 | Brookesia dependency/bootstrap proof | 2–3 |
| 3 | Resource ownership policy | 2–3 |
| 4 | Reopenable StopWatch audio backend | 3–5 |
| 5 | Brookesia audio HAL/service integration | 3–5 |
| 6 | Shared Wi-Fi ownership migration | 3–4 |
| 7 | Xiaozhi compatibility gate | 3–5 |
| 8 | Assistant lifecycle runtime | 4–6 |
| 9 | Mooncake Assistant UI integration | 2–4 |
| 10 | Single partition and full-flash migration | 1–2 |
| 11 | Diagnostics and 100-cycle validation | 3–5 |
| 12 | Full product regression and release candidate | 2–4 |
| **Total implementation** |  | **29–48** |

The critical path is Tasks 2 → 4 → 5 → 7 → 8 → 9 → 10 → 11 → 12. Allow 30–50 person-days including hardware-specific rework. The gate in Task 7 can deliberately stop the project before destructive dual-image cleanup if the official Agent does not meet product parity.

## Fixed Technical Decisions

- Pin every Brookesia direct dependency to `>=0.7.0,<0.8.0`, then commit `dependencies.lock` to pin exact patch versions.
- Do not add `brookesia_hal_boards` or replace StopWatch board initialization with ESP Board Manager.
- StopWatch HAL initializes physical display, buses, codec, I2S, PMIC, buttons, BLE, and Wi-Fi capabilities only once.
- Brookesia manages service and Agent lifetimes; Mooncake manages foreground application and UI lifetimes.
- For the validated Brookesia path, `AssistantRuntime` is the concrete `XiaozhiVoiceTransport` boundary from the design: the Agent component owns its internal encoded-audio transport, so the firmware must not create a duplicate PCM/Opus sender beside it. A later `CodexMacVoiceTransport` will consume the shared Audio service's encoded-frame output independently.
- Assistant shutdown is cooperative and observable. Never use external `vTaskDelete()` to claim that a service stopped.
- Do not remove the dual-image fallback until the Brookesia Xiaozhi compatibility gate passes on the watch.
- Keep the unrelated untracked plan `docs/superpowers/plans/2026-07-12-firmware-low-risk-size-reduction.md` outside all commits in this plan.

## File Map

### Runtime and tests

- Create `main/runtime/resource_supervisor.h` and `.cpp`: pure ownership policy for Wi-Fi, capture, playback, and Assistant lifecycle.
- Create `main/runtime/runtime_supervisor.h` and `.cpp`: Brookesia bindings, asynchronous start/stop orchestration, timeout handling, and diagnostics.
- Create `main/runtime/runtime_diagnostics.h` and `.cpp`: heap, PSRAM, task, owner, and timing snapshots.
- Create `tests/resource_supervisor_test.cpp`: host tests for ownership, conflicts, idempotence, and release order.
- Create `tests/assistant_state_machine_test.cpp`: host tests for Assistant start/stop transitions and timeout behavior.
- Create `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`: source, dependency, partition, and flash-script contract.

### HAL and services

- Create `main/hal/audio/stopwatch_audio_backend.h` and `.cpp`: reusable logical recorder/player lifecycle over the existing one-time codec/I2S initialization.
- Create `main/hal/audio/audio_backend_lifecycle.h` and `.cpp`: ESP-IDF-free recorder/player state used by host tests.
- Create `main/runtime/services/stopwatch_audio_device.h` and `.cpp`: Brookesia audio HAL interfaces backed by StopWatch HAL.
- Create `main/runtime/services/shared_wifi_service.h` and `.cpp`: reference-counted STA lease and exclusive AP session over the existing Wi-Fi implementation.
- Create `main/runtime/services/shared_wifi_policy.h` and `.cpp`: ESP-IDF-free STA/AP ownership policy used by host tests.
- Modify `main/hal/hal_audio.cpp` and `main/hal/hal.h`: preserve existing public facade while routing through the new backend.
- Modify `main/apps/app_codex_status/app_codex_status.cpp`: obtain and release a shared STA lease instead of owning Wi-Fi startup.
- Modify `main/hal/utils/config_ap/config_ap.cpp`: obtain an exclusive AP session instead of directly competing with STA owners.

### Assistant

- Create `main/runtime/assistant/assistant_state.h` and `.cpp`: pure state machine.
- Create `main/runtime/assistant/assistant_runtime.h` and `.cpp`: Xiaozhi Agent binding, events, start/stop, and stop acknowledgement.
- Create `main/apps/app_assistant/view/assistant_view.h` and `.cpp`: LVGL UI independent of Xiaozhi board/display classes.
- Modify `main/apps/app_assistant/app_assistant.h` and `.cpp`: replace partition switching with asynchronous runtime lifecycle.
- Modify `main/main.cpp`: start the shared runtime after StopWatch HAL and before installing applications.
- Modify `main/CMakeLists.txt`: include runtime sources and only the assets/models required by the selected Agent.
- Create `main/cmake/unified_assets.cmake`: generate `build/generated_assets.bin` from the unified `sdkconfig` and selected wake-word model.
- Modify `main/idf_component.yml`, `sdkconfig.defaults`, and generated `dependencies.lock`: add and pin the validated Brookesia 0.7 dependency set.

### Single-image packaging

- Modify `partitions.csv`: replace `ota_0`/`ota_1` with one 7.5 MiB factory application partition.
- Modify `scripts/flash_full_assistant_stack.sh`: flash one application and the assets image at the new offset.
- Delete `scripts/flash_assistant_runtime.sh`: the standalone Assistant image no longer exists after the compatibility gate.
- Delete `scripts/force_boot_slot.py` if no other supported workflow uses it.
- Create `scripts/report_firmware_metrics.py`: emit machine-readable Flash, static RAM, heap, and headroom results.
- Modify `README.md`: document the unified runtime, full-flash migration, and rollback limitations.

## Milestone 0: Establish Reproducible Baselines

### Task 1: Lock The Unified-Firmware Contract

**Files:**
- Create: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`
- Create: `scripts/report_firmware_metrics.py`
- Create: `docs/superpowers/reports/unified-runtime-baseline.json`

- [x] **Step 1: Write the failing source contract**

The unittest must resolve paths from the repository root and initially assert the final state:

```python
class UnifiedRuntimeFirmwareContractTest(unittest.TestCase):
    def test_only_launcher_defines_app_main(self):
        definitions = find_definitions(r'extern\s+"C"\s+void\s+app_main\s*\(')
        self.assertEqual(definitions, ["main/main.cpp"])

    def test_assistant_does_not_switch_partitions_or_restart(self):
        source = read("main/apps/app_assistant/app_assistant.cpp")
        self.assertNotIn("esp_ota_set_boot_partition", source)
        self.assertNotIn("esp_restart", source)
        self.assertNotIn('"ota_1"', source)

    def test_partition_table_has_one_application(self):
        rows = parse_partition_csv("partitions.csv")
        app_rows = [row for row in rows if row[1] == "app"]
        self.assertEqual(len(app_rows), 1)
        self.assertEqual(app_rows[0][2], "factory")

    def test_brookesia_is_pinned_to_zero_seven(self):
        manifest = read("main/idf_component.yml")
        self.assertNotIn("version: '*'", manifest)
        self.assertNotIn("brookesia_hal_boards", manifest)
        for component in REQUIRED_BROOKESIA_COMPONENTS:
            self.assertRegex(manifest, rf"{component}:.*>=0\.7\.0,<0\.8\.0")
```

Also assert that `flash_assistant_runtime.sh` is absent, the full-flash script does not reference `xiaozhi.bin`, and no supported source depends on the labels `ota_0` or `ota_1`.

- [x] **Step 2: Run the contract and confirm RED**

```bash
python3 -m unittest mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
```

Expected: failures for the current dual-image architecture, missing Brookesia dependencies, and old flash scripts.

- [x] **Step 3: Add a read-only metrics reporter**

Implement `scripts/report_firmware_metrics.py` using only the Python standard library. It must read `build/project_description.json`, `build/StopWatch-UserDemo.map`, `build/StopWatch-UserDemo.bin`, and partition CSV data, then write JSON with these keys:

```json
{
  "app_bytes": 4283824,
  "app_partition_bytes": 5177344,
  "app_headroom_bytes": 893520,
  "diram_bytes": 160711,
  "source": "launcher-baseline"
}
```

Fail with a clear message if a required build artifact is missing; do not silently output zeros.

- [x] **Step 4: Capture the current two-image baseline**

```bash
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py build
python3 scripts/report_firmware_metrics.py --output docs/superpowers/reports/unified-runtime-baseline.json
wc -c build/StopWatch-UserDemo.bin third_party/xiaozhi-esp32/build-m5stack-stopwatch/xiaozhi.bin
```

Record the current measured values and device commit SHA. Preserve the known reference values of 4,283,824 bytes for Launcher and 2,907,312 bytes for Assistant only if the fresh measurements match.

- [x] **Step 5: Commit the contract and baseline**

```bash
git add mac-codex-agent/tests/test_unified_runtime_firmware_contract.py scripts/report_firmware_metrics.py docs/superpowers/reports/unified-runtime-baseline.json
git commit -m "Add unified runtime firmware contract"
```

## Milestone 1: Prove Brookesia On The Existing Launcher

### Task 2: Pin Brookesia 0.7 And Start The Service Manager

**Files:**
- Modify: `main/idf_component.yml`
- Create: `main/runtime/service_runtime.h`
- Create: `main/runtime/service_runtime.cpp`
- Modify: `main/main.cpp`
- Modify: `main/CMakeLists.txt`
- Create/Modify: `dependencies.lock`

- [x] **Step 1: Add the smallest dependency set**

Add direct dependencies with the same range:

```yaml
  espressif/brookesia_service_manager:
    version: ">=0.7.0,<0.8.0"
  espressif/brookesia_service_helper:
    version: ">=0.7.0,<0.8.0"
  espressif/brookesia_hal_interface:
    version: ">=0.7.0,<0.8.0"
```

Do not add `brookesia_hal_adaptor` or `brookesia_hal_boards`.

- [x] **Step 2: Resolve and verify exact versions**

```bash
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
idf.py reconfigure
python3 - <<'PY'
from pathlib import Path
lock = Path("dependencies.lock").read_text()
assert "brookesia_service_manager" in lock
assert "brookesia_hal_boards" not in lock
for line in lock.splitlines():
    if "version:" in line and "0.8." in line:
        raise SystemExit(f"unexpected 0.8 dependency: {line}")
PY
```

If a transitive dependency selects 0.8, pin the owning direct Brookesia component to a compatible 0.7 patch and rerun; do not continue with a mixed 0.7/0.8 graph.

- [x] **Step 3: Add the runtime bootstrap**

Implement one-shot startup:

```cpp
bool ServiceRuntime::start()
{
    if (started_) return true;
    auto &manager = esp_brookesia::service::ServiceManager::get_instance();
    if (!manager.start()) return false;
    started_ = true;
    return true;
}
```

Call it in `app_main()` after `Hal::init()` succeeds and before Mooncake installs applications. A startup failure must show a fatal diagnostic and must not enter a partially initialized app loop.

- [ ] **Step 4: Build and smoke-test the Launcher**

```bash
idf.py fullclean
idf.py build
```

Flash the existing dual-partition package, boot to Launcher, and verify Clock, Codex, Badge, IMU, FFT, and Setup still open and return normally. Capture boot logs proving the service manager starts exactly once.

- [x] **Step 5: Commit**

```bash
git add main/idf_component.yml dependencies.lock main/runtime/service_runtime.h main/runtime/service_runtime.cpp main/main.cpp main/CMakeLists.txt
git commit -m "Bootstrap Brookesia service runtime"
```

### Task 3: Add A Testable Resource Ownership Policy

**Files:**
- Create: `main/runtime/resource_supervisor.h`
- Create: `main/runtime/resource_supervisor.cpp`
- Create: `tests/resource_supervisor_test.cpp`

- [ ] **Step 1: Write failing host tests**

Cover the policy independently of ESP-IDF:

```cpp
ResourceSupervisor resources;
assert(resources.acquireWifi(RuntimeOwner::Assistant) == AcquireResult::Granted);
assert(resources.acquireWifi(RuntimeOwner::Codex) == AcquireResult::Granted);
assert(resources.wifiLeaseCount() == 2);
assert(resources.acquireCapture(RuntimeOwner::Assistant) == AcquireResult::Granted);
assert(resources.acquireCapture(RuntimeOwner::Codex) == AcquireResult::Busy);
assert(resources.releaseCapture(RuntimeOwner::Assistant) == ReleaseResult::Released);
assert(resources.acquireCapture(RuntimeOwner::Codex) == AcquireResult::Granted);
assert(resources.releaseWifi(RuntimeOwner::Assistant) == ReleaseResult::Released);
assert(resources.wifiLeaseCount() == 1);
assert(resources.releaseWifi(RuntimeOwner::Codex) == ReleaseResult::LastOwnerReleased);
```

Also test idempotent duplicate release, playback conflict, AP-mode conflict with STA leases, and a blocked resource after stop timeout.

- [ ] **Step 2: Compile and confirm RED**

```bash
c++ -std=c++17 -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test
```

Expected: compilation fails because the policy types do not exist.

- [ ] **Step 3: Implement the minimal policy**

Use explicit types:

```cpp
enum class RuntimeOwner { None, Assistant, Codex, Badge, System };
enum class AcquireResult { Granted, AlreadyOwned, Busy, Blocked };
enum class ReleaseResult { Released, LastOwnerReleased, NotOwned };

struct ResourceSnapshot {
    RuntimeOwner capture_owner;
    RuntimeOwner playback_owner;
    std::size_t wifi_lease_count;
    bool wifi_ap_active;
    bool blocked;
};
```

Keep this class free of FreeRTOS, Brookesia, and LVGL includes. The embedded orchestration layer will serialize calls.

- [ ] **Step 4: Run GREEN**

```bash
c++ -std=c++17 -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test
/tmp/resource_supervisor_test
```

- [ ] **Step 5: Commit**

```bash
git add main/runtime/resource_supervisor.h main/runtime/resource_supervisor.cpp tests/resource_supervisor_test.cpp
git commit -m "Add runtime resource ownership policy"
```

## Milestone 2: Make StopWatch Hardware Reusable

### Task 4: Refactor Audio Into A Reopenable Backend

**Files:**
- Create: `main/hal/audio/stopwatch_audio_backend.h`
- Create: `main/hal/audio/stopwatch_audio_backend.cpp`
- Create: `main/hal/audio/audio_backend_lifecycle.h`
- Create: `main/hal/audio/audio_backend_lifecycle.cpp`
- Modify: `main/hal/hal_audio.cpp`
- Modify: `main/hal/hal.h`
- Modify: `main/CMakeLists.txt`
- Create: `tests/audio_backend_lifecycle_test.cpp`

- [ ] **Step 1: Extract the logical lifecycle contract**

Define a hardware-independent state object used by the backend and test it first:

```cpp
AudioBackendLifecycle state;
assert(state.openRecorder() == AudioOpenResult::Opened);
assert(state.openRecorder() == AudioOpenResult::AlreadyOpen);
assert(state.openPlayer() == AudioOpenResult::Opened);
assert(state.closeRecorder() == AudioCloseResult::Closed);
assert(!state.recorderOpen());
assert(state.playerOpen());
assert(state.closePlayer() == AudioCloseResult::Closed);
```

Run the host test and confirm RED before implementing the state object.

- [ ] **Step 2: Move physical audio ownership out of the file-local singleton**

Move the codec/I2S fields currently hidden in `main/hal/hal_audio.cpp` into `StopWatchAudioBackend`. Preserve one-time physical initialization, but add session-level methods:

```cpp
bool openRecorder(uint32_t sample_rate, uint8_t bits, uint8_t channels);
void closeRecorder();
std::size_t read(void *data, std::size_t bytes, TickType_t timeout);
bool openPlayer(uint32_t sample_rate, uint8_t bits, uint8_t channels);
void closePlayer();
std::size_t write(const void *data, std::size_t bytes, TickType_t timeout);
bool setVolume(uint8_t percent);
bool setGeneralGain(float gain_db);
```

Codec bus and I2S channel installation remain one-time. `open` and `close` only enable/disable the corresponding data path and session task. Do not create a permanent playback task when no playback session is active.

- [ ] **Step 3: Preserve existing HAL behavior**

Keep `Hal::audioRecord`, `Hal::audioPlay`, and `Hal::getAudioSampleRate` as compatibility facades. Route them through a `System` session so Boot SFX and existing FFT behavior remain unchanged while applications migrate.

- [ ] **Step 4: Prove repeated logical open/close on hardware**

Add temporary diagnostic commands or a guarded boot test that executes 100 recorder and player open/close cycles without reinstalling I2S. Verify:

- no `ESP_ERR_INVALID_STATE`;
- no task count growth;
- internal heap after the loop differs by at most 4 KiB;
- codec and I2S physical initialization each log exactly once.

Remove the temporary automatic loop before committing; retain a callable diagnostic method.

- [ ] **Step 5: Run regression and commit**

```bash
c++ -std=c++17 -Imain tests/audio_backend_lifecycle_test.cpp main/hal/audio/audio_backend_lifecycle.cpp -o /tmp/audio_backend_lifecycle_test
/tmp/audio_backend_lifecycle_test
idf.py build
git add main/hal/audio main/hal/hal_audio.cpp main/hal/hal.h main/CMakeLists.txt tests/audio_backend_lifecycle_test.cpp
git commit -m "Make StopWatch audio lifecycle reusable"
```

### Task 5: Register StopWatch Audio With Brookesia

**Files:**
- Modify: `main/idf_component.yml`
- Create: `main/runtime/services/stopwatch_audio_device.h`
- Create: `main/runtime/services/stopwatch_audio_device.cpp`
- Modify: `main/runtime/service_runtime.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `dependencies.lock`

- [ ] **Step 1: Add the audio service dependencies**

```yaml
  espressif/brookesia_service_device:
    version: ">=0.7.0,<0.8.0"
  espressif/brookesia_service_audio:
    version: ">=0.7.0,<0.8.0"
```

Reconfigure and re-run the dependency assertions from Task 2.

- [ ] **Step 2: Implement the exact Brookesia HAL interfaces**

Wrap `StopWatchAudioBackend` in:

```cpp
class StopWatchPlayer final : public esp_brookesia::hal::AudioCodecPlayerIface {
public:
    bool open(const Config &config) override;
    void close() override;
    bool set_volume(uint8_t volume) override;
    bool write_data(const uint8_t *data, size_t size) override;
    bool is_pa_on_off_supported() override;
    bool set_pa_on_off(bool on) override;
    bool is_pa_on() const override;
};

class StopWatchRecorder final : public esp_brookesia::hal::AudioCodecRecorderIface {
public:
    bool open() override;
    void close() override;
    bool read_data(uint8_t *data, size_t size) override;
    bool set_general_gain(float gain) override;
    bool set_channel_gains(const std::map<uint8_t, float> &gains) override;
};
```

Register both from a StopWatch `hal::Device` plugin. Do not depend on `brookesia_hal_adaptor` merely to obtain board discovery.

- [ ] **Step 3: Initialize the device before binding audio**

In `ServiceRuntime::start()`, initialize the StopWatch audio device once, then start the service manager. Assert both audio interfaces exist before accepting Assistant startup.

- [ ] **Step 4: Bind and release the Audio service on hardware**

Run 20 cycles of:

```cpp
auto binding = manager.bind(AudioHelper::get_name().data());
assert(binding.is_valid());
// record PCM, encode/decode one short frame, play it
binding = {};
```

After each release, assert recorder/player logical sessions are closed. At the end, compare heap and task snapshots with the pre-test baseline.

- [ ] **Step 5: Commit**

```bash
git add main/idf_component.yml dependencies.lock main/runtime/services/stopwatch_audio_device.h main/runtime/services/stopwatch_audio_device.cpp main/runtime/service_runtime.cpp main/CMakeLists.txt
git commit -m "Adapt StopWatch audio to Brookesia"
```

### Task 6: Unify Wi-Fi Ownership Without Replacing Stable Drivers

**Files:**
- Create: `main/runtime/services/shared_wifi_service.h`
- Create: `main/runtime/services/shared_wifi_service.cpp`
- Create: `main/runtime/services/shared_wifi_policy.h`
- Create: `main/runtime/services/shared_wifi_policy.cpp`
- Create: `tests/shared_wifi_policy_test.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `main/hal/utils/config_ap/config_ap.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Test STA lease and AP session policy**

Write host tests proving:

- the first STA lease starts the existing `WifiManager`;
- additional STA leases share it;
- releasing a non-last lease leaves Wi-Fi running;
- releasing the last lease stops STA only when no system policy requires it;
- Badge AP is rejected while any STA lease exists;
- new STA leases are rejected while Badge AP is active;
- failed startup rolls back the lease count.

Inject a fake driver into `SharedWifiService`; do not mock ESP-IDF globals.

- [ ] **Step 2: Implement RAII leases**

Expose move-only handles:

```cpp
class WifiStaLease {
public:
    WifiStaLease(WifiStaLease&&) noexcept;
    ~WifiStaLease();
    bool valid() const;
};

class WifiApSession {
public:
    WifiApSession(WifiApSession&&) noexcept;
    ~WifiApSession();
    bool valid() const;
};
```

The service wraps the current, validated Wi-Fi code. It does not add a second `esp_wifi_init()` owner. Brookesia manages the higher-level lifetime through `RuntimeSupervisor`; StopWatch retains the driver implementation.

- [ ] **Step 3: Migrate Codex Wi-Fi fallback**

Replace `ensure_network_started()` ownership with a STA lease held for the WebSocket session. BLE-only Codex monitoring must not acquire Wi-Fi. Closing Codex releases its lease after the WebSocket stops.

- [ ] **Step 4: Migrate Badge configuration AP**

Acquire an AP session before starting DNS/HTTP, and release it only after DNS/HTTP callbacks and sockets stop. Remove direct duplicated Wi-Fi initialization from `config_ap.cpp` while preserving the existing captive portal behavior.

- [ ] **Step 5: Run tests and hardware coexistence checks**

```bash
c++ -std=c++17 -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp -o /tmp/shared_wifi_policy_test
/tmp/shared_wifi_policy_test
idf.py build
```

On hardware, verify Codex BLE monitoring, Codex WebSocket fallback, Badge AP, and return-to-Launcher behavior. Confirm no `ESP_ERR_INVALID_STATE` from repeated Wi-Fi init/deinit.

- [ ] **Step 6: Commit**

```bash
git add main/runtime/services main/apps/app_codex_status/app_codex_status.cpp main/hal/utils/config_ap/config_ap.cpp main/CMakeLists.txt tests/shared_wifi_policy_test.cpp
git commit -m "Centralize runtime Wi-Fi ownership"
```

## Milestone 3: Prove And Integrate Xiaozhi Agent

### Task 7: Run The Xiaozhi Compatibility Gate

**Files:**
- Modify: `main/idf_component.yml`
- Modify: `sdkconfig.defaults`
- Modify: `dependencies.lock`
- Create: `main/cmake/unified_assets.cmake`
- Modify: `main/CMakeLists.txt`
- Create: `docs/superpowers/reports/brookesia-xiaozhi-compatibility.md`
- Create: `main/runtime/assistant/assistant_probe.cpp`

- [ ] **Step 1: Add only the required Agent dependencies**

Add the stable 0.7 range for:

```yaml
  espressif/brookesia_agent_manager:
    version: ">=0.7.0,<0.8.0"
  espressif/brookesia_agent_xiaozhi:
    version: ">=0.7.0,<0.8.0"
  espressif/brookesia_service_nvs:
    version: ">=0.7.0,<0.8.0"
  espressif/brookesia_service_sntp:
    version: ">=0.7.0,<0.8.0"
```

If `brookesia_service_wifi` is a required transitive dependency, pin it explicitly to the same range but keep it behind the StopWatch Wi-Fi ownership adapter. Re-run the lockfile 0.8 exclusion.

- [ ] **Step 2: Configure the existing product identity and audio model**

Map the current Xiaozhi activation identity, server configuration, wake model partition, language, 16 kHz input, Opus settings, and volume to the Brookesia Agent configuration. Do not create a new user-visible device identity unless the server protocol requires reactivation and the user approves it.

Create `main/cmake/unified_assets.cmake` with an `ALL` custom target that invokes the existing asset generator against the unified configuration:

```cmake
set(UNIFIED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
add_custom_command(
    OUTPUT "${UNIFIED_ASSETS_BIN}"
    COMMAND "${PYTHON}" "${PROJECT_DIR}/third_party/xiaozhi-esp32/scripts/build_default_assets.py"
            --sdkconfig "${SDKCONFIG}"
            --output "${UNIFIED_ASSETS_BIN}"
            --esp_sr_model_path "${PROJECT_DIR}/managed_components/espressif__esp-sr/model"
    DEPENDS "${SDKCONFIG}"
            "${PROJECT_DIR}/third_party/xiaozhi-esp32/scripts/build_default_assets.py"
    VERBATIM
)
add_custom_target(generated_unified_assets ALL DEPENDS "${UNIFIED_ASSETS_BIN}")
```

Include this file from `main/CMakeLists.txt`. Configure exactly the wake model that passed the existing Assistant baseline; do not package Xiaozhi's alternate board fonts, emoji packs, or display assets that the Mooncake UI does not use. Assert `build/generated_assets.bin` is non-empty and at most 4 MiB.

- [ ] **Step 3: Build a temporary probe outside Mooncake navigation**

The probe must:

1. bind the Audio and Agent manager services;
2. register Xiaozhi with `SetAgentInfo`;
3. subscribe to general Agent and Xiaozhi events;
4. call `Activate`, then `Start` asynchronously;
5. display/log activation code, connection state, user text, assistant text, emotion, and errors;
6. call `Stop`, wait for the `Stopped` event, disconnect subscriptions, and release bindings.

Use the official helper APIs and event acknowledgement; do not infer stopped state from a fixed delay.

- [ ] **Step 4: Execute the hardware compatibility matrix**

Record pass/fail and logs for:

| Capability | Required result |
|---|---|
| Existing activation or reactivation | succeeds and is documented |
| Wake phrase `你好小智` | detected locally |
| Manual start/listen | starts a session |
| User text | delivered to UI callback |
| Assistant text/emotion | delivered to UI callback |
| TTS audio | audible and stable |
| Existing Xiaozhi/MCP product functions | preserved or explicitly proven out-of-scope |
| `Stop` while idle | acknowledged within 2 s |
| `Stop` while recording | acknowledged within 2 s |
| `Stop` while playing | acknowledged within 2 s |
| `Stop` while reconnecting | acknowledged within 2 s |
| Post-stop tasks/connections | zero Agent session residue |

- [ ] **Step 5: Enforce the gate**

Continue to Task 8 only if every required row passes. If any required row fails, commit the report and stop implementation. Revise the design with a scoped custom Xiaozhi service adapter around the current protocol/audio modules; do not delete the current Assistant image and do not weaken the acceptance criteria.

- [ ] **Step 6: Commit the passing probe/report**

```bash
git add main/idf_component.yml sdkconfig.defaults dependencies.lock main/cmake/unified_assets.cmake main/CMakeLists.txt main/runtime/assistant/assistant_probe.cpp docs/superpowers/reports/brookesia-xiaozhi-compatibility.md
git commit -m "Validate Brookesia Xiaozhi compatibility"
```

### Task 8: Implement The Assistant Runtime State Machine

**Files:**
- Create: `main/runtime/assistant/assistant_state.h`
- Create: `main/runtime/assistant/assistant_state.cpp`
- Create: `main/runtime/assistant/assistant_runtime.h`
- Create: `main/runtime/assistant/assistant_runtime.cpp`
- Create: `tests/assistant_state_machine_test.cpp`
- Modify: `main/runtime/runtime_supervisor.h`
- Modify: `main/runtime/runtime_supervisor.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Write failing state tests**

Test exactly these legal transitions:

```text
Stopped -> Starting -> Running -> Stopping -> Stopped
Stopped -> Starting -> Failed -> Stopping -> Stopped
Running -> Stopping -> StopTimedOut
StopTimedOut -> Stopped        only after the real stopped event arrives
```

Assert that duplicate `requestStop()` is idempotent, `start()` is rejected outside `Stopped`, and a timed-out session keeps capture/playback blocked.

- [ ] **Step 2: Implement the pure state machine and run GREEN**

```bash
c++ -std=c++17 -Imain tests/assistant_state_machine_test.cpp main/runtime/assistant/assistant_state.cpp -o /tmp/assistant_state_machine_test
/tmp/assistant_state_machine_test
```

- [ ] **Step 3: Wrap Brookesia Agent lifecycle**

Implement the confirmed interface:

```cpp
class AssistantRuntime {
public:
    StartResult start(const AssistantDependencies &dependencies);
    void requestStop(StopReason reason);
    StopResult waitStopped(TickType_t timeout);
    AssistantRuntimeState state() const;
    bool pollEvent(AssistantEvent &event);
};
```

`start()` obtains Wi-Fi, capture, playback, Audio service, and Agent service in that order. `requestStop()` disables new input, sends Agent `Stop`, and returns immediately. A runtime worker observes the stopped event, disconnects callbacks, releases Agent/Audio bindings, releases playback/capture, then releases Wi-Fi last.

- [ ] **Step 4: Add stop timeout diagnostics**

At 2 seconds, transition to `StopTimedOut`, capture outstanding bindings/tasks/resources, and keep conflicting resources blocked. Never externally delete the Agent task. When the actual stopped event arrives later, finish normal release and transition to `Stopped`.

- [ ] **Step 5: Remove the temporary probe entry and build**

Keep its reusable setup/event code in `AssistantRuntime`, but remove automatic probe startup. Run host tests and `idf.py build`.

- [ ] **Step 6: Commit**

```bash
git add main/runtime/assistant main/runtime/runtime_supervisor.h main/runtime/runtime_supervisor.cpp main/CMakeLists.txt tests/assistant_state_machine_test.cpp
git commit -m "Add restart-free Assistant runtime"
```

### Task 9: Connect Assistant Runtime To Mooncake UI

**Files:**
- Create: `main/apps/app_assistant/view/assistant_view.h`
- Create: `main/apps/app_assistant/view/assistant_view.cpp`
- Modify: `main/apps/app_assistant/app_assistant.h`
- Modify: `main/apps/app_assistant/app_assistant.cpp`
- Modify: `main/CMakeLists.txt`
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`

- [ ] **Step 1: Extend the source contract**

Assert that `AppAssistant` calls `RuntimeSupervisor::startAssistant()` and `requestStopAssistant()`, does not include `esp_ota_ops.h` or `esp_system.h`, and does not reference a fixed partition label.

- [ ] **Step 2: Build the native Assistant view**

Use the current Launcher LVGL instance. Display these stable states without constructing the old Xiaozhi board/display:

- Connecting Wi-Fi
- Activating with code
- Listening
- Thinking
- Speaking
- Connection failed / retry
- Closing
- Stop timed out / restart available

Update text and emotion only from events drained on the Mooncake/LVGL thread.

- [ ] **Step 3: Make close asynchronous**

On user return:

1. set the UI to `Closing` and ignore new conversation actions;
2. call `requestStopAssistant()` once;
3. keep `onRunning()` responsive while polling runtime state;
4. destroy Assistant LVGL objects and call Mooncake `close()` only after `Stopped`;
5. on `StopTimedOut`, stay on the error screen and preserve the resource block.

Do not block the UI loop in `waitStopped()`.

- [ ] **Step 4: Verify open/close behavior on hardware**

Test first entry, second entry, return during each runtime state, and opening Clock/Codex immediately afterward. Confirm there is no boot animation or reset reason change between Launcher and Assistant.

- [ ] **Step 5: Run focused tests and commit**

```bash
python3 -m unittest mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
c++ -std=c++17 -Imain tests/assistant_state_machine_test.cpp main/runtime/assistant/assistant_state.cpp -o /tmp/assistant_state_machine_test
/tmp/assistant_state_machine_test
idf.py build
git add main/apps/app_assistant main/CMakeLists.txt mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
git commit -m "Run Assistant inside Mooncake"
```

## Milestone 4: Remove The Dual Image And Reclaim Flash

### Task 10: Convert To One Application Partition

**Files:**
- Modify: `partitions.csv`
- Modify: `scripts/flash_full_assistant_stack.sh`
- Delete: `scripts/flash_assistant_runtime.sh`
- Delete: `scripts/force_boot_slot.py` if unreferenced
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`
- Modify: `README.md`

- [ ] **Step 1: Replace the partition table**

Use the confirmed 16 MiB first-phase layout:

```csv
# Name,   Type, SubType, Offset,    Size,      Flags
nvs,      data, nvs,     0x9000,    0x4000,
phy_init, data, phy,     0xf000,    0x1000,
app,      app,  factory, 0x20000,   0x780000,
storage,  data, fat,     0x7a0000,  0x100000,
assets,   data, spiffs,  0x8a0000,  0x400000,
coredump, data, coredump,0xca0000,  0x10000,
```

The application partition is 7.5 MiB. The unused tail remains unpartitioned for future Recovery/storage decisions; do not silently consume it in this phase.

- [ ] **Step 2: Update the full-flash script**

Rename internal variables from Launcher/Assistant to unified App. Flash only:

```text
0x000000  build/bootloader/bootloader.bin
0x008000  build/partition_table/partition-table.bin
0x020000  build/StopWatch-UserDemo.bin
0x8a0000  build/generated_assets.bin
```

The unified build must always emit `build/generated_assets.bin` through `generated_unified_assets`. Enforce the app maximum `0x780000` and assets maximum `0x400000` before calling esptool.

- [ ] **Step 3: Delete obsolete slot tooling and source dependencies**

Delete the Assistant-only flash script. Delete `force_boot_slot.py` only after `rg -n "force_boot_slot" .` proves it has no supported caller. Remove boot-partition switching code from `AppAssistant` and exclude the old `third_party/xiaozhi-esp32/main/main.cc` and M5Stack StopWatch board runtime from the unified build.

- [ ] **Step 4: Run the complete contract GREEN**

```bash
python3 -m unittest mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
idf.py fullclean
idf.py build
python3 scripts/report_firmware_metrics.py --output build/unified-runtime-metrics.json
```

Require at least 15% application-partition headroom. With a 7.5 MiB partition, the app binary must be no larger than 6,684,672 bytes for this gate.

- [ ] **Step 5: Perform the mandatory one-time full flash**

```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
test -n "$PORT"
./scripts/flash_full_assistant_stack.sh "$PORT"
```

This step must rewrite the partition table, app, and assets together. A normal app-only `idf.py flash` from the old layout is not an accepted migration test.

- [ ] **Step 6: Document and commit**

Document that the first phase has no remote update or automatic rollback and that returning to the old dual-partition release requires another USB full flash.

```bash
git add partitions.csv scripts/flash_full_assistant_stack.sh mac-codex-agent/tests/test_unified_runtime_firmware_contract.py README.md
git add -u scripts/flash_assistant_runtime.sh scripts/force_boot_slot.py
git commit -m "Switch firmware to one application partition"
```

## Milestone 5: Validate Complete Shutdown And Product Stability

### Task 11: Add Runtime Diagnostics And Leak Gates

**Files:**
- Create: `main/runtime/runtime_diagnostics.h`
- Create: `main/runtime/runtime_diagnostics.cpp`
- Modify: `main/runtime/runtime_supervisor.cpp`
- Modify: `scripts/report_firmware_metrics.py`
- Create: `docs/superpowers/reports/unified-runtime-validation.md`

- [ ] **Step 1: Define a stable diagnostic snapshot**

```cpp
struct RuntimeDiagnosticSnapshot {
    size_t internal_free_heap;
    size_t internal_min_free_heap;
    size_t psram_free;
    size_t task_count;
    RuntimeOwner capture_owner;
    RuntimeOwner playback_owner;
    size_t wifi_lease_count;
    AssistantRuntimeState assistant_state;
    uint32_t last_start_ms;
    uint32_t last_stop_ms;
};
```

Expose it in logs and a debug-only serial command. Do not add a permanent high-frequency polling task.

- [ ] **Step 2: Record before/open/after snapshots**

For every Assistant session, record a snapshot immediately before resource acquisition, after `Running`, and after `Stopped`. Include remaining service bindings and Agent event subscriptions in stop logs.

- [ ] **Step 3: Run the 100-cycle stress test**

Automate or manually execute 100 open/close cycles with at least these injected exits:

- 20 idle/listening exits;
- 20 exits while recording;
- 20 exits during TTS playback;
- 20 exits during Wi-Fi/Xiaozhi reconnect;
- 20 normal completed conversations.

Require:

- zero crashes and watchdog resets;
- normal stop at or below 2,000 ms;
- after-stop internal heap within 4 KiB of its paired before snapshot;
- cumulative internal heap loss at or below 8 KiB after 100 cycles;
- Assistant audio/protocol/activation task count returns to zero;
- capture and playback owners return to `None`;
- Assistant Wi-Fi lease is absent after exit.

- [ ] **Step 4: Extend the metrics report**

Add final binary size, application headroom, static DIRAM, peak internal heap delta, peak PSRAM delta, maximum stop time, and 100-cycle cumulative delta to the validation report. Distinguish measured values from targets.

- [ ] **Step 5: Commit**

```bash
git add main/runtime/runtime_diagnostics.h main/runtime/runtime_diagnostics.cpp main/runtime/runtime_supervisor.cpp scripts/report_firmware_metrics.py docs/superpowers/reports/unified-runtime-validation.md
git commit -m "Add unified runtime lifecycle diagnostics"
```

### Task 12: Run Full Regression And Produce The Release Candidate

**Files:**
- Modify: `docs/superpowers/reports/unified-runtime-validation.md`
- Modify: `README.md`

- [ ] **Step 1: Run all automated tests**

```bash
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
for test in tests/*_test.cpp; do
  echo "Review and run the repository-defined compile command for $test"
done
idf.py fullclean
idf.py build
python3 scripts/report_firmware_metrics.py --output build/unified-runtime-metrics.json
```

Use each existing host test's repository command or add a small checked-in test runner; do not assume every C++ test shares the same source list.

- [ ] **Step 2: Full-flash the release candidate**

```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
./scripts/flash_full_assistant_stack.sh "$PORT"
idf.py -p "$PORT" monitor
```

Record boot logs, reset reason, partition label, binary version, and service versions.

- [ ] **Step 3: Run the application regression matrix**

Verify:

- Launcher boots and Home behavior is unchanged;
- Clock selector, Alarm, Watch Face, and Stopwatch work;
- Assistant activation, wake, conversation, text, emotion, playback, retry, and return work;
- Codex BLE status and Wi-Fi fallback work;
- Badge normal view and configuration AP work;
- IMU, FFT, Setup, Boot SFX, display brightness, buttons, and power behavior work;
- BLE remains connected while Assistant uses Wi-Fi;
- Assistant exit leaves BLE Codex monitoring functional.

- [ ] **Step 4: Check capacity and lifecycle release gates**

Reject the release candidate if any of these are true:

- app binary exceeds 6,684,672 bytes;
- Assistant inactive internal-heap cost exceeds 20 KiB over the Launcher baseline;
- paired post-exit heap loss exceeds 4 KiB;
- 100-cycle loss exceeds 8 KiB;
- any normal stop exceeds 2 seconds;
- any Assistant protocol/audio/activation task remains after stopped acknowledgement.

- [ ] **Step 5: Finalize documentation and commit**

Update the validation report with exact measured values, the tested device, firmware SHA, and known limitations. Update README flash instructions.

```bash
git add docs/superpowers/reports/unified-runtime-validation.md README.md
git commit -m "Validate unified Assistant firmware"
git status --short
```

Expected final status: clean except for explicitly pre-existing user-owned files such as `docs/superpowers/plans/2026-07-12-firmware-low-risk-size-reduction.md`.

## Completion Definition

The work is complete only when all of the following are true:

- there is exactly one built `app_main()` and one application image;
- Assistant opens and closes without resetting the ESP32-S3;
- the compatibility matrix passes with the selected Brookesia 0.7 component versions locked;
- StopWatch HAL remains the only board hardware owner;
- Assistant stop is event-acknowledged, finishes within 2 seconds normally, and releases every session resource;
- the 100-cycle leak test passes;
- the one-partition full-flash process succeeds from the old layout;
- the main application retains at least 15% partition headroom;
- all existing applications pass hardware regression;
- the validation report contains exact Flash, RAM, task, timing, and coexistence measurements.
