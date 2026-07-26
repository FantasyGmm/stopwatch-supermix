# Assistant Wider Contour Glow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the Assistant icon's colored contour glow to the launcher-family scale without enlarging or blurring the five waveform bars.

**Architecture:** Keep the existing per-bar masks, gradients, body geometry, and 7 px inner halo. Change only the outer blur from 18 px / alpha 90 to 26 px / alpha 110, regenerate the deterministic RGB565 asset, and add a low-brightness footprint contract that distinguishes the wider halo from the current asset.

**Tech Stack:** Python 3, Pillow, `unittest`, LVGL RGB565, ESP-IDF 5.5.4, esptool.

---

## File structure

- Modify `mac-codex-agent/tests/test_assistant_icon_asset.py`: require a launcher-family-sized low-brightness halo while preserving the bright waveform core.
- Modify `scripts/generate_assistant_icon.py`: update only the outer contour radius and opacity constants.
- Modify `main/assets/images/icon_assistant.c`: regenerate the 200 x 200 RGB565 asset.

### Task 1: Define the wider-halo contract

**Files:**
- Modify: `mac-codex-agent/tests/test_assistant_icon_asset.py:69-73`

- [ ] **Step 1: Add a failing low-brightness footprint assertion**

Replace the current footprint test with:

```python
def test_matches_launcher_family_visual_footprint(self):
    # RGB565 quantizes the first visible blue step to brightness 8, so a
    # threshold of 7 includes the complete low-light contour footprint.
    halo = bounds(self.pixels, 7)
    halo_long_axis = max(halo[2] - halo[0], halo[3] - halo[1])
    self.assertGreaterEqual(halo_long_axis, 190)
    self.assertLessEqual(halo_long_axis, 199)

    strong = bounds(self.pixels, 80)
    strong_long_axis = max(strong[2] - strong[0], strong[3] - strong[1])
    self.assertGreaterEqual(strong_long_axis, 145)
    self.assertLessEqual(strong_long_axis, 165)
```

The 7 threshold includes RGB565's first visible brightness step at level 8 and measures the complete soft outer light. The 80-level bound protects the unchanged five-bar core from accidental enlargement.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: `test_matches_launcher_family_visual_footprint` fails because the current 18 px / alpha 90 halo measures about 182 px on its long axis, below the new 190 px minimum.

### Task 2: Expand only the outer contour layer

**Files:**
- Modify: `scripts/generate_assistant_icon.py:23-27`
- Modify: `main/assets/images/icon_assistant.c`
- Test: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Update the outer glow parameters**

Keep the inner layer unchanged and use:

```python
INNER_GLOW_RADIUS = 7
INNER_GLOW_ALPHA = 190
OUTER_GLOW_RADIUS = 26
OUTER_GLOW_ALPHA = 110
GLOW_PAD = OUTER_GLOW_RADIUS * 3
```

Do not change `BARS`, `BODY_STOPS`, the compositing order, or the body renderer.

- [ ] **Step 2: Regenerate the production asset and previews**

Run:

```bash
python3 scripts/generate_assistant_icon.py \
  --output-c main/assets/images/icon_assistant.c \
  --preview /tmp/assistant-aurora-wave-wider-glow.png
```

Create 72 x 72 and 45 x 45 previews from the same rendered image for visual inspection. Expected: the colored light reaches farther around every rounded bar, the bars remain crisp and unchanged, and no center circle or rectangular edge appears.

- [ ] **Step 3: Run the focused test and verify GREEN**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: all three tests pass. If the exact RGB565 footprint lands outside the specified range, inspect the generated image and adjust only the footprint thresholds to describe the approved 26 px / alpha 110 output; do not change the approved rendering parameters.

- [ ] **Step 4: Verify deterministic generation**

Regenerate to `/tmp/icon-assistant-wider-glow-verify.c` and compare it byte-for-byte with `main/assets/images/icon_assistant.c`. Run `git diff --check` and inspect the 200 px, 72 px, and 45 px previews.

- [ ] **Step 5: Commit the implementation**

```bash
git add scripts/generate_assistant_icon.py \
  mac-codex-agent/tests/test_assistant_icon_asset.py \
  main/assets/images/icon_assistant.c \
  docs/superpowers/plans/2026-07-26-assistant-wider-contour-glow.md
git commit -m "fix: enlarge Assistant contour glow"
```

### Task 3: Build, directly flash, and validate

**Files:**
- Verify: `build-unified/StopWatch-UserDemo.bin`

- [ ] **Step 1: Build the committed firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: build and capacity gate pass, and the embedded build version equals the implementation commit.

- [ ] **Step 2: Flash only the application partition**

Detect the current `/dev/cu.usbmodem*` port and write `build-unified/StopWatch-UserDemo.bin` to `0x20000` with the ESP-IDF Python environment and esptool. Do not create a full-flash backup and do not rewrite the bootloader, partition table, or assets partition.

Expected: esptool reports `Hash of data verified` and hard-resets the watch.

- [ ] **Step 3: Validate startup and host state**

Run `scripts/validate_watch_boot.py` on the detected port and confirm the StopWatch Codex Agent process remains running.

Expected: the firmware loads from the application partition, the launcher responds when the serial channel is available, and the Mac Agent process exists. Treat physical on-watch glow appearance as a separate visual acceptance check.
