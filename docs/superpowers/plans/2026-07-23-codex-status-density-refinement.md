# Codex Status Density Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved A · Balanced Descent typography and vertical rhythm on the Codex watch page without changing its divider, data, assets, colors, animations, or behavior.

**Architecture:** Keep `codex_status_ui_layout.h` as the geometry contract and `AppCodexStatus::create_view()` as the sole widget-construction path. Lock the new coordinates and existing divider/battery anchors with native assertions, then replace only the three oversized font assignments and percentage baseline/compression settings. Preserve all runtime state, alert-carousel, Pending-glow, transport, pet, and battery logic.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.5.4, native `assert` tests, Python `unittest`, static HTML/CSS preview.

---

## File Map

- `main/apps/app_codex_status/codex_status_ui_layout.h`: exact lower-half Y coordinates.
- `main/apps/app_codex_status/app_codex_status.cpp`: quota, alert-heading, and project font assignments.
- `tests/codex_status_ui_layout_test.cpp`: native geometry and spacing contract.
- `mac-codex-agent/tests/test_firmware_font_contract.py`: source and font-fit contract.
- `/Users/bytedance/.codex/visualizations/2026/07/12/019f5555-dd8e-7760-bd37-1e3d164069bb/index.html`: approved browser preview; outside Git and never staged.

No new production file, font asset, widget, or state is required.

### Task 1: Lock the fixed divider and refined alert coordinates

**Files:**
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`

- [ ] **Step 1: Write the failing geometry assertions**

In `tests/codex_status_ui_layout_test.cpp`, replace the current alert-position assertions with:

```cpp
assert(kAlertDividerX == 82);
assert(kAlertDividerY == 304);
assert(kAlertDividerWidth == 302);
assert(kAlertHeadingY == 328);
assert(kAlertHeadingWidth == 310);
assert(kAlertProjectY == 357);
assert(kAlertProjectWidth == 340);
assert(kAlertDetailY == 389);
assert(kAlertDetailWidth == 320);
assert(kBatteryY == 438);

assert(kAlertHeadingY - kAlertDividerY == 24);
assert(kAlertProjectY - kAlertHeadingY == 29);
assert(kAlertDetailY - kAlertProjectY == 32);
assert(kBatteryY - kAlertDetailY == 49);
```

Keep the existing ordering assertions:

```cpp
assert(kWeekY < kAlertDividerY);
assert(kAlertDividerY < kAlertHeadingY);
assert(kAlertHeadingY < kAlertProjectY);
assert(kAlertProjectY < kAlertDetailY);
assert(kAlertDetailY < kBatteryY);
```

These assertions make movement of the divider or battery a test failure.

- [ ] **Step 2: Run the native layout test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test
```

Expected: the executable aborts because the production header still contains `315 / 346 / 382`.

- [ ] **Step 3: Apply only the approved Y coordinates**

In `main/apps/app_codex_status/codex_status_ui_layout.h`, replace only these constants:

```cpp
constexpr int kAlertHeadingY = 328;
constexpr int kAlertProjectY = 357;
constexpr int kAlertDetailY = 389;
```

Leave these anchors unchanged:

```cpp
constexpr int kAlertDividerY = 304;
constexpr int kBatteryY = 438;
```

Do not move the logo, status, pet, spine, telemetry, quota rows, quota tracks, percentages, divider, or battery.

- [ ] **Step 4: Run the native layout test and verify GREEN**

Repeat the Step 2 command.

Expected: exit code 0 with no output.

- [ ] **Step 5: Commit the geometry contract**

```bash
git add -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  tests/codex_status_ui_layout_test.cpp
git diff --cached --check
git commit -m "Refine Codex alert spacing"
```

Expected: the commit contains exactly the layout header and native layout test.

### Task 2: Reduce primary lower-half typography without weakening behavior

**Files:**
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] **Step 1: Change the source contract to the approved font hierarchy**

In `test_codex_alert_rows_use_project_font_fallback_for_non_ascii_text`, require the new ASCII project font:

```python
self.assertIn("? &lv_font_montserrat_22", app)
self.assertIn(": &font_puhui_14_1", app)
```

In `test_codex_status_matches_beacon_spine_mockup_and_reuses_assets`, keep the brand, model, and effort checks on `MontserratSemiBold26`, then replace the percentage and alert assertions with:

```python
self.assertGreaterEqual(app.count("&MontserratSemiBold26"), 3)
self.assertIn(
    "lv_obj_set_style_text_font(pct, &lv_font_montserrat_22, 0)", app
)
self.assertIn(
    "lv_obj_set_style_text_letter_space(pct, 0, 0);", app
)
self.assertIn(
    '_panel, "NO ALERTS", kAlertHeadingY, &lv_font_montserrat_20, kEtchedGray',
    app,
)
self.assertIn(
    '_panel, "--", kAlertProjectY, &lv_font_montserrat_22, kBeaconViolet',
    app,
)
self.assertIn(
    "lv_obj_set_style_text_font(_alert_project, &lv_font_montserrat_22, 0)",
    app,
)
self.assertIn("? &lv_font_montserrat_22", app)
self.assertIn("lv_obj_set_pos(pct, kQuotaPercentX, y - 5)", app)
```

Keep the current assertions that require:

```python
self.assertIn(
    "lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0)", app
)
self.assertIn("constexpr int kQuotaPercentX = 351", layout)
self.assertIn("constexpr int kQuotaPercentWidth = 60", layout)
```

Rename `test_quota_percentages_use_single_line_compression_and_fit_bundled_font` to `test_quota_percentages_fit_bundled_22px_font_without_compression`. In that test, read the enabled LVGL 22 px font:

```python
letter_space_px = 0
box_width_px = 60
self.assertIn(
    "lv_obj_set_style_text_letter_space(pct, 0, 0);", quota_row
)

font = (
    ROOT
    / "components"
    / "lvgl"
    / "src"
    / "font"
    / "lv_font_montserrat_22.c"
).read_text(encoding="utf-8")
```

Use these exact expected metrics:

```python
expected_metrics = {
    "--%": ([135, 135, 297], 567, 35),
    "99%": ([217, 217, 297], 731, 47),
    "100%": ([130, 235, 235, 297], 897, 57),
}
```

Keep the exhaustive `0%` through `100%` fit loop and the `LV_LABEL_LONG_MODE_CLIP` assertion.

- [ ] **Step 2: Run the font contract and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py
```

Expected: failures report `MontserratSemiBold26`, `-3` letter spacing, and `y - 7` in the production UI.

- [ ] **Step 3: Apply the smaller quota typography**

In `make_quota_row()`, replace the current percentage font, compression, and baseline with:

```cpp
lv_obj_set_style_text_font(pct, &lv_font_montserrat_22, 0);
lv_obj_set_style_text_letter_space(pct, 0, 0);
lv_obj_set_style_text_color(pct, lv_color_hex(kSignalWhite), 0);
lv_obj_set_width(pct, kQuotaPercentWidth);
lv_label_set_long_mode(pct, LV_LABEL_LONG_MODE_CLIP);
lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
lv_obj_set_pos(pct, kQuotaPercentX, y - 5);
```

Do not change `kQuotaPercentX`, `kQuotaPercentWidth`, the progress tracks, or quota values.

- [ ] **Step 4: Apply the smaller alert typography**

In `create_view()`, construct the alert heading and project with:

```cpp
_activity_title = make_center_label(
    _panel, "NO ALERTS", kAlertHeadingY, &lv_font_montserrat_20, kEtchedGray
);
_alert_project = make_center_label(
    _panel, "--", kAlertProjectY, &lv_font_montserrat_22, kBeaconViolet
);
```

Keep `_alert_detail` on `font_puhui_14_1`.

In `render_alert()`, reset the no-alert placeholder with:

```cpp
lv_obj_set_style_text_font(_alert_project, &lv_font_montserrat_22, 0);
```

Select the project font on every real-alert render with:

```cpp
lv_obj_set_style_text_font(
    _alert_project,
    is_ascii_text(alert->project.c_str())
        ? &lv_font_montserrat_22
        : &font_puhui_14_1,
    0
);
```

Do not change heading text, state colors, project color, detail construction, carousel selection, or Pending-glow synchronization.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test
```

Expected: Python reports `OK`; the native binary exits 0.

- [ ] **Step 6: Commit the typography implementation**

```bash
git add -- \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git diff --cached --check
git commit -m "Reduce Codex lower-half typography"
```

Expected: the commit contains exactly the LVGL view and its source contract.

### Task 3: Synchronize the approved preview

**Files:**
- Modify: `/Users/bytedance/.codex/visualizations/2026/07/12/019f5555-dd8e-7760-bd37-1e3d164069bb/index.html`
- Verify: `docs/superpowers/specs/2026-07-23-codex-status-density-refinement-design.md`

- [ ] **Step 1: Match option A to production font sizes**

Keep the three-option comparison and selected A card, but change the A status size and description from 21 px to the production-equivalent 20 px:

```css
.balanced {
  --pct-size: 22px;
  --pct-top: -5px;
  --heading-size: 20px;
  --heading-y: 328px;
  --project-size: 22px;
  --project-y: 357px;
  --detail-size: 14px;
  --detail-y: 389px;
}
```

Update the A description to:

```html
<p>状态 20px、项目 22px、百分比 22px。提醒组整体下移，保留清晰层级，也更充分使用下半屏。</p>
```

Do not change the divider, battery, logo, pet animation, colors, or other options.

- [ ] **Step 2: Reload and inspect the preview**

Open or reload:

```text
http://127.0.0.1:60257/
```

Verify visually:

- divider remains at `y=304`;
- A heading/project/detail align to `328 / 357 / 389`;
- battery remains at `y=438`;
- `64%` and `72%` are smaller but remain right aligned;
- the alert stack does not collide with the battery;
- the original Codex logo and pet frames are still used.

Expected: A matches the confirmed Balanced Descent layout. This visualization file is outside the repository and must not be staged.

### Task 4: Run regression, build, and hardware gates

**Files:**
- Verify all files changed in Tasks 1–2.
- Do not stage or modify `.superpowers/`, `hardware-evidence/`, or unrelated worktree changes.

- [ ] **Step 1: Run the focused Codex regression suite**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test

mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py

clang++ -std=c++17 -Imain/apps/app_codex_status \
  tests/codex_alert_carousel_test.cpp \
  main/apps/app_codex_status/codex_alert_carousel.cpp \
  -o /tmp/codex_alert_carousel_test && \
  /tmp/codex_alert_carousel_test

c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_pending_glow_logic_test.cpp \
  -o /tmp/codex_pending_glow_logic_test && \
  /tmp/codex_pending_glow_logic_test

clang -c "$HOME/esp/esp-idf-v5.5.4/components/json/cJSON/cJSON.c" \
  -I"$HOME/esp/esp-idf-v5.5.4/components/json/cJSON" \
  -o /tmp/cJSON.o
clang++ -std=c++17 \
  -Imain/apps/app_codex_status \
  -I"$HOME/esp/esp-idf-v5.5.4/components/json/cJSON" \
  tests/codex_status_model_test.cpp \
  main/apps/app_codex_status/codex_status_model.cpp \
  /tmp/cJSON.o \
  -o /tmp/codex_status_model_test && \
  /tmp/codex_status_model_test
```

Expected: every focused test exits 0 and Python reports `OK`.

- [ ] **Step 2: Run the complete Mac Agent suite**

Run:

```bash
PYTHONPATH=mac-codex-agent \
  mac-codex-agent/.venv/bin/python -m unittest \
  discover -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass. Record any unrelated pre-existing failure separately; do not change unrelated code to hide it.

- [ ] **Step 3: Build the unified firmware and enforce capacity**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
./scripts/build_unified_candidate.sh
```

Expected: exit code 0, `capacity_gate_passed` is `true`, and the application remains below the configured capacity limit.

- [ ] **Step 4: Review the exact repository change set**

Run:

```bash
git diff --check HEAD~2..HEAD -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py

git diff --stat HEAD~2..HEAD
git status --short
```

Expected: the two implementation commits contain only the four approved repository files; unrelated untracked files remain untouched.

- [ ] **Step 5: Re-probe hardware before any device write**

Run:

```bash
find /dev -maxdepth 1 -name 'cu.usbmodem*' -print
test -f hardware-evidence/firmware-backups/2026-07-23-before-pending-soft-glow.bin
test -f hardware-evidence/firmware-backups/2026-07-23-before-pending-soft-glow.bin.sha256
shasum -a 256 -c \
  hardware-evidence/firmware-backups/2026-07-23-before-pending-soft-glow.bin.sha256
```

Expected: exactly one usable StopWatch serial port is present and the existing 16 MiB recovery backup passes its SHA-256 check. If the port is absent or ambiguous, stop before flashing and report the verified build artifact.

- [ ] **Step 6: Flash only after explicit device-write confirmation**

After confirming the connected port is the intended StopWatch, run with the actual port returned by Step 5:

```bash
./scripts/flash_unified_candidate.sh \
  --port /dev/cu.usbmodem1101 \
  --backup hardware-evidence/firmware-backups/2026-07-23-before-pending-soft-glow.bin
```

Expected: bootloader, partition table, application, and assets write successfully, verify, and the watch hard-resets. Re-probe the port immediately before execution instead of assuming `/dev/cu.usbmodem1101` is stable.

- [ ] **Step 7: Perform the physical-screen acceptance check**

On the watch, verify:

- the divider did not move;
- Pending/Handled/Running/Stop text is visibly smaller than the prior 26 px version;
- `StopWatch` is visibly smaller but remains stronger than the detail line;
- `64%`, `72%`, `100%`, and `--%` fit without clipping;
- the alert text group sits lower and remains clear of the battery;
- Pending glow, carousel, logo, pet animation, quotas, connection state, and edge controls still behave normally.

Expected: the physical panel matches option A. Treat build and flash success as necessary but not sufficient evidence of visual fidelity.
