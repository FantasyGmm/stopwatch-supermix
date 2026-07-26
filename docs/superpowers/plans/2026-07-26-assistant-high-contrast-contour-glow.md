# Assistant High-Contrast Contour Glow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the barely perceptible Assistant halo with the user-approved A preview: a visibly brighter two-layer contour glow around the unchanged five-bar waveform.

**Architecture:** Keep the existing deterministic per-bar renderer and two-layer composition. Increase the inner layer from 7 px / alpha 190 to 8 px / alpha 230 and the outer layer from 26 px / alpha 110 to 30 px / alpha 220, then regenerate the RGB565 asset and directly flash only the application partition.

**Tech Stack:** Python 3, Pillow, `unittest`, LVGL RGB565, ESP-IDF 5.5.4, esptool.

---

## File structure

- Modify `mac-codex-agent/tests/test_assistant_icon_asset.py`: require the approved mid-brightness footprint while retaining the waveform-core contract.
- Modify `scripts/generate_assistant_icon.py`: update the two halo radius and alpha pairs only.
- Modify `main/assets/images/icon_assistant.c`: regenerate the 200 x 200 RGB565 asset.

### Task 1: Define the visible-glow regression contract

**Files:**
- Modify: `mac-codex-agent/tests/test_assistant_icon_asset.py:69-80,125-126`

- [ ] **Step 1: Add a failing mid-brightness footprint assertion**

Add this block between the low-light halo and strong-core checks:

```python
visible = bounds(self.pixels, 40)
visible_long_axis = max(visible[2] - visible[0], visible[3] - visible[1])
self.assertGreaterEqual(visible_long_axis, 168)
self.assertLessEqual(visible_long_axis, 175)
```

Allow the four near-edge samples to reach brightness 20:

```python
for point in ((5, 100), (194, 100), (100, 5), (100, 194)):
    self.assertLessEqual(brightness(self.pixel(*point)), 20)
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: `test_matches_launcher_family_visual_footprint` fails because the current 26 px / alpha 110 asset has a brightness-40 long-axis footprint of about 161 px, below the approved minimum of 168 px.

### Task 2: Render the approved A treatment

**Files:**
- Modify: `scripts/generate_assistant_icon.py:23-27`
- Modify: `main/assets/images/icon_assistant.c`
- Test: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Update only the glow constants**

Use exactly:

```python
INNER_GLOW_RADIUS = 8
INNER_GLOW_ALPHA = 230
OUTER_GLOW_RADIUS = 30
OUTER_GLOW_ALPHA = 220
GLOW_PAD = OUTER_GLOW_RADIUS * 3
```

Do not change `BARS`, `BODY_STOPS`, the two-layer compositing order, or the body renderer.

- [ ] **Step 2: Regenerate the production asset and preview**

Run:

```bash
python3 scripts/generate_assistant_icon.py \
  --output-c main/assets/images/icon_assistant.c \
  --preview /tmp/assistant-glow-approved-a.png
```

Expected: the decoded RGB565 preview matches `/tmp/assistant-glow-strong-a.png`: a clearly visible cyan-blue-violet contour glow with no center circle, frame, or body-size change.

- [ ] **Step 3: Verify GREEN and deterministic output**

Run the focused test again, regenerate to `/tmp/icon-assistant-approved-a-verify.c`, compare it byte-for-byte with the tracked asset, and run `git diff --check`.

Expected: all three tests pass and the generated C files are identical.

- [ ] **Step 4: Commit the implementation**

```bash
git add scripts/generate_assistant_icon.py \
  mac-codex-agent/tests/test_assistant_icon_asset.py \
  main/assets/images/icon_assistant.c \
  docs/superpowers/plans/2026-07-26-assistant-high-contrast-contour-glow.md
git commit -m "fix: strengthen Assistant contour glow"
```

### Task 3: Build, flash, and validate

**Files:**
- Verify: `build-unified/StopWatch-UserDemo.bin`

- [ ] **Step 1: Build the committed firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: build and capacity checks pass and the embedded application version equals the implementation commit.

- [ ] **Step 2: Directly flash only the application partition**

Detect the active `/dev/cu.usbmodem*` port and write `build-unified/StopWatch-UserDemo.bin` at `0x20000` with the ESP-IDF Python environment and esptool. Do not create a backup and do not rewrite the unchanged bootloader, partition table, or assets partition.

Expected: esptool reports `Hash of data verified` and hard-resets the watch.

- [ ] **Step 3: Validate startup and host state**

Run `scripts/validate_watch_boot.py` on the detected port and confirm the StopWatch Codex Agent process remains running.

Expected: the log reports the new application version and `launcher=open`; the Mac Agent process exists. Physical glow appearance remains the final visual acceptance check.
