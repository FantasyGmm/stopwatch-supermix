# Assistant Soft Spark Icon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current Assistant launcher image with the approved three-star Soft Spark Core icon at the same perceived size as the existing StopWatch launcher icons.

**Architecture:** Generate the 200 x 200 image deterministically with a small Pillow-based development script, then serialize the final pixels directly into the existing LVGL RGB565 C asset contract. A standard-library contract test decodes the C array to verify its dimensions, byte count, footprint, black safety margin, and key composition landmarks without adding any runtime rendering code.

**Tech Stack:** Python 3, Pillow for offline asset generation, Python `unittest`, LVGL RGB565 image descriptors, ESP-IDF unified firmware build and flash scripts.

---

## File structure

- Create `scripts/generate_assistant_icon.py`: deterministic high-resolution star/glow renderer and RGB565 C serializer.
- Modify `main/assets/images/icon_assistant.c`: generated 200 x 200 launcher asset; keep symbol and descriptor contract unchanged.
- Create `mac-codex-agent/tests/test_assistant_icon_asset.py`: standard-library parser and visual-footprint contract tests.
- Do not modify `main/apps/app_assistant`, `main/assets/assets.h`, or any launcher/runtime code.

### Task 1: Add the Assistant icon asset contract

**Files:**
- Create: `mac-codex-agent/tests/test_assistant_icon_asset.py`
- Test: `main/assets/images/icon_assistant.c`

- [ ] **Step 1: Write the failing asset contract test**

Create `mac-codex-agent/tests/test_assistant_icon_asset.py`:

```python
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "main/assets/images/icon_assistant.c"


def load_pixels():
    source = ASSET.read_text()
    body = source.split("icon_assistant_map[] = {", 1)[1].split("};", 1)[0]
    raw = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body))
    pixels = []
    for offset in range(0, len(raw), 2):
        value = raw[offset] | (raw[offset + 1] << 8)
        pixels.append(
            (
                ((value >> 11) & 0x1F) * 255 // 31,
                ((value >> 5) & 0x3F) * 255 // 63,
                (value & 0x1F) * 255 // 31,
            )
        )
    return source, raw, pixels


def brightness(pixel):
    return max(pixel)


def bounds(pixels, threshold):
    points = [
        (index % 200, index // 200)
        for index, pixel in enumerate(pixels)
        if brightness(pixel) > threshold
    ]
    return (
        min(x for x, _ in points),
        min(y for _, y in points),
        max(x for x, _ in points) + 1,
        max(y for _, y in points) + 1,
    )


class AssistantIconAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source, cls.raw, cls.pixels = load_pixels()

    def pixel(self, x, y):
        return self.pixels[y * 200 + x]

    def test_keeps_lvgl_rgb565_contract(self):
        self.assertIn(".header.cf    = LV_COLOR_FORMAT_RGB565", self.source)
        self.assertIn(".header.w     = 200", self.source)
        self.assertIn(".header.h     = 200", self.source)
        self.assertIn(".data_size    = 40000 * 2", self.source)
        self.assertEqual(len(self.raw), 80000)

    def test_matches_launcher_family_visual_footprint(self):
        strong = bounds(self.pixels, 40)
        faint = bounds(self.pixels, 12)
        strong_long_axis = max(strong[2] - strong[0], strong[3] - strong[1])
        faint_long_axis = max(faint[2] - faint[0], faint[3] - faint[1])
        self.assertGreaterEqual(strong_long_axis, 155)
        self.assertLessEqual(strong_long_axis, 170)
        self.assertGreaterEqual(faint_long_axis, 180)
        self.assertLessEqual(faint_long_axis, 190)
        self.assertGreaterEqual(faint[0], 5)
        self.assertGreaterEqual(faint[1], 5)
        self.assertLessEqual(faint[2], 195)
        self.assertLessEqual(faint[3], 195)

    def test_has_approved_three_star_landmarks_without_an_outer_frame(self):
        self.assertGreaterEqual(brightness(self.pixel(100, 100)), 245)
        self.assertGreaterEqual(brightness(self.pixel(52, 58)), 180)
        self.assertGreaterEqual(brightness(self.pixel(151, 147)), 160)
        for point in ((5, 100), (194, 100), (100, 5), (100, 194)):
            self.assertLessEqual(brightness(self.pixel(*point)), 12)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the contract to verify it fails against the old robot icon**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: `test_has_approved_three_star_landmarks_without_an_outer_frame` fails because the current robot-face asset does not have a white star center and the approved two secondary-star landmarks.

### Task 2: Generate and install the Soft Spark Core asset

**Files:**
- Create: `scripts/generate_assistant_icon.py`
- Modify: `main/assets/images/icon_assistant.c`
- Test: `mac-codex-agent/tests/test_assistant_icon_asset.py`

- [ ] **Step 1: Add the deterministic renderer and RGB565 serializer**

Create `scripts/generate_assistant_icon.py` with these boundaries:

```python
#!/usr/bin/env python3
import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


WORK_SIZE = 800
OUTPUT_SIZE = 200
SCALE = WORK_SIZE // OUTPUT_SIZE


def star_points(cx, cy, outer_radius, inner_radius):
    points = []
    for index in range(8):
        angle = math.radians(-90 + index * 45)
        radius = outer_radius if index % 2 == 0 else inner_radius
        points.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
    return points


def vertical_gradient(size, colors):
    image = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(image)
    stops = len(colors) - 1
    for y in range(size):
        position = y / (size - 1)
        segment = min(int(position * stops), stops - 1)
        local = position * stops - segment
        start, end = colors[segment], colors[segment + 1]
        color = tuple(round(start[i] + (end[i] - start[i]) * local) for i in range(4))
        draw.line((0, y, size, y), fill=color)
    return image


def add_star(canvas, center, outer, inner, colors, glow_color, glow_radius):
    mask = Image.new("L", canvas.size, 0)
    draw = ImageDraw.Draw(mask)
    draw.polygon(star_points(center[0], center[1], outer, inner), fill=255)

    glow_alpha = mask.filter(ImageFilter.GaussianBlur(glow_radius)).point(
        lambda value: value * glow_color[3] // 255
    )
    glow = Image.new("RGBA", canvas.size, (*glow_color[:3], 0))
    glow.putalpha(glow_alpha)
    canvas.alpha_composite(glow)

    fill = vertical_gradient(WORK_SIZE, colors)
    fill.putalpha(mask)
    canvas.alpha_composite(fill)


def render_icon():
    image = Image.new("RGBA", (WORK_SIZE, WORK_SIZE), (0, 0, 0, 255))
    add_star(
        image, (400, 400), 228, 72,
        ((248, 254, 255, 255), (104, 226, 255, 255), (157, 103, 255, 255)),
        (74, 166, 255, 170), 48,
    )
    add_star(
        image, (208, 232), 80, 28,
        ((244, 255, 255, 255), (74, 220, 255, 255)),
        (68, 218, 255, 140), 28,
    )
    add_star(
        image, (604, 588), 68, 24,
        ((246, 239, 255, 255), (157, 104, 255, 255)),
        (143, 96, 255, 140), 26,
    )
    ImageDraw.Draw(image).ellipse((340, 340, 460, 460), fill=(255, 255, 255, 246))
    return image.convert("RGB").resize(
        (OUTPUT_SIZE, OUTPUT_SIZE), Image.Resampling.LANCZOS
    )


def rgb565_bytes(image):
    data = bytearray()
    for red, green, blue in image.getdata():
        value = ((red * 31 // 255) << 11) | ((green * 63 // 255) << 5) | (blue * 31 // 255)
        data.extend((value & 0xFF, value >> 8))
    return bytes(data)


def write_c_asset(path, data):
    lines = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    path.write_text(
        """#ifdef __has_include
#if __has_include(\"lvgl.h\")
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#define LV_LVGL_H_INCLUDE_SIMPLE
#endif
#endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include \"lvgl.h\"
#else
#include \"lvgl/lvgl.h\"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_ICON_ASSISTANT
#define LV_ATTRIBUTE_IMAGE_ICON_ASSISTANT
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_ICON_ASSISTANT uint8_t icon_assistant_map[] = {
""" + "\n".join(lines) + """
};

const lv_image_dsc_t icon_assistant = {
    .header.cf    = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w     = 200,
    .header.h     = 200,
    .data_size    = 40000 * 2,
    .data         = icon_assistant_map,
};
"""
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-c", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    image = render_icon()
    write_c_asset(args.output_c, rgb565_bytes(image))
    if args.preview:
        image.save(args.preview)


if __name__ == "__main__":
    main()
```

The exact radii, glow blur, and star positions may be adjusted during the visual-footprint step, but the approved composition, absence of rings, output format, and target bounds may not change.

- [ ] **Step 2: Generate the C asset and preview**

Run:

```bash
python3 scripts/generate_assistant_icon.py \
  --output-c main/assets/images/icon_assistant.c \
  --preview /tmp/assistant-soft-spark.png
```

Expected: the C asset is replaced, `/tmp/assistant-soft-spark.png` is 200 x 200, and the script exits 0.

- [ ] **Step 3: Run the asset contract and tune only generator constants until green**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_icon_asset.py
```

Expected: 3 tests pass. If footprint assertions fail, adjust only the star radii, positions, or blur constants in `scripts/generate_assistant_icon.py`, regenerate, and repeat.

- [ ] **Step 4: Decode the generated C file and create a family comparison**

Use the same RGB565 decoder from the contract test to build `/tmp/assistant-icon-family-comparison.png` containing the new Assistant icon beside Clock, Codex, Setup, IMU, FFT, and Badge.

Expected visual checks:

- Three distinct four-point silhouettes remain visible.
- No ring, orbit, enclosing frame, or circular boundary is present.
- The new icon has similar visual weight to Codex, Setup, FFT, and Badge.
- Glow does not touch the 200 x 200 canvas edge.
- The 72 x 72 and 45 x 45 previews retain the two secondary stars.

- [ ] **Step 5: Run focused tests and whitespace checks**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_assistant_icon_asset.py \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
git diff --check
```

Expected: all tests pass and `git diff --check` produces no output.

- [ ] **Step 6: Commit the icon implementation**

```bash
git add \
  scripts/generate_assistant_icon.py \
  main/assets/images/icon_assistant.c \
  mac-codex-agent/tests/test_assistant_icon_asset.py
git commit -m "feat: refresh Assistant launcher icon"
```

### Task 3: Build, flash, and verify on the watch

**Files:**
- Verify: `build-unified/StopWatch-UserDemo.bin`
- Record locally: `hardware-evidence/assistant-soft-spark/`

- [ ] **Step 1: Run the full Python test suite**

Run:

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: the suite exits 0 with no failures.

- [ ] **Step 2: Build the unified firmware and enforce capacity gates**

Run only when no other process is writing the main worktree's `build-unified` directory:

```bash
source "$HOME/esp/esp-idf-v5.5.4/export.sh"
./scripts/build_unified_candidate.sh
```

Expected: build exits 0, `capacity_gate_passed` is `true`, and the script prints the new app SHA-256.

- [ ] **Step 3: Verify hardware and backup gates**

Confirm a `/dev/cu.usbmodem*` port exists, the selected backup is exactly 16 MiB, its recorded SHA-256 matches, and the flash script's dry run succeeds:

```bash
./scripts/flash_unified_candidate.sh \
  --dry-run \
  --port /dev/cu.usbmodem1101 \
  --backup hardware-evidence/firmware-backups/2026-07-24-before-running-cyan.bin \
  --build-dir build-unified
```

Expected: dry-run prints the four intended flash regions without writing.

- [ ] **Step 4: Stop the Mac Agent and flash the verified candidate**

```bash
pkill -f '/StopWatch Codex Agent.app/Contents/MacOS/StopWatchCodexAgent' || true
./scripts/flash_unified_candidate.sh \
  --port /dev/cu.usbmodem1101 \
  --backup hardware-evidence/firmware-backups/2026-07-24-before-running-cyan.bin \
  --build-dir build-unified
```

Expected: each region reports `Hash of data verified` and the device hard-resets.

- [ ] **Step 5: Validate boot and restore the Mac Agent**

```bash
mkdir -p hardware-evidence/assistant-soft-spark
/Users/bytedance/.espressif/python_env/idf5.5_py3.9_env/bin/python \
  scripts/validate_watch_boot.py \
  --port /dev/cu.usbmodem1101 \
  --timeout 60 \
  --log hardware-evidence/assistant-soft-spark/boot-20260726.log \
  --summary hardware-evidence/assistant-soft-spark/boot-20260726.json
open -a '/Users/bytedance/Applications/StopWatch Codex Agent.app'
```

Expected: summary reports `result: passed` and `launcher: open`; the Mac Agent process returns.

- [ ] **Step 6: Perform the physical launcher check**

On the watch launcher, verify the Assistant icon has approximately the same visual footprint as adjacent icons, all three stars remain distinct, the glow is soft, and no ring is visible. If the physical display differs materially from the approved preview, adjust only generator constants, regenerate, rebuild, and repeat the hardware check before declaring completion.
