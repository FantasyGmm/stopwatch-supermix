# Trae Robot Blink Cadence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Trae robot blink noticeable with a 1300 ms idle interval and 320 ms action frame.

**Architecture:** Keep the existing `TraeStatusPage` timer state machine unchanged. Lock the approved cadence in the layout constants and host test, then validate, build, and flash the existing unified image.

**Tech Stack:** C++17, LVGL timers, ESP-IDF 5.5.4, host assertions.

---

## Task 1: Lock the approved cadence with a failing host test

**Files:**
- Modify: `tests/trae_status_ui_layout_test.cpp`
- Test: `tests/trae_status_ui_layout_test.cpp`

- [ ] Add exact assertions that `kRobotIdleDurationMs == 1300` and `kRobotActionDurationMs == 320` beside the existing robot timer assertions.
- [ ] Compile and run the host test:

  ```bash
  mkdir -p build-host-tests
  c++ -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_status_ui_layout_test.cpp \
    -o build-host-tests/trae_status_ui_layout_test
  build-host-tests/trae_status_ui_layout_test
  ```

  Expected: fail at the new 1300 ms assertion because production still uses the old cadence.

## Task 2: Apply the minimal timer-only change

**Files:**
- Modify: `main/apps/app_codex_status/trae_status_ui_layout.h`
- Test: `tests/trae_status_ui_layout_test.cpp`
- Test: `mac-codex-agent/tests/test_trae_status_firmware_contract.py`

- [ ] Change `kRobotIdleDurationMs` from 2200 to 1300.
- [ ] Change `kRobotActionDurationMs` from 180 to 320.
- [ ] Rebuild and run the host test; expected result: pass.
- [ ] Run the focused firmware contract test:

  ```bash
  mac-codex-agent/.venv/bin/python -m unittest discover \
    -s mac-codex-agent/tests \
    -p 'test_trae_status_firmware_contract.py'
  ```

  Expected: pass with the existing timer state machine and activation contract intact.
- [ ] Review the diff to confirm there are no layout, asset, data, or state-machine changes.
- [ ] Commit the focused implementation:

  ```bash
  git add tests/trae_status_ui_layout_test.cpp \
    main/apps/app_codex_status/trae_status_ui_layout.h
  git commit -m "fix: make Trae robot blink more visible"
  ```

## Task 3: Validate the unified firmware

**Files:**
- Verify: `scripts/run_unified_offline_validation.sh`
- Verify: `build-unified/StopWatch-UserDemo.bin`
- Verify: `build-unified/generated_assets.bin`

- [ ] Run the repository's complete offline validation:

  ```bash
  ./scripts/run_unified_offline_validation.sh
  ```

  Expected: all host/agent/firmware contract checks pass and the ESP-IDF unified image builds successfully.
- [ ] Confirm the built app reports the current implementation commit and remains within the partition capacity gate.

## Task 4: Direct-flash and verify boot behavior

**Files:**
- Use: `build-unified/bootloader/bootloader.bin`
- Use: `build-unified/partition_table/partition-table.bin`
- Use: `build-unified/StopWatch-UserDemo.bin`
- Use: `build-unified/generated_assets.bin`
- Produce: `hardware-evidence/latest-main-<commit>-boot.log`
- Produce: `hardware-evidence/latest-main-<commit>-boot.json`

- [ ] Resolve exactly one `/dev/cu.usbmodem*` target and stop the Mac Agent if it owns the serial port.
- [ ] Direct-flash the bootloader, partition table, app, and assets to their existing offsets. Do not make a flash backup, per the user's standing instruction.
- [ ] Run `scripts/validate_watch_boot.py` for 45 seconds and require a successful Launcher boot.
- [ ] Restart the Mac Agent and confirm it reconnects over BLE.
- [ ] Report firmware/build/boot evidence separately from visual acceptance; the user confirms the final blink cadence on the physical watch.
