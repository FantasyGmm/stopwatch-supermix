# Assistant Aurora Wave Icon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Assistant launcher asset with the approved H2 five-bar aurora waveform without changing launcher or Assistant behavior.

**Architecture:** Keep the existing deterministic Pillow-to-RGB565 asset pipeline. Render the selected H2 SVG semantics faithfully: one centered radial glow plus an independent diagonal gradient inside every rounded waveform bar. Scale only the five-bar subject by about 10% for the user's on-device correction, regenerate the existing C asset, then build and flash directly because this is an asset-only change.

**Tech Stack:** Python 3, Pillow, `unittest`, LVGL RGB565, ESP-IDF 5.5.4, esptool-based repository scripts.

---

## File structure

- Modify `mac-codex-agent/tests/test_assistant_icon_asset.py`: assert the five-bar silhouette, non-white aurora colors, black margins, descriptor, and footprint.
- Modify `scripts/generate_assistant_icon.py`: deterministically draw the H2 glow, gradient, and five rounded bars.
- Modify `main/assets/images/icon_assistant.c`: generated 200 x 200 RGB565 asset.
- Create only temporary previews under `/tmp`; do not add preview binaries to Git.

### Task 1: Define and implement the H2 asset contract

**Files:**
- Modify: `mac-codex-agent/tests/test_assistant_icon_asset.py`
- Modify: `scripts/generate_assistant_icon.py`
- Modify: `main/assets/images/icon_assistant.c`

- [ ] **Step 1: Replace the star landmark test with a waveform and color contract**

Add helpers for column runs and color dominance, then assert:

```python
def bright_run_in_column(pixels, x, threshold=100):
    ys = [y for y in range(200) if brightness(pixels[y * 200 + x]) >= threshold]
    return min(ys), max(ys) + 1

def test_has_approved_five_bar_aurora_waveform(self):
    expected_runs = {
        43: (79, 121),
        72: (60, 140),
        100: (38, 162),
        128: (60, 140),
        157: (79, 121),
    }
    for x, expected in expected_runs.items():
        actual = bright_run_in_column(self.pixels, x)
        self.assertLessEqual(abs(actual[0] - expected[0]), 3)
        self.assertLessEqual(abs(actual[1] - expected[1]), 3)

    left = self.pixel(43, 100)
    center = self.pixel(100, 100)
    right = self.pixel(157, 100)
    self.assertGreater(left[1], left[2])
    self.assertGreater(center[2], center[0])
    self.assertGreater(right[2], right[1])
    self.assertLess(max(left), 245)
    self.assertLess(max(center), 245)
    self.assertLess(max(right), 245)
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: the new five-bar contract fails against the current three-star asset while the LVGL descriptor contract remains green.

- [ ] **Step 3: Replace the generator's star renderer with rounded waveform bars**

Render at 800 x 800, using output-space bar rectangles scaled by four:

```python
BARS = (
    (34, 79, 52, 121),
    (63, 60, 81, 140),
    (91, 38, 109, 162),
    (119, 60, 137, 140),
    (148, 79, 166, 121),
)
COLORS = (
    (74, 231, 213, 255),
    (66, 182, 240, 255),
    (128, 100, 240, 255),
)
```

Scale the reference bar geometry uniformly around the canvas center, composite the reference diagonal teal-to-cyan-to-violet gradient independently inside every bar, and draw the reference centered radial cyan-violet glow. Do not derive the glow from the waveform mask, and do not draw any white highlight, ring, or container.

- [ ] **Step 4: Generate the C asset and preview**

Run:

```bash
python3 scripts/generate_assistant_icon.py \
  --output-c main/assets/images/icon_assistant.c \
  --preview /tmp/assistant-aurora-wave.png
```

Expected: exit 0, a 200 x 200 preview, and an 80,000-byte RGB565 array.

- [ ] **Step 5: Run the focused test and verify GREEN**

Run the Step 2 command again.

Expected: all focused asset tests pass.

- [ ] **Step 6: Inspect decoded family and small-size previews**

Decode the generated C array and compare it beside Setup, Badge, and Codex at 200 px, then inspect the new icon at 72 px and 45 px.

Expected: five bars remain separate, the gradient remains visible, the glow stays clear of the canvas edge, and the icon's optical weight matches the launcher family.

### Task 2: Build, commit, and flash the narrow asset change

**Files:**
- Verify: `main/assets/images/icon_assistant.c`
- Verify: `scripts/generate_assistant_icon.py`
- Verify: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Check generated determinism and whitespace**

Regenerate to `/tmp/icon_assistant.c`, compare it byte-for-byte with the tracked asset, and run `git diff --check`.

Expected: identical generated C files and no whitespace errors.

- [ ] **Step 2: Build the unified firmware**

Run:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: exit 0 and a complete unified firmware artifact set.

- [ ] **Step 3: Commit the implementation**

Stage only the focused test, generator, generated asset, and this plan; commit with `feat: use aurora waveform for Assistant icon`.

- [ ] **Step 4: Flash the asset-only firmware directly**

Detect the usable `/dev/cu.usbmodem*` port and run the repository flash script against the just-built candidate. Do not create a new full-flash backup for this deterministic icon-only asset change.

Expected: all programmed regions verify successfully.

- [ ] **Step 5: Validate boot and restore the Mac Codex Agent**

Run `scripts/validate_watch_boot.py` on the detected port and restore the Mac Codex Agent process after serial validation releases the device.

Expected: boot reaches `launcher=open`, the device remains responsive, and the Mac Agent reconnects. Record that this proves boot integrity, not final physical color acceptance.
