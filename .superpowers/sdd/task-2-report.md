# Task 2 Report: Forward Real Listening And Speaking Status

## Status

Implemented and committed as `0e29e59 Forward Assistant voice status events`.

## Files

- `main/runtime/assistant/assistant_event_mailbox.h`
- `main/runtime/assistant/assistant_event_mailbox.cpp`
- `main/runtime/assistant/assistant_runtime.cpp`
- `main/apps/app_assistant/app_assistant.cpp`
- `tests/assistant_event_mailbox_test.cpp`
- `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py`

## RED

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py -v
```

Output: exit 127, `no such file or directory: mac-codex-agent/.venv/bin/python`.
The required virtual environment is absent in this worktree. The equivalent focused command run with
`python3` failed as intended: `ListeningStatus` was absent from the mailbox header.

```bash
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test
```

Output: exit 1, expected missing `ListeningStatus`, `SpeakingStatus`, and `AssistantEvent::active`
compile errors.

## GREEN

```bash
python3 -m unittest mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py -v
```

Output: exit 0, `Ran 1 test ... OK`.

```bash
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
  tests/assistant_event_mailbox_test.cpp \
  main/runtime/assistant/assistant_event_mailbox.cpp \
  -o /tmp/assistant_event_mailbox_test && \
  /tmp/assistant_event_mailbox_test
```

Output: exit 0.

```bash
python3 -m unittest mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Output: exit 0, `Ran 8 tests ... OK`; the app event-switch exhaustiveness contract passes.

## Self-review

- Added six bounded persistent slots; text/emotion events remain transient.
- Published only inside the existing current-Xiaozhi/session guard, immediately after the existing trace.
- Preserved wake gating, close suppression, update-notice handling, session cleanup, and callback ordering.
- Added explicit compatibility switch cases with no `default`; no Task 1 reducer integration.

## Concerns

- The required `mac-codex-agent/.venv/bin/python` is absent, so focused Python verification used the
  system `python3`. No hardware/runtime-on-device validation was part of this task.

## Controller Verification

The controller reran both focused modules with the repository's existing absolute venv path:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Result: `Ran 9 tests ... OK` with pristine output.

## Review Fix: Close Suppression And Guarded Callback Contract

Fixed review finding: `isConversationEvent()` now includes `ListeningStatus` and
`SpeakingStatus`, so a post-close status event cannot replace the Closing view.

The source contract now extracts each `ListeningStatusChanged` and
`SpeakingStatusChanged` callback, extracts its successful `callbackIsCurrentXiaozhi(...)`
guard body, and asserts the corresponding `mailbox.publish(...)` is inside that body.
It also asserts both status kinds are part of `isConversationEvent()`.

### RED

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Output: exit 1; the new close-suppression test failed because
`AssistantEventKind::ListeningStatus` was absent from `isConversationEvent()`.
The guarded-callback contract and the other 9 tests passed.

### GREEN

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Output: exit 0, `Ran 10 tests in 4.082s`, `OK`.

```bash
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Imain tests/assistant_event_mailbox_test.cpp main/runtime/assistant/assistant_event_mailbox.cpp -o /tmp/assistant_event_mailbox_test && /tmp/assistant_event_mailbox_test
```

Output: exit 0.

```bash
git diff --check
```

Output: exit 0.

### Commit And Self-review

- Commit: `619d6a7 Suppress Assistant status while closing`.
- Commit scope is only `main/apps/app_assistant/app_assistant.cpp` and
  `mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py`.
- Verified the status events are close-suppressed while the existing guard, mailbox, wake gate,
  update-notice behavior, and event ordering remain unchanged.
