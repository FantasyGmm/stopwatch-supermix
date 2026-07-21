# Task 3 Report: Testable WiFi Settings Controller

## Changed Files

- `main/apps/app_setup/workers/wifi_settings_model.h`
  - Adds the value-only WiFi Settings snapshot, the seven required states, and equality operators.
- `main/apps/app_setup/workers/wifi_settings_controller.h`
  - Declares the LVGL-free controller with injectable shared-WiFi, WiFi-manager, and SSID-manager dependencies.
- `main/apps/app_setup/workers/wifi_settings_controller.cpp`
  - Owns Settings STA/config-AP leases; polls configuration-mode exit; copies only SSIDs; restarts STA after pairing or saved-network selection.
- `tests/wifi_settings_model_test.cpp`
  - Covers `NoSaved`, `NotConnected`, `Connecting`, `Connected`, `Pairing`, `Busy`, and `PairingFailed`, plus snapshot equality changes.
- `scripts/run_unified_offline_validation.sh`
  - Registers the model host test as step 9 of 14. This Task 3 hunk is intentionally left uncommitted because the file already had overlapping user changes to its totals and audio-jitter test.

## Exact Tests

1. Red phase:
   `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test`
   - Failed as expected before implementation: `wifi_settings_model.h` did not exist.
2. `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test && /tmp/wifi_settings_model_test`
   - Passed.
3. `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test && /tmp/resource_supervisor_test`
   - Passed.
4. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/shared_wifi_policy_test && /tmp/shared_wifi_policy_test`
   - Passed.
5. `bash -n scripts/run_unified_offline_validation.sh`
   - Passed.
6. `git diff --check`
   - Passed with no output before and after the isolated commit.

## Self-Review

- The controller has no LVGL dependency and does not call `WifiManager::SetEventCallback`.
- Snapshots retain only copied SSIDs and public connection/AP details; no password field is introduced.
- The constructor obtains Settings STA only when the copied saved-SSID list is nonempty.
- `startPairing()` resets STA before requesting the Settings config-AP session.
- `poll()` observes `IsConfigMode()`, resets the config session before refreshing SSIDs and reacquiring STA, and skips STA acquisition when no credentials exist.
- `selectSaved()` calls `SetDefaultSsid()`, stops config-AP ownership before STA activity, and either acquires STA or calls `reconnectSta(RuntimeOwner::Settings)`.
- The destructor calls `stopPairing()` before resetting its STA lease.
- A full `idf.py build` was started only as extra validation, then deliberately stopped during dependency rebuild at 1160/2265 when scope was narrowed. It is not a passing build result.

## Commit Status

- Committed isolated controller/model/test files in `2c22c0d Add global WiFi settings controller`.
- Not committed: the Task 3 registration in `scripts/run_unified_offline_validation.sh`, because staging that file would also include its pre-existing uncommitted 12-to-13 step-total and audio-jitter test changes.
- The required report is left as an uncommitted Task 3 artifact. All other pre-existing dirty work remains preserved.

## Review Fix Report

- Replaced concrete singleton injection with the narrow `WifiSettingsBackend` interface. The controller now owns `std::unique_ptr<WifiSettingsBackend>`, while the production backend remains private to `wifi_settings_controller.cpp` and owns the Settings `WifiStaLease` and `WifiConfigApSession`.
- Added `STOPWATCH_HOST_TEST` coverage through `tests/wifi_settings_controller_test.cpp`. It asserts no-saved and saved startup, start-pairing release/acquire order, failed pairing STA recovery while retaining `PairingFailed`, portal exit release-before-STA, explicit stop, saved-network selection/reconnect, invalid selection, and destructor cleanup.
- `startPairing()` now reacquires the Settings STA lease after a failed config-AP acquire whenever saved credentials exist. `poll()` still reports `PairingFailed`.
- Expanded snapshot inequality coverage to independently change `ssid`, `ip_address`, `ap_ssid`, `ap_url`, `rssi`, and `saved_ssids`.
- Registered both model and controller host tests in the committed offline-validation script hunk. The staged script patch deliberately excludes the pre-existing audio-jitter registration and its related numbering changes.

### Fix Validation

1. `c++ -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain tests/wifi_settings_controller_test.cpp main/apps/app_setup/workers/wifi_settings_controller.cpp -o /tmp/wifi_settings_controller_test && /tmp/wifi_settings_controller_test` - passed.
2. `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test && /tmp/wifi_settings_model_test` - passed.
3. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test && /tmp/resource_supervisor_test` - passed.
4. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/shared_wifi_policy_test && /tmp/shared_wifi_policy_test` - passed.
5. `bash -n scripts/run_unified_offline_validation.sh` - passed.
6. `git diff --check` and `git diff --cached --check` - passed after the final staged review.
