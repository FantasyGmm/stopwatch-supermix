# TRAE Scheme C Physical Fidelity Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the TRAE watch page render the approved Scheme C faithfully on the physical 466 × 466 AMOLED and populate its task title, model, and Effort from the current Trae runtime format.

**Architecture:** Keep the existing `TraeStatusPage` hierarchy and Scheme C anchors. Correct the firmware typography at the source with exact-size fonts and make the single-task metadata row flow from the title's actual rendered height, matching the browser layout instead of a hard-coded two-line sample. Extend the existing Agent collector with bounded, task-associated parsing of the current Trae log evidence; do not add a second collector or protocol.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.5.4, Python 3, unittest, SQLite read-only state inspection.

**Spec:** `docs/superpowers/designs/2026-08-30-trae-status-c/design-contract.md`

## Global Constraints

- Scheme C remains the sole visual source of truth; do not redesign its hierarchy.
- Target canvas is 466 × 466 and the circular safe area must remain unclipped.
- Keep spine `x=129, y=116, h=246, w=2`, status console `x=161, y=78, w=247`, page dots `y=416`, and battery `y=438`.
- Keep the confirmed one-step font increase where it fits; expand the left rail to preserve full `PENDING` and `RUNNING` labels.
- Every rendered digit, ASCII task title, separator, model, and Effort character must exist in its selected firmware font.
- Agent parsing must remain bounded and privacy-preserving and must associate model evidence with the matching task/session.
- Preserve unrelated dirty worktree changes and stage only files from this correction.
- Normal flashing must not back up firmware or erase NVS, pairing, credentials, or storage.

## Physical Gate Failure Addendum — 2026-08-30

The first flashed correction restored missing glyphs and real task metadata, but failed visual acceptance. The same-state physical photo showed a one-line title (`Trae task`) while firmware kept the metadata row at the two-line sample's fixed `y=261`. The approved browser implementation is flow-based: project, title, and metadata are separated by fixed margins, so a one-line title pulls metadata upward. The firmware also rendered Latin project/title/model text through scaled or undersized fonts rather than exact 16/28/14 px faces. These are source-level mismatches, not AMOLED variability.

Before another flash:

- lock the Scheme C single-task anchors to the measured browser boxes (`label y=131`, `project y=173`, `title y=201`);
- derive single-task metadata `y` from the title's actual visual height plus the approved gap;
- use full-ASCII Montserrat Bold 16, Bold 28, and native Montserrat 14 for Latin project/title/model text;
- preserve `x=161`, `w=247`, the spine, rail, and footer;
- validate both a one-line title and a wrapped two-line title before building.

---

### Task 1: Lock the physical typography and rail contract

**Files:**
- Modify: `tests/trae_status_ui_layout_test.cpp`
- Modify: `tests/test_trae_status_page_c_firmware_contract.py`
- Modify: `main/apps/app_codex_status/trae_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/trae_status_page.cpp`
- Create: `main/assets/fonts/MontserratBold18Trae.c`
- Create: `main/assets/fonts/MontserratBold12Trae.c`
- Create: `main/assets/fonts/MontserratBold24Digits.c`
- Modify: `main/assets/assets.h`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `TraeStatusPage::render` and Scheme C geometry constants.
- Produces: complete-glyph fonts and rail geometry that cannot clip the approved labels.

- [ ] **Step 1: Write failing firmware contract tests**

  Assert that the TRAE page no longer references `MontserratBold16` or `MontserratBold22`, that the group separator uses a supported glyph, and that the left rail right edge remains before the spine while its width can contain the 12 px labels.

- [ ] **Step 2: Run the focused tests and verify RED**

  Run: `python3 -m unittest tests.test_trae_status_page_c_firmware_contract -v`

  Run: the existing compile/run command for `tests/trae_status_ui_layout_test.cpp`.

  Expected: failures identify the subset fonts, unsupported separator, and 53 px rail.

- [ ] **Step 3: Generate complete, purpose-specific Montserrat fonts**

  Generate a full printable-ASCII semibold 18 px font for English task titles and a `0123456789` bold 24 px font for counts. Register both through the existing asset build path.

- [ ] **Step 4: Apply the minimal layout correction**

  Keep the spine and console fixed. Move the rail left edge leftward and increase its width while preserving its existing right edge; use the new count font, the full ASCII title font, and a supported bullet separator. Remove transform scaling from those two text paths.

- [ ] **Step 5: Run focused tests and verify GREEN**

  Run the two focused commands from Step 2 plus `git diff --check`.

### Task 2: Parse current Trae task metadata

**Files:**
- Modify: `mac-codex-agent/tests/test_trae_tasks.py`
- Modify: `mac-codex-agent/tests/test_trae_adapter.py`
- Modify: `mac-codex-agent/trae_tasks.py`
- Modify: `mac-codex-agent/trae_adapter.py`

**Interfaces:**
- Consumes: current task/session IDs from `TraeLogEvent` and bounded current Trae log lines.
- Produces: task payload fields `title`, `model`, and `effort` through the existing `trae_tasks` transport array.

- [ ] **Step 1: Add failing fixtures from the current Trae log shape**

  Add sanitized fixtures for a `TASK ... status=Created` line, a matching `TimingCostOriginEvent` containing `config_name` and `provider_model_name`, and a matching `reasoning=Some(...)` line. Assert that only evidence carrying the same session/task IDs enriches the task.

- [ ] **Step 2: Run the focused Agent tests and verify RED**

  Run: `python3 -m unittest mac-codex-agent.tests.test_trae_tasks mac-codex-agent.tests.test_trae_adapter -v`

  Expected: the current parser leaves model/Effort empty and cannot recover the current task title source.

- [ ] **Step 3: Implement bounded current-format parsing**

  Extend `TraeLogEvent` with only the metadata needed by the existing tracker. Parse bounded JSON embedded in the timing record, normalize the model display name through the existing catalog, and map the current reasoning/mode evidence to the existing Effort vocabulary. Preserve the old format as a compatibility path and never apply metadata across task/session boundaries.

- [ ] **Step 4: Recover the best available real task title**

  Read the existing current-session metadata/title sources already accessed by `TraeStateCollector`; prefer a non-empty real title and keep `Trae task` only as the final fallback. Do not inspect arbitrary user content or add unbounded database scans.

- [ ] **Step 5: Run focused tests and verify GREEN**

  Run the two focused Agent test modules plus `mac-codex-agent/tests/test_trae_status_firmware_contract.py` and `git diff --check`.

### Task 3: Install, render, build, and physically verify

**Files:**
- Modify: `docs/superpowers/designs/2026-08-30-trae-status-c/design-contract.md`

**Interfaces:**
- Consumes: corrected firmware renderer and corrected Agent payload.
- Produces: installed Agent, capacity-gated firmware, flashed watch, and a physical comparison record.

- [ ] **Step 1: Install and verify the matching Agent**

  Run the repository installer, restart the Agent, verify installed source hashes match the repository, wait for `Agent started`, ready status, and BLE connection, then inspect the emitted TRAE payload for a real title/model/Effort.

- [ ] **Step 2: Build the standard firmware**

  Source ESP-IDF 5.5.4 explicitly, build the normal candidate, and pass the existing capacity gate. Treat Agent tests, firmware build, flash, boot, and physical appearance as separate evidence.

- [ ] **Step 3: Compare a same-state render**

  Compare the approved `tasks=single` and `tasks=multiple` states against equivalent firmware data states, region by region: brand, channels, group label, task card, left rail, and footer. Do not compare a one-task physical state to the multi-task sample.

- [ ] **Step 4: Flash without backup or erase**

  Detect the exact `/dev/cu.usbmodem*` device, verify bootloader/partition/app/assets offsets and hashes, flash the candidate, verify written hashes, and verify a fresh boot over serial.

- [ ] **Step 5: Complete the physical acceptance gate**

  Capture the running AMOLED with the same logical state as the approved Scheme C artifact. Confirm there are no missing-glyph boxes, no clipped rail labels, task metadata is real when available, and the geometry matches the locked prototype. Record the firmware hash, Agent version, logical state, and region-by-region result in the design contract before reporting visual completion.

### Task 4: Correct the failed single-task physical gate

**Files:**
- Modify: `tests/trae_status_ui_layout_test.cpp`
- Modify: `tests/test_trae_status_page_c_firmware_contract.py`
- Modify: `main/apps/app_codex_status/trae_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/trae_status_page.cpp`
- Create: `main/assets/fonts/MontserratBold16Trae.c`
- Create: `main/assets/fonts/MontserratBold28Trae.c`
- Modify: `main/assets/assets.h`
- Modify: `docs/superpowers/designs/2026-08-30-trae-status-c/design-contract.md`

- [ ] **Step 1: Write RED tests for the exact single-task anchors, dynamic metadata flow, and exact-size Latin fonts.**
- [ ] **Step 2: Generate the two complete Latin fonts and replace the scaled/undersized single-task paths.**
- [ ] **Step 3: Make metadata follow the measured title height for one- and two-line cases.**
- [ ] **Step 4: Run focused layout/contract tests and inspect the exact diff.**
- [ ] **Step 5: Build, flash without backup/erase, verify boot, and repeat the same-state physical comparison.**
