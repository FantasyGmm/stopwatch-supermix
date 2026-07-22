# Task 3 Report: Build Assistant Signal Core View

## Implementation

- Replaced the text-only Assistant view with the approved A2 fixed Signal Core layout: status row, native nine-bar waveform, title, bounded conversation region, and hardware hint.
- Added deterministic state frames, metadata-only top-row update text, connectivity telemetry, and the 44 x 22 battery outline.
- Preserved `AssistantViewState`, `setPrimaryText()`, and `setSecondaryText()` compatibility for Task 4.
- Added source contracts for the fixed layout, no-task/no-timer boundary, Waiting Wake state, and compact update notice.

## TDD And Validation

RED: the requested worktree-relative venv command failed because `mac-codex-agent/.venv/bin/python` is absent. The repository venv then ran the new contracts and failed as intended on the old text-only view: missing waveform interface, Waiting Wake copy, and top-row update notice.

GREEN:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_signal_core_ui_contract.py \
  mac-codex-agent/tests/test_firmware_update_notice_contract.py -v
```

Result: `Ran 12 tests ... OK`.

`git diff --check` completed with no output.

## Firmware Build

Not run to completion. The controller was mechanically copying `managed_components` into this worktree (session 5551), so the started `idf.py -B build-unified reconfigure` raced that copy and was terminated before compilation. The controller will run the ESP-IDF build after the dependency copy completes.

## Commit

`Build Assistant signal core view`

## Residual Risk

ESP-IDF compile/link and on-device visual validation remain pending. Task 4 still owns application-loop animation driving, telemetry sampling, and final presentation-state integration.
