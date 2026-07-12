# Codex Status UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restyle the 466 × 466 StopWatch Codex status page as the approved Precision Console while preserving every existing element, asset, animation, and behavior.

**Architecture:** Put palette and geometry in a small dependency-free layout header so round-screen constraints can be checked by a native C++ test. Keep all LVGL construction in `app_codex_status.cpp`; refactor only its presentation helpers and `create_view()`, leaving status parsing, transports, carousel behavior, timers, and the Mac agent untouched.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.5.4, native `c++` assertion tests, existing Codex logo/pet/font assets.

---

## File Map

- Create `main/apps/app_codex_status/codex_status_ui_layout.h`: palette and fixed 466 × 466 geometry; no LVGL dependency.
- Create `tests/codex_status_ui_layout_test.cpp`: native assertions for symmetry, bounds, vertical ordering, and text-safe widths.
- Modify `main/apps/app_codex_status/app_codex_status.cpp`: apply the palette, mirrored instruments, aligned quota rows, divider, and final positions.
- Do not modify `app_codex_status.h`, the status model, alert carousel, transports, Mac agent, image assets, or font assets.

### Task 1: Lock the Approved Geometry with a Native Test

**Files:**
- Create: `tests/codex_status_ui_layout_test.cpp`
- Create: `main/apps/app_codex_status/codex_status_ui_layout.h`

- [ ] **Step 1: Write the failing geometry test**

Create `tests/codex_status_ui_layout_test.cpp`:

```cpp
#include "apps/app_codex_status/codex_status_ui_layout.h"

#include <cassert>

int main()
{
    using namespace codex_status_ui;

    assert(kScreenSize == 466);
    assert(kModelInstrument.x == 54);
    assert(kEffortInstrument.x == 296);
    assert(kModelInstrument.w == kEffortInstrument.w);
    assert(kModelInstrument.h == kEffortInstrument.h);
    assert(kModelInstrument.x + kModelInstrument.w +
               kEffortInstrument.x ==
           kScreenSize);

    assert(kQuotaX >= 42);
    assert(kQuotaX + kQuotaWidth <= kScreenSize - 42);
    assert(kFiveHourY < kWeekY);
    assert(kWeekY < kAlertDividerY);
    assert(kAlertDividerY < kAlertHeadingY);
    assert(kAlertHeadingY < kAlertProjectY);
    assert(kAlertProjectY < kAlertDetailY);
    assert(kAlertDetailY < kBatteryY);

    assert(kAlertHeadingWidth <= 310);
    assert(kAlertProjectWidth <= 340);
    assert(kAlertDetailWidth <= 320);
    assert(kBatteryY < kScreenSize);
}
```

- [ ] **Step 2: Run the test and verify that it fails**

Run:

```bash
c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test
```

Expected: compilation fails because `codex_status_ui_layout.h` does not exist.

- [ ] **Step 3: Add the dependency-free layout contract**

Create `main/apps/app_codex_status/codex_status_ui_layout.h`:

```cpp
#pragma once

#include <cstdint>

namespace codex_status_ui {

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

constexpr int kScreenSize = 466;

constexpr uint32_t kInstrumentBlack = 0x070A0F;
constexpr uint32_t kPanelGraphite   = 0x121823;
constexpr uint32_t kCalibrationBlue = 0x54C7EC;
constexpr uint32_t kSignalWhite     = 0xF2F6F8;
constexpr uint32_t kEtchedGray      = 0x788391;
constexpr uint32_t kAlertVermilion  = 0xFF6248;

constexpr int kLogoX = 168;
constexpr int kLogoY = 22;
constexpr int kLogoScale = 170;
constexpr int kBrandX = 218;
constexpr int kBrandY = 32;
constexpr int kStatusY = 63;
constexpr int kHostY = 84;

constexpr Rect kModelInstrument{54, 112, 116, 92};
constexpr Rect kEffortInstrument{296, 112, 116, 92};
constexpr int kPetCenterX = 233;
constexpr int kPetCenterY = 158;
constexpr int kCalibrationY = 158;

constexpr int kQuotaX = 49;
constexpr int kQuotaWidth = 368;
constexpr int kFiveHourY = 225;
constexpr int kWeekY = 268;

constexpr int kAlertDividerX = 73;
constexpr int kAlertDividerY = 310;
constexpr int kAlertDividerWidth = 320;
constexpr int kAlertDividerGap = 28;
constexpr int kAlertHeadingY = 324;
constexpr int kAlertHeadingWidth = 310;
constexpr int kAlertProjectY = 354;
constexpr int kAlertProjectWidth = 340;
constexpr int kAlertDetailY = 382;
constexpr int kAlertDetailWidth = 320;
constexpr int kBatteryY = 435;

}  // namespace codex_status_ui
```

- [ ] **Step 4: Run the geometry test and verify that it passes**

Run:

```bash
c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test
```

Expected: exit code 0 and no output.

- [ ] **Step 5: Commit the layout contract**

```bash
git add main/apps/app_codex_status/codex_status_ui_layout.h tests/codex_status_ui_layout_test.cpp
git commit -m "test: lock Codex status UI geometry"
```

### Task 2: Build the Precision Console Presentation Helpers

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.cpp:6-140`
- Test: `tests/codex_status_ui_layout_test.cpp`

- [ ] **Step 1: Include the layout contract and replace local presentation colors**

Add the include beside the other Codex headers:

```cpp
#include "codex_status_ui_layout.h"
```

Inside the anonymous namespace, import the approved palette while preserving the existing handled green and behavioral constants:

```cpp
using namespace codex_status_ui;

constexpr uint32_t kCodexBlue = kCalibrationBlue;
constexpr uint32_t kCodexRed = kAlertVermilion;
constexpr uint32_t kHandledGreen = 0x3CB371;
constexpr uint32_t kTextDim = kEtchedGray;
```

- [ ] **Step 2: Replace the arc helper with a mirrored instrument helper**

Replace `make_arc` with:

```cpp
lv_obj_t* make_instrument_arc(lv_obj_t* parent, const Rect& rect, int rotation)
{
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, rect.w, rect.h);
    lv_obj_set_pos(arc, rect.x, rect.y);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 100);
    lv_arc_set_rotation(arc, rotation);
    lv_arc_set_bg_angles(arc, 0, 115);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(kPanelGraphite), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(kCalibrationBlue), LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}
```

The left and right instances use opposite rotations in Task 3. Both use blue in ordinary operation; red remains reserved for actual error and pending states.

- [ ] **Step 3: Generalize the centered value helper to the new instrument width**

Replace `make_arc_value` with:

```cpp
lv_obj_t* make_instrument_value(
    lv_obj_t* parent,
    const Rect& rect,
    int y,
    const char* text,
    const lv_font_t* font,
    uint32_t color
)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, rect.w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, rect.x, y);
    return label;
}
```

- [ ] **Step 4: Replace the quota row geometry and styling**

Replace `make_quota_row` with:

```cpp
QuotaWidgets make_quota_row(lv_obj_t* parent, int y, const char* label_text)
{
    constexpr int label_x = kQuotaX;
    constexpr int label_w = 43;
    constexpr int gap = 10;
    constexpr int pct_w = 66;
    constexpr int pct_x = kQuotaX + kQuotaWidth - pct_w;
    constexpr int bar_x = label_x + label_w + gap;
    constexpr int bar_w = pct_x - gap - bar_x;

    make_label(parent, label_text, label_x, y - 2, &lv_font_montserrat_18, kSignalWhite);

    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, bar_w, 8);
    lv_obj_set_pos(bar, bar_x, y + 8);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kPanelGraphite), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kCalibrationBlue), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_obj_t* pct = lv_label_create(parent);
    lv_label_set_text(pct, "--%");
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pct, lv_color_hex(kSignalWhite), 0);
    lv_obj_set_width(pct, pct_w);
    lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(pct, pct_x, y - 6);

    return {pct, bar};
}
```

- [ ] **Step 5: Add one-pixel line helpers for the calibration line and split divider**

Add after `make_quota_row`:

```cpp
lv_obj_t* make_line(lv_obj_t* parent, int x, int y, int width)
{
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, width, 1);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(kEtchedGray), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return line;
}

void make_split_line(lv_obj_t* parent, int x, int y, int width, int gap)
{
    const int segment = (width - gap) / 2;
    make_line(parent, x, y, segment);
    make_line(parent, x + segment + gap, y, segment);
}
```

- [ ] **Step 6: Run the native geometry test**

```bash
c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test
```

Expected: exit code 0 and no output.

- [ ] **Step 7: Commit the presentation helpers**

```bash
git add main/apps/app_codex_status/app_codex_status.cpp
git commit -m "refactor: add Codex precision console helpers"
```

### Task 3: Apply the Approved 466 × 466 Layout

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.cpp:337-399`
- Test: `tests/codex_status_ui_layout_test.cpp`

- [ ] **Step 1: Restyle the panel and brand block**

In `create_view()`, set the panel background to `kInstrumentBlack`. Position the existing logo with `kLogoX`, `kLogoY`, and `kLogoScale`; position the existing `Codex` label at `kBrandX`, `kBrandY` in `kCalibrationBlue`. Create status and host labels at `kStatusY` and `kHostY` using `kCalibrationBlue`/`kEtchedGray` and set the host to width 320, centered, with `LV_LABEL_LONG_MODE_DOTS`.

Use this complete block after panel padding is configured:

```cpp
lv_obj_set_style_bg_color(_panel, lv_color_hex(kInstrumentBlack), 0);

lv_obj_t* logo = lv_image_create(_panel);
lv_image_set_src(logo, &logo_codex);
lv_image_set_scale(logo, kLogoScale);
lv_obj_set_pos(logo, kLogoX, kLogoY);

make_label(_panel, "Codex", kBrandX, kBrandY, &lv_font_montserrat_24, kCalibrationBlue);

_status_label = make_center_label(_panel, "START", kStatusY, &lv_font_montserrat_14, kEtchedGray);
_host_label = make_center_label(_panel, "", kHostY, &lv_font_montserrat_14, kEtchedGray);
lv_obj_set_width(_host_label, 320);
lv_obj_set_style_text_align(_host_label, LV_TEXT_ALIGN_CENTER, 0);
lv_label_set_long_mode(_host_label, LV_LABEL_LONG_MODE_DOTS);
lv_obj_align(_host_label, LV_ALIGN_TOP_MID, 0, kHostY);
```

- [ ] **Step 2: Build the mirrored instruments without changing image assets**

Replace the existing arc/value/pet block with:

```cpp
make_instrument_arc(_panel, kModelInstrument, 195);
_model_value = make_instrument_value(
    _panel, kModelInstrument, 145, "--", &lv_font_montserrat_14, kSignalWhite
);

make_instrument_arc(_panel, kEffortInstrument, 230);
_effort_value = make_instrument_value(
    _panel, kEffortInstrument, 143, "--", &lv_font_montserrat_18, kCalibrationBlue
);

constexpr int kPetClearance = 8;
const int pet_left = kPetCenterX - CODEX_PET_FRAME_W / 2;
const int pet_right = pet_left + CODEX_PET_FRAME_W;
make_line(_panel, kModelInstrument.x, kCalibrationY,
          pet_left - kPetClearance - kModelInstrument.x);
make_line(_panel, pet_right + kPetClearance, kCalibrationY,
          kEffortInstrument.x + kEffortInstrument.w - pet_right - kPetClearance);

_pet_image = lv_image_create(_panel);
lv_image_set_src(_pet_image, codex_pet_idle_frames[0]);
lv_image_set_antialias(_pet_image, false);
lv_obj_clear_flag(_pet_image, LV_OBJ_FLAG_CLICKABLE);
lv_obj_set_pos(
    _pet_image,
    kPetCenterX - CODEX_PET_FRAME_W / 2,
    kPetCenterY - CODEX_PET_FRAME_H / 2
);
```

Keep the existing pet timer creation block unchanged immediately afterward.

- [ ] **Step 3: Position quotas and the split alert divider**

Create quota rows at `kFiveHourY` and `kWeekY`, assign their returned pointers exactly as today, then add:

```cpp
make_split_line(
    _panel,
    kAlertDividerX,
    kAlertDividerY,
    kAlertDividerWidth,
    kAlertDividerGap
);
```

- [ ] **Step 4: Apply the approved alert widths and colors**

Create the three existing alert labels at `kAlertHeadingY`, `kAlertProjectY`, and `kAlertDetailY`. Preserve the Puhui font assignments for project and detail. Configure each explicitly:

```cpp
const auto configure_alert_label = [](lv_obj_t* label, int y, int width) {
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
};
configure_alert_label(_activity_title, kAlertHeadingY, kAlertHeadingWidth);
configure_alert_label(_alert_project, kAlertProjectY, kAlertProjectWidth);
configure_alert_label(_alert_detail, kAlertDetailY, kAlertDetailWidth);
```

Use `kSignalWhite` for the initial heading, `kCalibrationBlue` for the project, and `kEtchedGray` for detail. Do not change `render_alert()`; it continues to apply pending red, handled green, and empty-state gray.

- [ ] **Step 5: Put the existing battery label on the approved baseline**

Replace bottom alignment with an explicit centered position while preserving the existing label and `update_battery()` behavior:

```cpp
_battery_label = lv_label_create(_panel);
lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);
lv_obj_set_width(_battery_label, 150);
lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_CENTER, 0);
lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, kBatteryY);
update_battery();
```

- [ ] **Step 6: Run source-focused and native checks**

```bash
c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test
rg -n "logo_codex|codex_pet_idle_frames|codex_pet_idle_durations_ms|lv_image_set_antialias" main/apps/app_codex_status/app_codex_status.cpp
```

Expected: native test exits 0; source search shows the original logo, pet frames, frame durations, and disabled antialiasing still in use.

- [ ] **Step 7: Commit the final view layout**

```bash
git add main/apps/app_codex_status/app_codex_status.cpp
git commit -m "feat: redesign Codex status precision console"
```

### Task 4: Build and Perform Visual State Verification

**Files:**
- Verify: `main/apps/app_codex_status/app_codex_status.cpp`
- Verify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Verify: `tests/codex_status_ui_layout_test.cpp`

- [ ] **Step 1: Run whitespace and native regression checks**

```bash
git diff --check HEAD~2..HEAD
c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test
```

Expected: both commands exit 0.

- [ ] **Step 2: Build the ESP-IDF firmware**

Run from the repository root after exporting ESP-IDF 5.5.4:

```bash
idf.py build
```

Expected: `Project build complete` and a successful `StopWatch-UserDemo.bin` link. If the current working tree contains unrelated build-breaking edits, record the exact failure and rerun against the isolated UI commits rather than modifying unrelated files.

- [ ] **Step 3: Inspect the 466 × 466 page in all required states**

Flash to the configured StopWatch and verify this checklist on-device:

```text
[ ] START/unknown values: no overlap; -- and --% align correctly
[ ] BLE LIVE: status and host fit inside the circular safe area
[ ] Wi-Fi fallback: long host truncates on one centered line
[ ] Pending alert: heading is vermilion; project/detail remain readable
[ ] All handled: heading is existing handled green
[ ] No alerts: heading is etched gray and placeholders remain aligned
[ ] Long model/effort: both truncate inside mirrored instruments
[ ] 100% quotas: percentages remain right-aligned and bars do not collide
[ ] Low battery/charging: existing red/green battery semantics remain
[ ] Pet: original frames, native dimensions, timing, and crisp pixels remain
[ ] Logo: original asset and scale remain
```

- [ ] **Step 4: Make only coordinate-level corrections found on-device**

If an overlap or edge clipping is observed, change only constants in `codex_status_ui_layout.h`, rerun the native geometry test, rebuild, and repeat the checklist. Do not change assets, transport logic, carousel rules, or payload handling.

- [ ] **Step 5: Commit verified coordinate corrections, if any**

```bash
git add main/apps/app_codex_status/codex_status_ui_layout.h main/apps/app_codex_status/app_codex_status.cpp tests/codex_status_ui_layout_test.cpp
git diff --cached --quiet || git commit -m "fix: tune Codex status layout on device"
```
