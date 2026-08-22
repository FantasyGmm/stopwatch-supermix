# Codex Dual Status and Task Stream Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved dual-channel Codex status, card-free three-task center stream, and visible upward-entry handle.

**Architecture:** Keep transport, task ordering, selection, and detail routing unchanged. Add pure presentation reducers for status channels and stream slots, render three reusable LVGL row groups from the existing deck snapshot, and pass bounded vertical drag offset from the existing gesture owner into the stream layer.

**Tech Stack:** C++17, LVGL 9, existing host-test shell harness, ESP-IDF 5.5.4

**Spec:** `docs/superpowers/specs/2026-08-22-codex-dual-status-task-stream-design.md`

## Global Constraints

- Preserve the 466 × 466 circular viewport and pure-black background.
- No cards, task-row backgrounds, gradients, or shadows.
- Preserve pending-before-running order, route selection, detail crypto, PTT routing, haptics, and page/gesture ownership.
- The deck handle is full-opacity and visible during the bottom-center upward pull.
- CJK task titles use existing native 16 px/14 px raster assets with transform
  scale `256`; no additional full CJK font asset is added.
- Do not stage or modify `.superpowers/`.

---

### Task 1: Dual-channel Codex status

**Files:**
- Modify: `tests/codex_status_priority_test.cpp`
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `main/apps/app_codex_status/codex_status_priority.h`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

**Interfaces:**
- Consumes: base transport text/color/error and `VoiceInputUiState`.
- Produces: `resolve_codex_status_channels(...) -> CodexStatusChannels` plus two dot/label widget pairs.

- [ ] **Step 1: Write failing status reducer tests**

  Replace the single-priority assertions with literal expectations for live,
  connecting, disconnected, transport error, voice ready, starting, active,
  unavailable, and voice error. Assert that connection and voice remain
  independently visible.

- [ ] **Step 2: Run the status reducer test and verify RED**

  Run:
  `c++ -std=c++17 -Wall -Wextra -Werror -I. tests/codex_status_priority_test.cpp -o /tmp/codex-status-priority-test && /tmp/codex-status-priority-test`

  Expected: compilation fails because `resolve_codex_status_channels` and the
  channel presentation types do not exist.

- [ ] **Step 3: Write failing home-layout assertions**

  Assert exact dot/label geometry, 6 px dots, non-overlap between groups, and
  placement below the existing Codex mark within the round safe chord.

- [ ] **Step 4: Run the UI layout test and verify RED**

  Run:
  `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex-status-ui-test && /tmp/codex-status-ui-test`

  Expected: compilation fails because the dual-channel layout constants do not
  exist.

- [ ] **Step 5: Implement the reducer and LVGL widgets**

  Add filled/hollow dot semantics and independent Chinese labels. Replace
  `_status_label` with connection and voice dot/label pointers, update both in
  `render_status()`, and clear them in `destroy_view()`.

- [ ] **Step 6: Run both focused status tests and verify GREEN**

  Re-run the two commands from steps 2 and 4. Expected: both exit 0.

### Task 2: Three-row center task stream and pull handle

**Files:**
- Modify: `tests/codex_task_focus_layout_test.cpp`
- Modify: `main/apps/app_codex_status/codex_task_focus_layout.h`
- Modify: `main/apps/app_codex_status/codex_task_focus_gesture.h`
- Modify: `main/apps/app_codex_status/codex_task_focus_view.h`
- Modify: `main/apps/app_codex_status/codex_task_focus_view.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

**Interfaces:**
- Consumes: `CodexTaskFocusDeck::tasks()`, `focused_index()`, task state/time, and total vertical touch delta.
- Produces: `make_stream_slots(count, focused_index)`, `task_stream_drag_offset(total_dy, has_previous, has_next, limit)`, `CodexTaskFocusView::set_deck_scroll_offset(int)`.

- [ ] **Step 1: Write failing stream-slot and drag tests**

  Add literal assertions for first/middle/last focus neighbor indices, fixed
  round-safe row geometry, full/reduced opacity, bounded drag at both ends,
  native CJK scale `256`, and the deck handle at `(209, 12, 48, 4)`.

- [ ] **Step 2: Run the task-focus layout test and verify RED**

  Run:
  `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/codex_task_focus_layout_test.cpp -o /tmp/codex-task-focus-layout-test && /tmp/codex-task-focus-layout-test`

  Expected: compilation fails because stream slots and deck-handle constants do
  not exist.

- [ ] **Step 3: Implement pure slot and drag helpers**

  Add three fixed slot roles, neighbor selection without wrapping, circular
  safety helpers, and resistance that divides outward boundary movement by four
  while clamping all offsets to 96 px.

- [ ] **Step 4: Render the stream and deck handle**

  Replace the single project/title/status block with three reusable text rows,
  two separators, centered header/count, focus rule, semantic status dots,
  times, and the existing right-side position rail. Use `CodexTaskTitle21`
  generated from Hiragino Sans GB W6 without transform scaling for focused CJK
  titles, `font_puhui_14_1` without transform scaling for neighboring CJK
  titles, `MontserratSemiBold26` for focused Latin titles, and native
  Montserrat 16 for neighboring Latin titles.
  Keep the focused project and task title in separate vertical boxes with a
  visible 2 px gap. Render the right position nodes as a compact group using
  4 px inactive dots, an 8 × 18 px focused capsule, and fixed 6 px gaps rather
  than distributing them across the full rail height. Match the approved
  option-A colors exactly: neutral `#394452` inactive nodes, `#829DFF` focus,
  and the locked text/handle/status palette from the design spec. Build each
  separator from 18% left fade, 64% solid center, and 18% right fade segments.
  Add the deck-layer handle and keep the detail handle.

- [ ] **Step 5: Connect finger-follow motion**

  While the existing deck gesture is vertically axis-locked, compute bounded
  offset using whether previous/next neighbors exist and move only the stream
  layer. Reset offset on release, cancellation, rerender, close, and lifecycle
  cleanup.

  Give the visible deck handle a centered 100 × 48 px invisible touch target.
  Only a downward gesture that starts in that target may move and dismiss the
  whole deck; task-content drags remain browse-only at every list boundary.
  Require release on the physical bottom row (`y = 465`) before dismissing;
  any earlier release animates back to the fully open position.

- [ ] **Step 6: Run the task-focus layout and firmware contract tests**

  Run the C++ command from step 2, then:
  `mac-codex-agent/.venv/bin/python -m unittest tests.test_codex_task_focus_firmware_contract`

  Expected: both exit 0.

- [ ] **Step 7: Preserve voice across Running-to-Completed detail races**

  Let the Agent voice submitter snapshot a currently registered Completed
  route so a still-open detail can start the next turn. Keep the existing
  route/thread revalidation before submission and add a focused regression
  proving missing, collided, and changed routes remain rejected.

### Task 3: Focused regression and firmware build

**Files:**
- Verify only; no new production interfaces.

- [ ] **Step 1: Run focused host tests**

  Run the three C++ test commands from Tasks 1 and 2 plus
  `mac-codex-agent/.venv/bin/python -m unittest tests.test_codex_task_focus_firmware_contract`.

- [ ] **Step 2: Run source hygiene checks**

  Run `git diff --check`, inspect `git diff --stat`, and inspect the exact diff.
  Confirm `.superpowers/` is untracked and unstaged.

- [ ] **Step 3: Build the current firmware candidate**

  Source the repository-pinned ESP-IDF 5.5.4 environment using the project’s
  existing build command, then build the current candidate without erasing NVS
  or creating a firmware backup. Expected: build exits 0 and capacity gates
  accept the bootloader, partition table, application, and assets.

- [ ] **Step 4: Record the visual gate honestly**

  Report host/build evidence separately from physical appearance. Flash only
  after the user requests it or the existing task scope already authorizes it;
  after flashing, compare home status, first/middle/last stream states,
  disconnected state, upward-entry handle, and detail state on the AMOLED.

## Plan self-review

- Spec coverage: each approved visual state and the handle correction maps to a
  production task and a focused test.
- Placeholder scan: every implementation step is concrete and specified.
- Type consistency: reducer, stream-slot, drag-offset, and view method names are
  identical between interfaces and task steps.
- Scope check: secure transport, Agent packaging, task protocol, and detail
  cryptography are unchanged; only the task voice submitter's same-route
  Completed snapshot policy is extended.
