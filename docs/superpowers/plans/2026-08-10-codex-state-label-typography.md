# Codex State Label Typography Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the Codex page's `PENDING` and `RUNNING` labels in bundled Montserrat Bold 22 px at the exact geometry approved in design option C.

**Architecture:** Keep the existing LVGL label and glow objects intact. Add one purpose-built ASCII font asset, update the centralized Codex layout constants, and lock both choices with the existing Python firmware-contract suite before running the native/offline verification route.

**Tech Stack:** ESP-IDF CMake, LVGL generated C fonts, C++17 firmware UI, Python `unittest` contract tests, shell-based offline validation.

---

## File Map

- Create `main/assets/fonts/MontserratBold22.c`: generated LVGL glyph data for the two state words.
- Modify `main/assets/assets.h`: export the new font to firmware UI code.
- Modify `main/apps/app_codex_status/codex_status_ui_layout.h`: record the approved label and count geometry.
- Modify `main/apps/app_codex_status/app_codex_status.cpp`: apply the new font to both state labels.
- Modify `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`: lock the approved design contract and font asset.
- Modify `mac-codex-agent/tests/test_firmware_font_contract.py`: update the cross-firmware typography expectation.

### Task 1: Lock the approved typography contract in failing tests

**Files:**
- Modify: `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`

- [ ] **Step 1: Update the Codex layout contract expectations**

Replace the old label geometry and `MontserratBold16` assertions with:

```python
self.assertIn("constexpr int kPendingLabelX = 54", layout)
self.assertIn("constexpr int kRunningLabelX = 300", layout)
self.assertIn("constexpr int kStateLabelY = 272", layout)
self.assertIn("constexpr int kStateLabelWidth = 112", layout)
self.assertIn("constexpr int kStateCountY = 298", layout)
self.assertIn("&MontserratBold22", pending_block)
self.assertIn("&MontserratBold22", running_block)
```

Rename `test_state_words_use_bundled_true_bold_font` to
`test_state_words_use_bundled_22px_true_bold_font` and assert:

```python
assets_header = (ROOT / "main/assets/assets.h").read_text()
bold_font_path = ROOT / "main/assets/fonts/MontserratBold22.c"

self.assertIn("LV_FONT_DECLARE(MontserratBold22);", assets_header)
self.assertTrue(bold_font_path.is_file())
bold_font = bold_font_path.read_text()
self.assertIn("Size: 22 px", bold_font)
self.assertIn("const lv_font_t MontserratBold22", bold_font)
```

Update the matching exact font block in
`test_firmware_font_contract.py` from `&MontserratBold16` to
`&MontserratBold22` without changing unrelated font expectations.

- [ ] **Step 2: Run focused tests and verify the red phase**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_codex_status_f_layout_contract \
  mac-codex-agent.tests.test_firmware_font_contract
```

Expected: FAIL because the layout still contains `x=62/308`, `y=278/294`,
`width=96`, `MontserratBold22.c` does not exist, and both labels still use
`MontserratBold16`.

- [ ] **Step 3: Commit the failing contract**

```bash
git add mac-codex-agent/tests/test_codex_status_f_layout_contract.py \
  mac-codex-agent/tests/test_firmware_font_contract.py
git commit -m "test: lock Codex state label typography"
```

### Task 2: Add the production 22 px font and approved geometry

**Files:**
- Create: `main/assets/fonts/MontserratBold22.c`
- Modify: `main/assets/assets.h`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Test: `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`
- Test: `mac-codex-agent/tests/test_firmware_font_contract.py`

- [ ] **Step 1: Generate the minimal LVGL font asset**

Use the Montserrat Bold face already supplied by the checked-out LVGL test
assets. If the root dependency checkout is absent, use the identical source
from the existing `.worktrees/multi-agent-secure-pairing` dependency checkout.

```bash
npx --yes lv_font_conv@1.5.3 \
  --bpp 2 \
  --size 22 \
  --no-compress \
  --no-prefilter \
  --no-kerning \
  --font components/lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf \
  --symbols DEGINPRU \
  --format lvgl \
  --lv-include lvgl.h \
  --lv-font-name MontserratBold22 \
  -o main/assets/fonts/MontserratBold22.c
```

The generated header must identify `Size: 22 px`, and the exported symbol must
be exactly `MontserratBold22`.

- [ ] **Step 2: Declare the font and apply the exact layout constants**

Add to `main/assets/assets.h`:

```cpp
LV_FONT_DECLARE(MontserratBold22);
```

Replace the Codex state constants with:

```cpp
constexpr int kPendingLabelX = 54;
constexpr int kRunningLabelX = 300;
constexpr int kPendingCountX = 78;
constexpr int kRunningCountX = 324;
constexpr int kStateLabelY = 272;
constexpr int kStateLabelWidth = 112;
constexpr int kStateCountY = 298;
constexpr int kStateCountWidth = 64;
```

In both label creation blocks in `app_codex_status.cpp`, replace:

```cpp
&MontserratBold16,
```

with:

```cpp
&MontserratBold22,
```

Do not change colors, text, alignment, count font, glow registration, or TRAE
code.

- [ ] **Step 3: Run focused tests and verify the green phase**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_codex_status_f_layout_contract \
  mac-codex-agent.tests.test_firmware_font_contract
```

Expected: PASS.

- [ ] **Step 4: Inspect the generated asset and diff**

Run:

```bash
rg -n "Size: 22 px|MontserratBold22" \
  main/assets/fonts/MontserratBold22.c main/assets/assets.h \
  main/apps/app_codex_status/app_codex_status.cpp
git diff --check
git diff --stat
```

Expected: the font metadata and declaration are present, both UI labels use the
new symbol, `git diff --check` prints nothing, and no TRAE source is modified.

- [ ] **Step 5: Commit the implementation**

```bash
git add main/assets/fonts/MontserratBold22.c main/assets/assets.h \
  main/apps/app_codex_status/codex_status_ui_layout.h \
  main/apps/app_codex_status/app_codex_status.cpp
git commit -m "feat: enlarge Codex state labels"
```

### Task 3: Run regression and fidelity verification

**Files:**
- Verify: `docs/superpowers/specs/2026-08-10-codex-state-label-typography-design.md`
- Verify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Verify: `main/apps/app_codex_status/app_codex_status.cpp`
- Verify: `main/assets/fonts/MontserratBold22.c`

- [ ] **Step 1: Run the complete Python contract suite**

Run:

```bash
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: PASS with no failures or errors.

- [ ] **Step 2: Run the repository offline validation route**

Run:

```bash
scripts/run_unified_offline_validation.sh
```

Expected: exit code 0. The script's Codex UI native tests, font contracts, and
firmware source checks all pass.

- [ ] **Step 3: Compare the implementation against design option C**

At a 466 x 466 canvas, verify the implementation values against the approved
design contract:

```text
PENDING: x=54, y=272, width=112, Montserrat Bold 22, #FF6248
RUNNING: x=300, y=272, width=112, Montserrat Bold 22, #49C9EE
Counts: y=298, Montserrat SemiBold 26, existing x/width/colors
```

If a hardware or LVGL simulator capture is available, save the actual 466 x
466 screenshot and compare it to the approved companion reference. If this
environment cannot produce a running LVGL capture, report that limitation
explicitly and use the exact layout/font contract tests as the non-visual
fidelity evidence; do not claim pixel-diff verification.

- [ ] **Step 4: Confirm repository state and report evidence**

Run:

```bash
git status --short
git log -3 --oneline
```

Expected: only the intentionally untracked `.superpowers/` visual-companion
session may remain; implementation and tests are committed. Report focused
test output, full suite output, offline validation output, the design spec
path, and any screenshot limitation.
