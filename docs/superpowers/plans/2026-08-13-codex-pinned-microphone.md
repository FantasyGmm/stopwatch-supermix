# Codex Pinned Microphone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a centered-beacon Codex page that starts and stops the existing encrypted watch-to-Mac voice session without holding the upper-right button.

**Architecture:** Extend the existing page swipe model from two pages to three in the order Codex → pinned microphone → TRAE. Add a small firmware-only pinned-microphone state/controller that drives the existing `VoiceInputController` through explicit start/stop methods; the existing Begin/Audio/End protocol and Mac `VoiceSessionService` remain unchanged.

**Tech Stack:** ESP-IDF C++, LVGL, existing encrypted BLE voice protocol, C++ host tests, Python firmware contract tests, unified offline validation.

---

## File Structure

- Create `main/apps/app_codex_status/pinned_microphone_controller.h`: latch and lifecycle policy independent of LVGL.
- Create `main/apps/app_codex_status/pinned_microphone_controller.cpp`: idempotent start/stop, safety clearing, and UI snapshot mapping.
- Create `tests/pinned_microphone_controller_test.cpp`: focused policy tests using a fake voice sink.
- Modify `main/apps/app_codex_status/status_page_swipe_logic.{h,cpp}`: three-page navigation results.
- Modify `tests/status_page_swipe_logic_test.cpp`: exact Codex → pinned microphone → TRAE boundaries.
- Modify `main/apps/app_codex_status/voice_input_controller.{h,cpp}`: explicit idempotent session start/stop and active query shared by hold and pinned modes.
- Modify `tests/voice_input_controller_test.cpp`: sustained session, duplicate start/stop, and no second session coverage.
- Modify `main/apps/app_codex_status/app_codex_status.{h,cpp}`: third page, approved centered-beacon LVGL view, switch touch handling, page indicators, and lifecycle wiring.
- Modify `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`: firmware wiring/geometry contract.
- Modify `scripts/run_unified_offline_validation.sh` and exact validation contracts: register the new host policy test and update declared stage/source lists.
- Modify `README.md`, `mac-codex-agent/README.md`, `VALIDATION.md`, and `AGENTS.md`: user semantics, safety boundaries, and validation evidence.

### Task 1: Three-page navigation

- [ ] Write failing swipe tests for Codex left → pinned microphone, pinned left → TRAE, TRAE right → pinned, pinned right → Codex, and both no-wrap boundaries.
- [ ] Run the focused host compile/run command and verify the new enum values fail to compile.
- [ ] Add `PinnedMicrophone` and three-page transitions without changing gesture thresholds.
- [ ] Run the focused swipe test and verify all transitions pass.

### Task 2: Reusable explicit voice session control

- [ ] Add failing controller tests that call `start_continuous(now)` once, keep producing audio without a held button, ignore duplicate start, call `stop_continuous(now)` once, and ignore duplicate stop.
- [ ] Verify the focused voice host test fails because the explicit API does not exist.
- [ ] Implement `start_continuous`, `stop_continuous`, and `active` as the idempotent public seam; route existing button press/release through the same seam.
- [ ] Preserve cancel/disconnect/error behavior and verify existing push-to-talk tests plus new continuous tests pass.

### Task 3: Pinned microphone policy controller

- [ ] Write a fake-sink host test for enable-when-ready, reject-when-unready, page navigation persistence, disable graceful stop, duplicate operations, disconnect/error/Home clearing, and right-hold suppression while pinned.
- [ ] Verify the test fails because the controller is absent.
- [ ] Implement a controller with one latch and a small snapshot (`Off`, `Starting`, `Active`, `Unavailable`, `Error`) that never owns transport or audio resources.
- [ ] Run the focused controller test and verify all lifecycle cases pass.

### Task 4: Approved third-page LVGL UI

- [ ] Add firmware contract failures for a 1398-pixel page strip, pinned page at x=466, TRAE at x=932, three page indicators, approved Chinese/English labels, centered geometry, mint/violet/cyan colors, and switch callback wiring.
- [ ] Verify the focused Python contract fails against the two-page implementation.
- [ ] Implement the approved centered-beacon page using LVGL objects and fixed geometry derived from the 466 × 466 mockup.
- [ ] Render controller snapshots without rearranging geometry and make the centered switch a generous touch target.
- [ ] Wire swipes and indicators in the order Codex → pinned microphone → TRAE.
- [ ] Run the firmware contract and navigation tests until green.

### Task 5: Runtime lifecycle integration

- [ ] Add host/contract cases proving page changes preserve capture while Home/app close, global suppression, BLE disconnect, connection epoch change, unusable voice link, credential change, and controller error clear the latch and stop/cancel once.
- [ ] Verify at least one new lifecycle assertion fails before integration.
- [ ] Wire the pinned controller before normal UI rendering and after voice readiness polling so UI state follows the actual voice controller.
- [ ] Prevent right-button hold from starting another voice session while pinned while preserving single/double click actions.
- [ ] Run focused voice, gesture, swipe, pinned-controller, and firmware contract tests.

### Task 6: Validation registration and documentation

- [ ] Register `pinned_microphone_controller_test` in the unified host runner and update exact stage/source/trace contracts.
- [ ] Document activation, continuous behavior, safety stop conditions, and the fact that the Mac wire protocol is unchanged.
- [ ] Run `git diff --check`, focused host tests, focused Python contracts, unified `--contract`, and real `--host-tests-only` with explicit `IDF_PATH`.
- [ ] Run a clean ESP-IDF build because the firmware source list and application UI changed; verify capacity gates.
- [ ] Flash the exact connected watch without making a firmware backup, verify hashes and boot, then perform the real enable/speak/navigate/disable acceptance sequence.
