# Task 2 Report: Driver STA and Config-AP Semantics

## Changed Files

- `mac-codex-agent/tests/test_service_runtime_firmware_contract.py`
  - Added a production teardown contract asserting that `stopConfigAp()` calls `WifiManager::GetInstance().StopConfigAp()`.
  - Preserved the unrelated pre-existing user hunk for Brookesia service-stop ordering; it is not part of this Task 2 change.
- `main/runtime/services/stopwatch_wifi_driver.cpp`
  - No working-tree change was required. The implementation from `b6150ce` already provides the required distinct STA, Config AP, and Config AP teardown methods.

## Exact Tests

1. `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_service_runtime_firmware_contract`
   - Result in the preserved working tree: `Ran 7 tests ... OK`.
   - Reproducible committed-tree count: 6 Task-2-relevant tests; the seventh
     working-tree test is the pre-existing uncommitted service-stop contract.
2. `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/shared_wifi_policy_test.cpp main/runtime/resource_supervisor.cpp main/runtime/services/shared_wifi_policy.cpp main/runtime/services/shared_wifi_service.cpp -o /tmp/stopwatch-task2-host-<pid>/shared_wifi_policy_test && /tmp/stopwatch-task2-host-<pid>/shared_wifi_policy_test`
   - Result: compile and execution passed with exit status 0.

## Self-Review

- `startSta()` initializes WiFi, always calls `StartStation()`, and sets performance power save; it does not call `StartConfigAp()` or inspect `SsidManager`.
- `startConfigAp()` initializes WiFi, clears the callback, stops Station, calls `StartConfigAp()`, and returns `IsConfigMode()`.
- `stopConfigAp()` calls `WifiManager::GetInstance().StopConfigAp()`.
- Badge `startAp()`/`stopAp()` behavior was not changed.
- The diff is limited to the new teardown contract and this report. No firmware build, live device, or manual UI validation was run for this contract-only task.

## Commit Status

Committed in the isolated Task 2 commit. The commit contains only the new teardown-contract hunk and this report; all unrelated dirty files remain unstaged and preserved.
