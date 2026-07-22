# Task 4 Report: Integrate Presentation State And Telemetry

## Implementation

- Added `AssistantPresentationModel` ownership to `AppAssistant`; it resets on open and routes
  runtime state, real listening status, and real speaking status events through the reducer.
- `Running` now applies `AssistantPresentationSignal::Running`, which renders `WaitingWake` until
  a real `ListeningStatus` event is active.
- Preserved the close/session guards, retry transition, update notice, BLE close suppression, and
  dual-button return lifecycle. The event switch remains exhaustive and has no `default`.
- Drove the existing view animation from `onRunning()` under a short LVGL lock. Battery and
  charging values are sampled before a separate short LVGL lock no more than once per second.
- Removed the Task 3 `AssistantViewState` compatibility alias and the unused
  `setPrimaryText()` / `setSecondaryText()` view APIs.

## TDD And Validation

RED:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Result: exit 1, `Ran 18 tests`; the new reducer-routing assertion failed and the new telemetry
assertion errored because `updateTelemetry` was absent. The worktree-relative requested venv path
`mac-codex-agent/.venv/bin/python` is absent (exit 127), so the repository's existing absolute
venv was used.

GREEN:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Result: exit 0, `Ran 18 tests in 2.722s`, `OK`.

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/assistant_presentation_model_test.cpp \
  -o /tmp/assistant_presentation_model_test
/tmp/assistant_presentation_model_test
```

Result: both commands exited 0 with no output.

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py -v
```

Result: exit 1, `Ran 34 tests in 14.633s`; 33 passed. The sole failure is the pre-existing
`test_assistant_mooncake_view_preserves_async_lifecycle_behavior` assertion at line 107, which
requires `AssistantViewState::Connecting` and the other Task 3 compatibility-alias names. This
task explicitly removes that alias and the test is outside the permitted write scope, so it was
not changed.

`git diff --check` exited 0. The controller owns `dependencies.lock`; it was neither edited nor
staged. No ESP-IDF firmware build was started, per task instruction; firmware compile/link and
on-device validation remain pending controller verification.
