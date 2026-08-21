# Codex Agent Full-Info Ledger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Codex Agent focus rail's default selector with the approved all-Agent Full-info Ledger while preserving all existing management and security behavior.

**Architecture:** Extend the existing registry-derived row snapshot with presentation data, add pure geometry/time helpers that host tests can exercise, and render the new ledger as the default LVGL branch. The existing focus rail becomes the management branch, so delete/reset workflows stay isolated from normal switching.

**Tech Stack:** C++17, LVGL, ESP-IDF 5.5.4, existing host-test shell harness.

**Spec:** `docs/superpowers/specs/2026-08-15-codex-agent-full-info-ledger-design.md`

## Global Constraints

- Target viewport is exactly 466 × 466 at 1× LVGL coordinates.
- Support 0–8 authorized Agents with no scrolling in the default selector.
- Eight rows are exactly 44 px high; all visible actions have at least 44 px touch targets.
- Preserve pairing, voice cleanup, BLE routing, credential rotation, delete confirmation, reset hold, and close gestures.
- Do not mutate the registry serialization format or pairing wire protocol.
- Preserve unrelated dirty files and stage only files owned by this task.
- Do not call the UI visually complete before physical AMOLED comparison.

---

### Task 1: Carry truthful identity summary into panel rows

**Files:**
- Modify: `main/apps/app_codex_status/agent_panel_model.h`
- Modify: `main/apps/app_codex_status/agent_panel_model.cpp`
- Test: `tests/agent_panel_model_test.cpp`

**Interfaces:**
- Consumes: `secure_pairing::AgentRecord::{fingerprint,display_name,last_connected_at}`.
- Produces: `AgentPanelRow::{short_id,last_connected_at}` and an unmodified display name.

- [x] **Step 1: Write the failing row-data test**

Set two records to the same name, give them literal timestamps, call
`sync_registry`, and assert that each row keeps the exact name, exposes the
corresponding timestamp, and has a distinct uppercase four-hex `short_id`.

- [x] **Step 2: Run the focused model host test and verify RED**

Run the existing `agent_panel_model_test` compile/run command from
`scripts/run_unified_offline_validation.sh`. Expected failure: the new row
members do not exist and duplicate names are still rewritten.

- [x] **Step 3: Implement the minimal row propagation**

Add:

```cpp
std::array<char, 5> short_id = {};
int64_t last_connected_at = 0;
```

Copy the stored name without suffixing, format bytes 0/1 with `%02X%02X`, and
copy `record.last_connected_at`.

- [x] **Step 4: Re-run the model host test and verify GREEN**

Expected: all existing pairing, decision, and gesture assertions plus the new
row-data assertions pass.

### Task 2: Add pure ledger geometry and relative-time presentation

**Files:**
- Modify: `main/apps/app_codex_status/agent_panel_ui_layout.h`
- Create: `main/apps/app_codex_status/agent_panel_presenter.h`
- Create: `main/apps/app_codex_status/agent_panel_presenter.cpp`
- Modify: `tests/agent_panel_ui_layout_test.cpp`
- Create: `tests/agent_panel_presenter_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

**Interfaces:**
- Produces: `agent_panel_ui::ledger_row_layout(size_t count, size_t index)` returning `Rect`.
- Produces: `agent_panel_presentation::format_last_connected(...)` writing bounded text to a caller buffer.

- [x] **Step 1: Write failing geometry boundary tests**

Use literal expectations: one row is centered and 66 px high; five rows are
66 px high and centered; eight rows span `y=74..426` with 44 px rows; every
row is inside the 466 circle and has positive width.

- [x] **Step 2: Run the layout test and verify RED**

Expected failure: the ledger helper and locked constants are absent.

- [x] **Step 3: Implement the locked geometry**

Use the formula from the spec with an integer square-root helper local to the
header. Return `{0,0,0,0}` for invalid count/index rather than indexing outside
the eight-row registry.

- [x] **Step 4: Write failing time-format boundary tests**

Cover `Connected -> Now`, `Requesting -> Awaiting`, invalid legacy timestamp
`-> Unknown`, 8 minutes, 2 hours, 7 days, and an offline future timestamp
`-> Unknown` rather than a negative duration.

- [x] **Step 5: Run the new presenter test and verify RED**

Expected failure: presenter files/functions do not exist.

- [x] **Step 6: Implement the minimal presenter and register its host stage**

Use fixed output buffers and `snprintf`; perform no allocation and no locale
lookup. Add the exact compile/run stage to the validation script.

- [x] **Step 7: Re-run both focused tests and verify GREEN**

Expected: literal geometry and time strings pass with `-Wall -Wextra -Werror`.

### Task 3: Render the Full-info Ledger and isolate management

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/agent_panel_ui_layout_test.cpp`

**Interfaces:**
- Consumes: `AgentPanelSnapshot`, `ledger_row_layout`, and `format_last_connected`.
- Preserves: existing `AgentPanelDecision` actions and existing focus-rail management view.

- [x] **Step 1: Add failing layout/interaction contract assertions**

Assert the centered Manage target, ledger zone, 44 px minimum, and close hint
do not overlap. The production change that breaks these tests is an out-of-safe
area action or an eight-row overflow.

- [x] **Step 2: Run the layout test and verify RED**

Expected failure: Manage/ledger constants are missing or still describe the
old default rail.

- [x] **Step 3: Implement the default ledger LVGL branch**

Create one transparent row button per snapshot row at the pure helper's exact
rectangle. Add the status dot, name, metadata state, `ID XXXX`, and time label;
use the locked colors and bundled fonts. No scroll container, focus band,
shadow, rounded card, or permanent delete button is created in this branch.

- [x] **Step 4: Implement Manage/DONE routing**

Add `_agent_manage_button`, `_agent_manage_done_button`, and
`_agent_panel_manage_mode`. Manage renders the existing focus rail/reset UI;
Done returns to the ledger. Panel close and view destruction clear manage mode.
Row clicks in ledger keep the existing switch decision; row clicks in manage
do not initiate a switch.

- [x] **Step 5: Store valid Unix time on new selections**

Replace boot-millisecond persistence with synchronized system Unix
milliseconds. If `std::time(nullptr)` is below the valid threshold, preserve
the prior stored value and keep the UI truthful.

- [x] **Step 6: Run the panel model/layout tests and compile the firmware**

Expected: focused tests pass, then an incremental ESP-IDF build succeeds with
the configured 5.5.4 toolchain.

### Task 4: Fidelity verification and handoff

**Files:**
- Create after capture: `docs/superpowers/designs/2026-08-15-agent-selector-full-info-ledger/actual-8-agents.png`
- Create after comparison: `docs/superpowers/designs/2026-08-15-agent-selector-full-info-ledger/diff-8-agents.png`
- Modify: `docs/superpowers/specs/2026-08-15-codex-agent-full-info-ledger-design.md` only to record measured evidence, not to change the design.

**Interfaces:**
- Consumes: locked PNG and flashed physical AMOLED.
- Produces: recorded geometry/type/color/state comparison and any remaining accepted deviation.

- [x] **Step 1: Run source-level verification**

Run focused model, presenter, layout, registry, and pairing host tests plus
`git diff --check`. Inspect the exact diff and confirm unrelated dirty files
are untouched.

- [ ] **Step 2: Flash the connected watch without a firmware backup**

Use capacity-gated artifacts, exact `/dev/cu.usbmodem*` detection, written hash
verification, and a new serial boot. Do not erase NVS or pairing state.

- [ ] **Step 3: Capture the physical state packet**

Capture 8 Agents, 1 Agent, empty, Manage, Switch request, and Switching states
at the same brightness and camera geometry.

- [ ] **Step 4: Compare region by region**

Compare header, first/last safe widths, row heights, fonts, state colors,
close hint, and clipping against `reference-8-agents.png`. Generate an image
diff when the capture can be normalized to 466 × 466.

- [ ] **Step 5: Report evidence without overclaiming**

Report build, flash, boot, and physical fidelity as separate gates. If the
physical packet is unavailable, report the result as compiled/flashed only,
not visually complete.
