# Codex Pet and Quota Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the Codex robot frame, enlarge the existing animation by about 15%, and move each quota percentage to the right of its progress bar in both the approved preview and firmware.

**Architecture:** Keep all status data, timers, assets, and widget ownership unchanged. Update the layout contract first, then make the LVGL view consume those constants; update the standalone preview from the same exact geometry so it remains a faithful review surface.

**Tech Stack:** C++17, LVGL, ESP-IDF, native `assert` tests, Python `unittest`, static HTML/CSS.

---

### Task 1: Lock the refined firmware geometry

**Files:**
- Modify: `tests/codex_status_ui_layout_test.cpp:9-40`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h:32-53`

- [ ] **Step 1: Write failing geometry assertions**

Replace the pet-dock assertions with:

```cpp
assert(kPetX == 49);
assert(kPetY == 104);
assert(kPetWidth == 110);
assert(kPetHeight == 120);
assert(kPetScale == 293);
assert(kPetX + kPetWidth == 159);
assert(kBeaconSpine.x - (kPetX + kPetWidth) == 11);
```

Replace the quota-column assertions with:

```cpp
assert(kQuotaBarX == 101);
assert(kQuotaBarWidth == 248);
assert(kQuotaPercentX == 360);
assert(kQuotaPercentWidth == 51);
assert(kQuotaBarX + kQuotaBarWidth < kQuotaPercentX);
assert(kQuotaPercentX + kQuotaPercentWidth == kQuotaX + kQuotaWidth);
```

- [ ] **Step 2: Run the layout test and verify it fails**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test
```

Expected: compilation fails because `kPetScale` and `kQuotaPercentX` are not defined and the old constants do not match.

- [ ] **Step 3: Replace the old layout constants**

Use these exact constants in `codex_status_ui_layout.h`:

```cpp
constexpr int kPetX = 49;
constexpr int kPetY = 104;
constexpr int kPetWidth = 110;
constexpr int kPetHeight = 120;
constexpr int kPetScale = 293;
constexpr Rect kBeaconSpine{170, 112, 2, 104};

constexpr int kQuotaX = 55;
constexpr int kQuotaWidth = 356;
constexpr int kQuotaLabelWidth = 32;
constexpr int kQuotaBarX = 101;
constexpr int kQuotaBarWidth = 248;
constexpr int kQuotaPercentX = 360;
constexpr int kQuotaPercentWidth = 51;
```

The LVGL image scale uses 256 as 100%; `293 / 256 = 1.1445`, which yields approximately `110 × 119` from the native `96 × 104` source.

- [ ] **Step 4: Run the layout test and verify it passes**

Run the Step 2 command again.

Expected: exit code 0 with no output.

### Task 2: Update the LVGL widgets and source contract

**Files:**
- Modify: `main/apps/app_codex_status/app_codex_status.cpp:85-99,127-151,400-455`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py:69-110`

- [ ] **Step 1: Make the source-contract test fail on the old UI**

Change the test to require removal of the outline, use of `kPetScale`, and right-aligned percentage placement:

```python
self.assertNotIn("make_outline(", app)
self.assertNotIn("kPetDock", layout)
self.assertIn("lv_image_set_scale(_pet_image, kPetScale)", app)
self.assertIn("lv_obj_set_pos(pct, kQuotaPercentX, y - 6)", app)
self.assertIn("lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0)", app)
self.assertIn("constexpr int kQuotaBarX = 101", layout)
self.assertIn("constexpr int kQuotaBarWidth = 248", layout)
self.assertIn("constexpr int kQuotaPercentX = 360", layout)
```

- [ ] **Step 2: Run the source-contract test and verify it fails**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py
```

Expected: failure mentioning the old `make_outline`/pet-dock contract.

- [ ] **Step 3: Remove the pet outline and scale the existing animation**

Delete `make_outline(...)` and its call. Keep the original frame descriptor and timer, then add scale before positioning:

```cpp
_pet_image = lv_image_create(_panel);
lv_image_set_src(_pet_image, codex_pet_idle_frames[0]);
lv_image_set_antialias(_pet_image, false);
lv_image_set_scale(_pet_image, kPetScale);
lv_obj_clear_flag(_pet_image, LV_OBJ_FLAG_CLICKABLE);
lv_obj_set_pos(_pet_image, kPetX, kPetY);
```

- [ ] **Step 4: Put each percentage after its progress bar**

Keep bar construction unchanged except for the new geometry constants. Position and align the percentage with:

```cpp
lv_obj_set_width(pct, kQuotaPercentWidth);
lv_obj_set_style_text_align(pct, LV_TEXT_ALIGN_RIGHT, 0);
lv_obj_set_pos(pct, kQuotaPercentX, y - 6);
```

- [ ] **Step 5: Run the layout and source-contract tests**

Run the commands from Task 1 Step 2 and Task 2 Step 2.

Expected: both exit 0; Python reports all tests `OK`.

### Task 3: Synchronize the approved preview and verify the firmware

**Files:**
- Modify: `/Users/bytedance/.codex/visualizations/2026/07/12/019f5555-dd8e-7760-bd37-1e3d164069bb/index.html`
- Verify: `main/apps/app_codex_status/assets/codex_pet_assets.cpp`

- [ ] **Step 1: Update preview geometry**

Remove the `.pet-dock` rule and element. Apply the approved pet and quota geometry:

```css
.pet { left: 49px; top: 104px; width: 110px; height: 120px; }
.quota-pct { left: 305px; top: -4px; width: 51px; text-align: right; }
.track { left: 46px; top: 8px; width: 248px; }
```

The quota positions are relative to `.quota-row` at `x=55`, producing global `x=101` for the track and `x=360` for the percentage.

- [ ] **Step 2: Reload and visually inspect the preview**

Reload `http://127.0.0.1:60257/` and verify:

- no rectangle surrounds the robot;
- the robot remains at least 11 px from the violet spine;
- both rows read `5H → bar → 72%` and `7D → bar → 64%`;
- neither percentage crosses the right safe edge at `x=411`.

- [ ] **Step 3: Run the focused Codex tests**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/codex_status_ui_layout_test.cpp \
  -o /tmp/codex_status_ui_layout_test && \
  /tmp/codex_status_ui_layout_test

mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_firmware_font_contract.py
```

Expected: native test exits 0 and Python reports all tests `OK`.

- [ ] **Step 4: Run the unified candidate build and capacity gate**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh" >/dev/null
./scripts/build_unified_candidate.sh
```

Expected: build exits 0, `capacity_gate_passed` is `true`, and the app remains below `6684672` bytes.

- [ ] **Step 5: Review and commit only refinement files**

Run:

```bash
git diff --check -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git add -- \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp \
  tests/codex_status_ui_layout_test.cpp \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "Refine Codex pet and quota layout"
```

Expected: one isolated commit; unrelated working-tree changes remain unstaged.
