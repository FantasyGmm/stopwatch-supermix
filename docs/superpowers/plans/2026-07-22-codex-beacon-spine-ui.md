# Codex Beacon Spine UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved A · Beacon Spine layout on the 466 × 466 Codex status page with high visual fidelity and no behavior changes.

**Architecture:** Keep the existing `AppCodexStatus` state and rendering lifecycle intact. Encode the approved geometry and palette in the header-only layout contract, validate it with a native C++ test, then make `create_view()` and its small presentation helpers consume those constants.

**Tech Stack:** C++17, LVGL, ESP-IDF, native `assert` layout tests, Ninja/ESP-IDF build.

---

### Task 1: Lock the approved geometry

**Files:**
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`

- [ ] Replace the old symmetric-frame assertions with exact assertions for the pet dock, native pet placement, beacon spine, telemetry column, quota columns, alert divider, and vertical ordering.
- [ ] Run `c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test` and confirm it fails because the Beacon Spine constants do not exist.
- [ ] Add the minimal layout constants and palette tokens required by the approved spec.
- [ ] Re-run the native layout test and confirm it passes.

### Task 2: Build the Beacon Spine view

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Test: `tests/codex_status_ui_layout_test.cpp`

- [ ] Replace the symmetric instrument-frame helpers with a non-interactive rectangular-rule helper and a left-aligned bounded telemetry-label helper.
- [ ] In `create_view()`, construct the pet dock, original pet image, violet-blue spine, right-side MODEL/EFFORT stack, mint quota rows, divider, centered alert channel, and battery using only layout-contract constants.
- [ ] Do not change parsing, state assignment, alert rotation, pet timing, assets, input handling, transport, or screen-off code.
- [ ] Re-run the native layout test.

### Task 3: Verify behavior and firmware

**Files:**
- Verify: `main/apps/app_codex_status/assets/codex_pet_assets.h`
- Verify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] Run the Codex layout, model, and alert-carousel native tests.
- [ ] Run the repository's full relevant native test command if available.
- [ ] Run `ninja -C build-unified` and confirm the ESP-IDF firmware links successfully.
- [ ] Inspect the diff to confirm only the approved UI files and design documents are included.
- [ ] Detect the current serial device; if connected, flash the newly built firmware and report the exact port and result.

### Task 4: Deliver an isolated commit

**Files:**
- Stage only the two Codex UI source/test files and the two Beacon Spine documents.

- [ ] Review `git diff --` for the scoped files.
- [ ] Commit only the scoped files with message `Redesign Codex status as beacon spine`.
- [ ] Report build, test, flash, and physical-visual validation as separate evidence.
