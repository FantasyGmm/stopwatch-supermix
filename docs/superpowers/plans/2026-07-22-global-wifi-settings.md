# Global WiFi Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a global WiFi page to Settings that lists shared credentials, starts the existing Xiaozhi configuration portal on demand, switches saved networks, and releases all Settings-owned WiFi resources on exit.

**Architecture:** Extend the existing shared WiFi policy with a distinct RAII configuration-AP session while preserving the Badge/raw-AP path. Add a small Settings controller that owns STA/config-AP leases and polls immutable snapshots from `WifiManager` and `SsidManager`; keep LVGL rendering in one dedicated worker and do not use the singleton WiFi event callback.

**Tech Stack:** ESP-IDF 5.5, C++17, LVGL/smooth_ui_toolkit, `78/esp-wifi-connect`, existing host C++ tests and Python firmware contract tests.

---

### Task 1: Add Settings Ownership and Config-AP Arbitration

**Files:**
- Modify: `main/runtime/resource_supervisor.h`
- Modify: `main/runtime/resource_supervisor.cpp`
- Modify: `main/runtime/services/shared_wifi_policy.h`
- Modify: `main/runtime/services/shared_wifi_policy.cpp`
- Modify: `main/runtime/services/shared_wifi_service.h`
- Modify: `main/runtime/services/shared_wifi_service.cpp`
- Test: `tests/resource_supervisor_test.cpp`
- Test: `tests/shared_wifi_policy_test.cpp`

- [x] **Step 1: Write failing ownership and config-AP tests**

Add assertions proving `RuntimeOwner::Settings` can own STA, can reconnect only while it owns STA, and can acquire the config portal only when no STA/raw AP owner exists. Extend the fake driver with separate counters:

```cpp
bool startConfigAp() override
{
    ++config_ap_starts;
    return config_ap_start_succeeds;
}

void stopConfigAp() override
{
    ++config_ap_stops;
}
```

Test that `wifi.acquireConfigAp(RuntimeOwner::Settings)` increments only the config counters and that destroying the returned session stops the portal and clears `wifi_ap_active`.

- [x] **Step 2: Run the focused tests and verify failure**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/shared_wifi_policy_test
```

Expected: compilation fails because `RuntimeOwner::Settings`, config-AP driver methods, and config-AP session APIs do not exist.

- [x] **Step 3: Add explicit owner and ownership query**

Change the owner enum and owner count together, and add a read-only ownership query used by reconnect authorization:

```cpp
enum class RuntimeOwner { None, Assistant, Codex, Badge, System, Settings };
static constexpr std::size_t ownerCount = 6;

bool ResourceSupervisor::ownsWifi(RuntimeOwner owner) const
{
    return isValidOwner(owner) && _wifi_owners[ownerIndex(owner)];
}
```

- [x] **Step 4: Add separate config-AP driver and policy methods**

Extend `SharedWifiDriver` with `startConfigAp()` and `stopConfigAp()`. Add `acquireConfigAp`, `releaseConfigAp`, and `reconnectSta(owner)` to `SharedWifiPolicy`. Both AP modes use `ResourceSupervisor::acquireWifiAp`, but generic AP calls `startAp/stopAp` and config AP calls `startConfigAp/stopConfigAp`. `reconnectSta` must return false unless `ownsWifi(owner)` is true and no AP is active.

- [x] **Step 5: Add a distinct RAII session to the service**

Add a move-only `WifiConfigApSession` mirroring `WifiApSession`, with reset routed only to `releaseConfigAp`. Expose:

```cpp
WifiConfigApSession acquireConfigAp(RuntimeOwner owner);
bool reconnectSta(RuntimeOwner owner);
```

- [x] **Step 6: Run focused tests and verify pass**

Run both binaries from Step 2. Expected: both exit 0.

- [x] **Step 7: Commit the arbitration layer**

```bash
git add main/runtime/resource_supervisor.h main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.h main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.h main/runtime/services/shared_wifi_service.cpp tests/resource_supervisor_test.cpp tests/shared_wifi_policy_test.cpp
git commit -m "Add shared WiFi config portal ownership"
```

### Task 2: Make Driver STA and Config-AP Semantics Explicit

**Files:**
- Modify: `main/runtime/services/stopwatch_wifi_driver.cpp`
- Test: `mac-codex-agent/tests/test_service_runtime_firmware_contract.py`

- [x] **Step 1: Add failing firmware contracts**

Assert the StopWatch driver implements `startConfigAp/stopConfigAp`, invokes `StartConfigAp/StopConfigAp` there, and no longer invokes `StartConfigAp` from `startSta` when the saved list is empty.

- [x] **Step 2: Run the contract test and verify failure**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_service_runtime_firmware_contract
```

Expected: the new assertions fail against the current implicit pairing behavior.

- [x] **Step 3: Implement distinct driver methods**

`startSta()` initializes WiFi, always starts Station, and sets performance power save. `startConfigAp()` initializes WiFi, clears the callback slot, calls `StartConfigAp()`, and returns `IsConfigMode()`. `stopConfigAp()` calls `StopConfigAp()`. Keep `startAp/stopAp` behavior unchanged for Badge.

- [x] **Step 4: Run the contract and shared policy tests**

Expected: both pass and Assistant/Codex STA acquisition can no longer start a hidden pairing AP.

- [x] **Step 5: Commit the driver semantics**

```bash
git add main/runtime/services/stopwatch_wifi_driver.cpp mac-codex-agent/tests/test_service_runtime_firmware_contract.py
git commit -m "Separate station and WiFi pairing startup"
```

### Task 3: Add a Testable WiFi Settings Controller

**Files:**
- Create: `main/apps/app_setup/workers/wifi_settings_controller.h`
- Create: `main/apps/app_setup/workers/wifi_settings_controller.cpp`
- Create: `main/apps/app_setup/workers/wifi_settings_model.h`
- Create: `tests/wifi_settings_model_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [x] **Step 1: Write failing state-model tests**

Cover `No saved WiFi`, disconnected, connecting, connected, pairing, busy, and pairing-failed snapshots. The pure model input contains only copied SSIDs and public connection details; no password field is permitted.

- [x] **Step 2: Run the model test and verify failure**

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test
```

Expected: compilation fails because the model does not exist.

- [x] **Step 3: Implement the pure snapshot model**

Define:

```cpp
enum class WifiSettingsState { NoSaved, NotConnected, Connecting, Connected, Pairing, Busy, PairingFailed };

struct WifiSettingsSnapshot {
    WifiSettingsState state = WifiSettingsState::NoSaved;
    std::vector<std::string> saved_ssids;
    std::string ssid;
    std::string ip_address;
    std::string ap_ssid;
    std::string ap_url;
    int rssi = 0;
};
```

Provide equality so the view can skip redundant LVGL rebuilds.

- [x] **Step 4: Implement lifecycle controller**

`WifiSettingsController` owns `WifiStaLease` and `WifiConfigApSession`. Constructor acquires STA only when saved credentials exist. `startPairing()` releases STA before acquiring config AP. `poll()` detects portal exit, releases AP ownership, refreshes saved SSIDs, and reacquires STA only when credentials exist. `selectSaved(index)` calls `SetDefaultSsid`, then either acquires STA or calls `reconnectSta(RuntimeOwner::Settings)`. Destructor calls `stopPairing()` then resets STA.

- [x] **Step 5: Register and run the host test**

Add the model test as a new numbered host step in `run_unified_offline_validation.sh`, adjusting the displayed total. Expected: model test exits 0.

- [x] **Step 6: Commit controller and model**

```bash
git add main/apps/app_setup/workers/wifi_settings_controller.h main/apps/app_setup/workers/wifi_settings_controller.cpp main/apps/app_setup/workers/wifi_settings_model.h tests/wifi_settings_model_test.cpp scripts/run_unified_offline_validation.sh
git commit -m "Add global WiFi settings controller"
```

### Task 4: Add the Round-Screen Settings UI

**Files:**
- Create: `main/apps/app_setup/workers/wifi.cpp`
- Modify: `main/apps/app_setup/workers/workers.h`
- Modify: `main/apps/app_setup/app_setup.cpp`
- Test: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`

- [x] **Step 1: Add failing menu and UI contract assertions**

Assert `AppSetup` contains a `WiFi` item creating `WifiSettingsWorker`; the worker uses `WifiSettingsController`, renders `Add WiFi` and `Stop Pairing`, and never calls `WifiManager::SetEventCallback`.

- [x] **Step 2: Run the contract test and verify failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_unified_runtime_firmware_contract
```

Expected: the menu and worker assertions fail.

- [x] **Step 3: Declare the worker**

Add `WifiSettingsWorker` to `workers.h` with a private `WifiSettingsView`, a controller, and a 500 ms next-poll tick. Its destructor explicitly releases the controller before destroying the view.

- [x] **Step 4: Implement the page**

Create a 466x466 black full-screen panel with stable round-screen bounds: title/status in the top safe area, a 350 px wide scrollable saved-network list in the center, and two compact bottom actions. Saved rows show SSID only; the first row is marked `Default`, connected rows are blue, and tapping any row requests an immediate switch. In pairing state, show AP SSID and URL and replace `Add WiFi` with `Stop Pairing`. Keep all text English and within the 40 px circular edge inset.

- [x] **Step 5: Implement worker updates**

Consume view requests on the LVGL/main thread, call controller operations, poll every 500 ms, and update the view only when the snapshot changes. `Done` sets `_is_done`; destruction releases all leases and returns to the Settings menu.

- [x] **Step 6: Add the Settings menu entry**

Insert a dedicated `WiFi` section between `Time & Date` and `Firmware`, following the existing `_destroy_menu` and worker construction pattern.

- [x] **Step 7: Run contract tests**

Expected: menu/UI contracts pass.

- [x] **Step 8: Commit the Settings UI**

```bash
git add main/apps/app_setup/app_setup.cpp main/apps/app_setup/workers/workers.h main/apps/app_setup/workers/wifi.cpp mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
git commit -m "Add global WiFi Settings page"
```

### Task 5: Remove Password Logging and Add Lifecycle Contracts

**Files:**
- Modify: `managed_components/78__esp-wifi-connect/wifi_configuration_ap.cc`
- Create: `mac-codex-agent/tests/test_global_wifi_settings_firmware_contract.py`

- [x] **Step 1: Add failing security and lifecycle contracts**

Assert the Settings controller contains no event-callback registration, no password fields or password logging, uses `RuntimeOwner::Settings`, releases STA before config AP, and releases config AP before STA reacquisition. Assert the shared component does not log SmartConfig passwords.

- [x] **Step 2: Run the contract and verify password-log failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_global_wifi_settings_firmware_contract
```

Expected: failure on the existing `SmartConfig SSID: %s, Password: %s` log.

- [x] **Step 3: Remove password content from the SmartConfig log**

Log only that credentials were received and, if needed, the SSID. Do not alter the credential save flow.

- [x] **Step 4: Run the focused contracts and host tests**

Expected: all pass.

- [x] **Step 5: Commit security and lifecycle contracts**

```bash
git add managed_components/78__esp-wifi-connect/wifi_configuration_ap.cc mac-codex-agent/tests/test_global_wifi_settings_firmware_contract.py
git commit -m "Protect global WiFi credential handling"
```

### Task 5.5: Close Final Architecture Review Findings

**Files:**
- Modify: `main/runtime/services/shared_wifi_policy.h/.cpp`
- Modify: `main/runtime/services/shared_wifi_service.h/.cpp`
- Modify: `main/runtime/services/stopwatch_wifi_driver.cpp`
- Modify: `main/apps/app_setup/workers/wifi_settings_controller.h/.cpp`
- Modify: `main/apps/app_setup/workers/wifi.cpp`
- Modify: `main/apps/app_setup/workers/workers.h`
- Modify: `main/apps/app_setup/app_setup.cpp`
- Modify: `main/runtime/assistant/assistant_runtime.cpp`
- Create: `main/cmake/esp_wifi_connect_priority_overlay.cmake`
- Test: `tests/shared_wifi_policy_test.cpp`
- Test: `tests/wifi_settings_controller_test.cpp`
- Test: `mac-codex-agent/tests/test_global_wifi_settings_firmware_contract.py`

- [x] **Step 1: Prove upstream RSSI ordering breaks saved-network selection**

Add a failing contract for a generated station overlay and verify the locked
3.1.3 source sorts scan records by RSSI before applying saved-list membership.

- [x] **Step 2: Add an exclusive STA reconfiguration transaction**

Require Settings to be the sole STA owner, stop station callbacks, mutate the
default credential, and restart STA under one shared-policy lock. Reject the
operation before mutation when another owner or AP is active.

- [x] **Step 3: Make saved order drive real connection order**

Generate an exact-anchor `wifi_station.cc` override that ranks candidates by
saved-list index and then RSSI. Clear the attempted SSID after the current
candidate queue is exhausted so the UI can show `Not connected` between scans.

- [x] **Step 4: Remove remaining UI and lifecycle races**

Hide saved rows during pairing, preserve row objects across RSSI-only updates,
and split the worker into lock-free runtime updates and LVGL-locked rendering.
Construct, poll, reconfigure, and release the WiFi controller outside the LVGL
lock before destroying its view.

- [x] **Step 5: Fail Assistant startup cleanly without usable global WiFi**

Use one shared-policy `acquireConfiguredSta` transaction that rejects config-AP
overlap before reading credentials, returns a distinct no-credentials result,
and acquires/starts STA atomically. Fail a pending connection after a bounded
timeout, releasing the Assistant lease in every failure path.

- [x] **Step 6: Run focused tests and an incremental firmware build**

Expected: shared policy/controller host tests and the global WiFi firmware
contract pass; both generated ESP WiFi Connect overrides compile.

- [x] **Step 7: Close independent review findings**

Run the saved-network comparator as host code with weak-default and
strong-non-default fixtures. Verify configured STA acquisition does not inspect
credentials while config AP ownership is active.

### Task 6: Full Offline and Firmware Build Validation

**Files:**
- Modify only if required by verified failures in files already in this plan.

- [ ] **Step 1: Run formatting and whitespace checks**

```bash
git diff --check
```

Expected: no output.

- [ ] **Step 2: Run all Python and host validations**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
./scripts/run_unified_offline_validation.sh
```

Expected: all tests pass. The script also performs an isolated clean firmware build and flash-capacity gates.

- [ ] **Step 3: Inspect firmware size and artifacts**

Record the generated app binary size and partition-capacity result. Do not flash hardware in this task; flashing requires the user to enter download mode first and to be told when flashing has finished so they can exit download mode.

- [ ] **Step 4: Review the final diff against the spec**

Confirm every acceptance criterion has either automated evidence or is explicitly listed as pending hardware validation. Verify unrelated dirty worktree files were not reverted or included in feature commits.

- [ ] **Step 5: Commit any narrowly required validation fix**

Only if Step 2 exposed a defect in this feature, commit the focused fix with its test. Otherwise, do not create an empty validation commit.
