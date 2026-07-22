# Task 3 Report: Testable WiFi Settings Controller

## Changed Files

- `main/apps/app_setup/workers/wifi_settings_model.h`
  - Adds the value-only WiFi Settings snapshot, the seven required states, and equality operators.
- `main/apps/app_setup/workers/wifi_settings_controller.h`
  - Declares the LVGL-free controller with a narrow injectable `WifiSettingsBackend`.
- `main/apps/app_setup/workers/wifi_settings_controller.cpp`
  - Owns Settings STA/config-AP leases; polls configuration-mode exit; maps config-AP ownership rejection separately from startup failure; and chooses exactly one STA path after saved-network selection.
- `tests/wifi_settings_model_test.cpp`
  - Covers `NoSaved`, `NotConnected`, `Connecting`, `Connected`, `Pairing`, `Busy`, and `PairingFailed`, plus snapshot equality changes.
- `scripts/run_unified_offline_validation.sh`
  - The existing runner currently registers the model host test as step 9 of 15. It remains outside this fix because its step-total and audio-jitter hunk predate this review fix.

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
- `selectSaved()` records the pre-selection ownership: a held STA reconnects once; pairing or no STA releases config AP if held, acquires STA once, and never reconnects in that path.
- Config-AP ownership rejection reports `Busy`; a driver startup failure rolls back ownership, recovers STA when saved credentials exist, and reports `PairingFailed`.
- The destructor releases config-AP ownership before its STA lease.
- A full `idf.py build` was started only as extra validation, then deliberately stopped during dependency rebuild at 1160/2265 when scope was narrowed. It is not a passing build result.

## Commit Status

- Earlier Task 3 work is in `2c22c0d Add global WiFi settings controller` and `b88d87d Fix WiFi settings controller lifecycle`.
- The runner's step-total and audio-jitter changes remain pre-existing uncommitted work and are excluded from this review-fix commit.
- The report is updated with the isolated review-fix evidence; unrelated dirty work remains preserved.

## Review Fix Report

- Replaced concrete singleton injection with the narrow `WifiSettingsBackend` interface. The controller now owns `std::unique_ptr<WifiSettingsBackend>`, while the production backend remains private to `wifi_settings_controller.cpp` and owns the Settings `WifiStaLease` and `WifiConfigApSession`.
- Added `STOPWATCH_HOST_TEST` coverage through `tests/wifi_settings_controller_test.cpp`. It asserts both config-AP failure outcomes recover STA, reports `Busy` for resource rejection and `PairingFailed` for startup failure, and covers saved selection with held STA, active pairing, and no STA.
- `startPairing()` releases STA before each config-AP attempt, then reacquires STA when saved credentials exist after either failed outcome.
- Expanded snapshot inequality coverage to independently change `ssid`, `ip_address`, `ap_ssid`, `ap_url`, `rssi`, and `saved_ssids`.
- The runner remains unchanged by this fix; its pre-existing audio-jitter registration and related numbering changes are deliberately excluded.

### Fix Validation

1. `c++ -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain tests/wifi_settings_controller_test.cpp main/apps/app_setup/workers/wifi_settings_controller.cpp -o /tmp/wifi_settings_controller_test && /tmp/wifi_settings_controller_test` - passed.
2. `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test && /tmp/wifi_settings_model_test` - passed.
3. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test && /tmp/resource_supervisor_test` - passed.
4. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/shared_wifi_policy_test && /tmp/shared_wifi_policy_test` - passed.
5. `bash -n scripts/run_unified_offline_validation.sh` - passed.
6. `git diff --check` and `git diff --cached --check` - passed after the final staged review.

### Remaining Review Fix Validation

1. `c++ -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain tests/wifi_settings_controller_test.cpp main/apps/app_setup/workers/wifi_settings_controller.cpp -o /tmp/wifi_settings_controller_test && /tmp/wifi_settings_controller_test` - passed (exit 0).
2. `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test && /tmp/wifi_settings_model_test` - passed (exit 0).
3. `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/resource_supervisor_test.cpp main/runtime/resource_supervisor.cpp -o /tmp/resource_supervisor_test && /tmp/resource_supervisor_test` - passed (exit 0).
4. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/shared_wifi_policy_test && /tmp/shared_wifi_policy_test` - passed (exit 0).
5. `bash -n scripts/run_unified_offline_validation.sh && git diff --check` - passed (exit 0).

## Commit-Isolation Fix

- Commit: `85cfc89cb20df0750b78b713c4cdd63f34ed38ea` (`Declare detailed WiFi config AP acquisition`).
- Committed diff: `main/runtime/services/shared_wifi_policy.h | 1 +`; `1 file changed, 1 insertion(+)`.
- The committed diff adds exactly `WifiConfigApAcquireResult acquireConfigApDetailed(RuntimeOwner owner);`.
- `cycleStaForValidation` remains an unstaged working-tree-only declaration in `main/runtime/services/shared_wifi_policy.h`.

### Committed-Snapshot Validation

The clean archive was extracted at `/tmp/stopwatch-task3-clean.1uv71H` from `HEAD=85cfc89cb20df0750b78b713c4cdd63f34ed38ea`.

1. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/stopwatch-task3-clean.1uv71H/shared_wifi_policy_test && /tmp/stopwatch-task3-clean.1uv71H/shared_wifi_policy_test`
   - `shared_wifi_policy_test: PASS (compile and execution exit 0)`.
2. `c++ -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain tests/wifi_settings_controller_test.cpp main/apps/app_setup/workers/wifi_settings_controller.cpp -o /tmp/stopwatch-task3-clean.1uv71H/wifi_settings_controller_test && /tmp/stopwatch-task3-clean.1uv71H/wifi_settings_controller_test`
   - `wifi_settings_controller_test: PASS (compile and execution exit 0)`.
3. `git diff --check HEAD^ HEAD` - passed with no output (exit 0).
4. `git diff --check` - passed with no output (exit 0).
5. `git diff --cached --check` - passed with no output (exit 0).
6. Post-commit `git diff -- main/runtime/services/shared_wifi_policy.h` contains only the pre-existing `cycleStaForValidation` declaration.
