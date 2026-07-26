# Assistant Contour Glow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ineffective centered Assistant glow with a clearly visible two-layer halo that follows every edge of the five H2 waveform bars.

**Architecture:** Remove the radial field entirely. Build a rounded mask and clamped per-bar aurora gradient field for each waveform bar, blur those masks into a 7 px inner halo and an 18 px outer halo, composite all glow layers behind the unchanged body, then regenerate the deterministic RGB565 asset.

**Tech Stack:** Python 3, Pillow, `unittest`, LVGL RGB565, ESP-IDF 5.5.4, esptool.

---

## File structure

- Modify `mac-codex-agent/tests/test_assistant_icon_asset.py`: assert visible glow immediately outside all four extrema of the waveform and dark canvas edges.
- Modify `scripts/generate_assistant_icon.py`: replace the radial field with per-bar inner and outer blurred masks.
- Modify `main/assets/images/icon_assistant.c`: regenerate the 200 x 200 RGB565 icon.

### Task 1: Define the contour-glow contract

**Files:**
- Modify: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Add failing edge-halo assertions**

Add this test coverage after the existing bar-gradient assertions:

```python
edge_halo = [
    self.pixel(20, 100),
    self.pixel(180, 100),
    self.pixel(100, 25),
    self.pixel(100, 175),
]
for pixel in edge_halo:
    self.assertGreaterEqual(brightness(pixel), 14)
    self.assertGreater(pixel[2], pixel[0])
```

Allow the strong-light footprint to grow from 160 px to at most 165 px because the new contour halo intentionally extends beyond the opaque subject. The 14-level edge threshold remains safely above the previous radial asset's value of 0 while accommodating RGB565 quantization of the violet bottom halo.

Keep the existing checks that pixels at `(5, 100)`, `(194, 100)`, `(100, 5)`, and `(100, 194)` have brightness at most 12.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
/Users/bytedance/Projects.localized/Codex/StopWatch/mac-codex-agent/.venv/bin/python \
  -m unittest mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: the new halo assertion fails because the current centered radial field is black at the waveform extrema.

### Task 2: Render the two-layer contour glow

**Files:**
- Modify: `scripts/generate_assistant_icon.py`
- Modify: `main/assets/images/icon_assistant.c`
- Test: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Replace the radial constants with contour constants**

Use:

```python
INNER_GLOW_RADIUS = 7
INNER_GLOW_ALPHA = 190
OUTER_GLOW_RADIUS = 18
OUTER_GLOW_ALPHA = 90
```

Import `ImageFilter`. Remove `GLOW_STOPS`, `GLOW_RADIUS`, and `radial_glow()` so there is no centered light field.

- [ ] **Step 2: Create a padded gradient glow for each bar**

For each bar, create a transparent rounded mask at the existing 4x work resolution. Pad it by `OUTER_GLOW_RADIUS * 3 * SCALE`; build an aurora color field whose normalized coordinates clamp to the original bar bounds, so the halo outside an edge keeps that edge's color. Blur the same mask twice with `GaussianBlur(INNER_GLOW_RADIUS * SCALE)` and `GaussianBlur(OUTER_GLOW_RADIUS * SCALE)`, scale their alpha to `INNER_GLOW_ALPHA` and `OUTER_GLOW_ALPHA`, and composite every outer layer before every inner layer.

The existing body renderer remains unchanged and is composited last, hiding all glow beneath the five opaque bars.

- [ ] **Step 3: Regenerate the asset and preview**

Run:

```bash
python3 scripts/generate_assistant_icon.py \
  --output-c main/assets/images/icon_assistant.c \
  --preview /tmp/assistant-aurora-wave-contour-glow.png
```

Expected: every rounded bar edge emits visible colored light; the center circle is absent; no glow is clipped at the canvas boundary.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the Task 1 command again.

Expected: all focused asset tests pass.

- [ ] **Step 5: Verify determinism and commit**

Regenerate to `/tmp/icon_assistant-contour-glow.c`, compare it byte-for-byte with the tracked asset, run `git diff --check`, inspect the 200 px preview, and commit the test, generator, generated asset, and this plan with message `fix: add contour glow to Assistant icon`.

### Task 3: Build, flash, and validate

**Files:**
- Verify: `build-unified/StopWatch-UserDemo.bin`

- [ ] **Step 1: Build the committed firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: the capacity gate passes and the firmware version equals the new commit.

- [ ] **Step 2: Directly flash only the app partition**

Use the ESP-IDF Python environment and esptool to write `build-unified/StopWatch-UserDemo.bin` at `0x20000` on the detected `/dev/cu.usbmodem*` port. Do not back up or rewrite the unchanged bootloader, partition table, or assets partition.

Expected: esptool reports `Hash of data verified` and hard-resets the watch.

- [ ] **Step 3: Validate boot and host-agent state**

Run `scripts/validate_watch_boot.py` with the ESP-IDF Python environment, then confirm the StopWatch Codex Agent process remains running.

Expected: the watch reports `launcher=open`, the booted version equals the new commit, and the Mac Agent process exists. Physical halo appearance remains the final on-watch acceptance check.
