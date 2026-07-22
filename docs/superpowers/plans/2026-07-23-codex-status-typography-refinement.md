# Codex Status Typography Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match the confirmed Scheme A typography hierarchy on the Codex watch page: stronger primary text, a larger state/project block, brighter Chinese detail text, right-aligned larger quota percentages, and a lower battery row.

**Architecture:** Keep the current Beacon Spine widget tree, assets, runtime model, alert carousel, and control behavior unchanged. Express geometry and color changes in the existing layout contract, reuse the bundled `MontserratSemiBold26` font for ASCII primary text, and select the bundled Chinese font at render time only when a project name contains non-ASCII bytes.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.5.4, native `assert` layout tests, Python `unittest` source-contract tests, static HTML/CSS preview.

---

### Task 1: Lock the approved geometry and color contract

**Files:**
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`

- [ ] **Step 1: Write the failing layout assertions**

Update `tests/codex_status_ui_layout_test.cpp` to require:

```cpp
assert(kQuotaPercentX == 351);
assert(kQuotaPercentWidth == 60);
assert(kQuotaPercentX + kQuotaPercentWidth == kQuotaX + kQuotaWidth);
assert(kAlertHeadingY == 315);
assert(kAlertProjectY == 346);
assert(kAlertDetailY == 382);
assert(kBatteryY == 438);
assert(kSecondaryText == 0xB8C2CC);
```

Keep the existing ordering, screen-bound, pet/spine clearance, telemetry-right-edge, and quota-track assertions. Do not relax a prior constraint just to make the new coordinates pass.

- [ ] **Step 2: Run the layout test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test
```

Expected: compilation or an assertion fails because the production constants still carry the previous values.

- [ ] **Step 3: Apply only the approved layout values**

In `main/apps/app_codex_status/codex_status_ui_layout.h`:

```cpp
constexpr uint32_t kSecondaryText = 0xB8C2CC;

constexpr int kQuotaPercentX = 351;
constexpr int kQuotaPercentWidth = 60;
constexpr int kAlertHeadingY = 315;
constexpr int kAlertProjectY = 346;
constexpr int kAlertDetailY = 382;
constexpr int kBatteryY = 438;
```

Leave the pure-black background, pet, spine, telemetry, quota tracks, divider, logo, and connection-status coordinates unchanged.

- [ ] **Step 4: Run the layout test and verify GREEN**

Repeat the command from Step 2.

Expected: exit code 0 with no output.

- [ ] **Step 5: Commit the contract change**

```bash
git add -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  tests/codex_status_ui_layout_test.cpp
git commit -m "Lock Codex typography layout"
```

Expected: only these two files are included; unrelated working-tree changes remain unstaged.

### Task 2: Implement the stronger typography and safe project-font fallback

**Files:**
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`

- [ ] **Step 1: Replace stale font assertions with the new source contract**

Update `mac-codex-agent/tests/test_firmware_font_contract.py` so it requires all of the following:

```python
self.assertIn("bool is_ascii_text(const char* text)", app)
self.assertIn("is_ascii_text(alert->project.c_str())", app)
self.assertIn("? &MontserratSemiBold26", app)
self.assertIn(": &font_puhui_14_1", app)
self.assertIn("lv_obj_set_style_text_font(_alert_detail, &font_puhui_14_1", app)
self.assertIn("kSecondaryText", app)
self.assertIn("constexpr int kQuotaPercentX = 351", layout)
self.assertIn("constexpr int kQuotaPercentWidth = 60", layout)
self.assertGreaterEqual(app.count("&MontserratSemiBold26"), 7)
```

Also update the brand, model, effort, percentage, heading, and project construction expectations from the old Montserrat 18/24 fonts to `MontserratSemiBold26`. Remove the obsolete assertion that `_alert_project` is always assigned `font_puhui_14_1`; retain the font-file glyph checks for Chinese detail text.

- [ ] **Step 2: Run the source contract and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py
```

Expected: failures identify the old fonts, unconditional project font, and old percentage geometry.

- [ ] **Step 3: Add the byte-safe ASCII detector**

In the anonymous namespace of `main/apps/app_codex_status/app_codex_status.cpp`, add:

```cpp
bool is_ascii_text(const char* text)
{
    if (text == nullptr) {
        return true;
    }
    const auto* cursor = reinterpret_cast<const unsigned char*>(text);
    while (*cursor != '\0') {
        if (*cursor >= 0x80) {
            return false;
        }
        ++cursor;
    }
    return true;
}
```

This treats empty/placeholder ASCII text as safe for `MontserratSemiBold26` and sends any UTF-8 project name to `font_puhui_14_1` without decoding or truncating it.

- [ ] **Step 4: Apply the confirmed font hierarchy**

In `app_codex_status.cpp`:

- use `MontserratSemiBold26` for the Codex brand, model value, effort value, both quota percentages, alert heading, and the project label's default font;
- keep connection state, `MODEL`/`EFFORT`, and `5H`/`7D` on their existing fonts;
- keep alert detail on `font_puhui_14_1`, changing only its color to `kSecondaryText`;
- keep state-dependent heading colors unchanged;
- keep project color `kBeaconViolet` unchanged;
- position quota percentage labels at `kQuotaPercentX` with `kQuotaPercentWidth`, preserving right alignment and the fixed right edge at x=411;
- use `y - 7` for the 26 px percentage font so it remains vertically centered against the existing 8 px track.

Remove the unconditional line that assigns `font_puhui_14_1` to `_alert_project`.

- [ ] **Step 5: Select the project font every time alert content changes**

In `render_alert()`, reset the no-alert placeholder to the semibold ASCII font:

```cpp
lv_obj_set_style_text_font(_alert_project, &MontserratSemiBold26, 0);
```

For a real alert, immediately after setting the project text, select the font dynamically:

```cpp
lv_obj_set_style_text_font(
    _alert_project,
    is_ascii_text(alert->project.c_str())
        ? &MontserratSemiBold26
        : &font_puhui_14_1,
    0
);
```

This assignment must run on every carousel render so switching between Chinese and ASCII project names cannot leave the previous font behind.

- [ ] **Step 6: Run focused tests and verify GREEN**

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

- [ ] **Step 7: Commit the implementation**

```bash
git add -- \
  main/apps/app_codex_status/app_codex_status.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "Strengthen Codex status typography"
```

Expected: only the implementation and its source contract are committed.

### Task 3: Synchronize and visually verify the approved preview

**Files:**
- Modify: `/Users/bytedance/.codex/visualizations/2026/07/12/019f5555-dd8e-7760-bd37-1e3d164069bb/index.html`
- Verify: `docs/superpowers/specs/2026-07-22-codex-beacon-spine-ui-design.md`

- [ ] **Step 1: Update the preview without changing its content or assets**

In the preview CSS, keep the pure-black watch background and existing Codex logo/pet sources. Apply these production-equivalent values:

- brand, model value, effort value, quota percentage, alert heading, and ASCII project: 26 px semibold Montserrat;
- quota percentage: global `x=351`, width `60`, right aligned;
- alert heading/project/detail global y positions: `315 / 346 / 382`;
- detail: 14 px Chinese font stack and `#B8C2CC`;
- battery: global `y=438`.

Do not add a pet frame, hostname row, glow, gradient, shadow, card, or new visual element.

- [ ] **Step 2: Reload and inspect the preview**

Reload `http://127.0.0.1:60257/` and verify:

- `PENDING` or `RUNNING`, `StopWatch`, and the Chinese detail form a visibly stronger hierarchy;
- the project line remains centered and does not collide with the detail line;
- `72%` and `64%` appear to the right of their tracks and share the same right edge;
- the battery row is lower but remains fully visible inside the 466 px circle;
- the background is pure black and the pet remains unframed, enlarged, and centered with the telemetry group;
- the Codex logo and animated pet still use the original assets.

Expected: the browser mockup matches the confirmed Scheme A specification. This preview file is outside the repository and must not be staged.

### Task 4: Regression, build, and hardware validation

**Files:**
- Verify all files changed in Tasks 1-2
- Do not stage or modify unrelated working-tree files

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

Expected: all focused tests pass with no behavior changes.

- [ ] **Step 2: Run the broader Mac Agent suite**

```bash
PYTHONPATH=mac-codex-agent mac-codex-agent/.venv/bin/python -m unittest \
  discover -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all tests pass. If an unrelated dirty-worktree test fails, record it separately and do not alter unrelated code to hide it.

- [ ] **Step 3: Build the unified firmware and enforce capacity**

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
./scripts/build_unified_candidate.sh
```

Expected: exit code 0, `capacity_gate_passed` is `true`, and the application image stays below the configured app partition size.

- [ ] **Step 4: Review the exact change set**

```bash
git diff --check HEAD~2..HEAD -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py

git diff --stat HEAD~2..HEAD
git status --short
```

Expected: no whitespace errors; the two implementation commits contain only the four approved repository files; pre-existing user changes remain untouched.

- [ ] **Step 5: Flash only the verified unified artifact**

First confirm `/dev/cu.usbmodem101` is still the intended connected device and that the existing 16 MiB recovery backup remains available. Then run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
idf.py -B build-unified -p /dev/cu.usbmodem101 flash
```

Expected: all partitions verify successfully and the device hard-resets. Do not flash a stale build directory if Task 4 Step 3 failed.

- [ ] **Step 6: Perform the physical-screen acceptance check**

On the watch, verify both an ASCII project such as `StopWatch` and, if available in live data, a non-ASCII project name:

- primary English text is visibly bolder and matches the design hierarchy;
- Chinese detail and non-ASCII project text show complete glyphs without tofu boxes;
- quota percentages remain right of the tracks;
- battery is lower but not clipped by the round panel;
- logo and pet assets/animation are unchanged;
- alert carousel, state colors, BLE/Wi-Fi updates, edge controls, and screen-off behavior still work.

Expected: firmware/build success is reported separately from this on-device visual and behavioral acceptance result.
