import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.compress_launcher_icons import extract_pixels


ROOT = Path(__file__).resolve().parents[2]
ASSET = ROOT / "main/assets/images/icon_assistant.c"
GENERATOR = ROOT / "scripts/generate_assistant_icon.py"


def load_pixels():
    source = ASSET.read_text()
    raw = extract_pixels(ASSET, "assistant")
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


def components(pixels, threshold):
    remaining = {
        (index % 200, index // 200)
        for index, pixel in enumerate(pixels)
        if brightness(pixel) > threshold
    }
    result = []
    while remaining:
        seed = remaining.pop()
        stack = [seed]
        points = [seed]
        while stack:
            x, y = stack.pop()
            for neighbor in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    stack.append(neighbor)
                    points.append(neighbor)
        if len(points) > 3:
            result.append(
                (
                    len(points),
                    (
                        min(x for x, _ in points),
                        min(y for _, y in points),
                        max(x for x, _ in points) + 1,
                        max(y for _, y in points) + 1,
                    ),
                )
            )
    return sorted(result, reverse=True)


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
        self.assertIn(".header.flags = LV_IMAGE_FLAGS_COMPRESSED", self.source)
        self.assertIn(".data_size    = sizeof(icon_assistant_map)", self.source)
        self.assertEqual(len(self.raw), 80000)

    def test_matches_launcher_family_visual_footprint(self):
        # Threshold 24 tracks the wide glow that remains visible after RGB565
        # quantization, while threshold 128 isolates the solid twin stars.
        halo = bounds(self.pixels, 24)
        halo_long_axis = max(halo[2] - halo[0], halo[3] - halo[1])
        self.assertGreaterEqual(halo_long_axis, 185)
        self.assertLessEqual(halo_long_axis, 200)

        strong = bounds(self.pixels, 128)
        strong_long_axis = max(strong[2] - strong[0], strong[3] - strong[1])
        self.assertGreaterEqual(strong_long_axis, 165)
        self.assertLessEqual(strong_long_axis, 170)

    def test_has_approved_blue_violet_and_mint_twin_stars(self):
        solid_components = components(self.pixels, 128)
        self.assertEqual(len(solid_components), 2)
        self.assertEqual(solid_components[0][1], (23, 36, 147, 160))
        self.assertEqual(solid_components[1][1], (116, 109, 190, 183))

        main = self.pixel(85, 98)
        self.assertGreater(main[2] - main[1], 100)
        self.assertGreater(main[1] - main[0], 20)

        small = self.pixel(153, 139)
        self.assertGreater(small[1] - small[0], 100)
        self.assertGreater(small[2] - small[0], 100)

    def test_keeps_visible_wide_glow_and_crisp_main_edge(self):
        glow = self.pixel(14, 98)
        self.assertGreaterEqual(brightness(glow), 28)
        self.assertGreater(glow[2], glow[0])

        outside = brightness(self.pixel(22, 98))
        edge = brightness(self.pixel(23, 98))
        self.assertGreaterEqual(edge - outside, 90)

    def test_checked_in_asset_matches_generator(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = Path(directory) / "icon_assistant.c"
            subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR),
                    "--output-c",
                    str(generated),
                ],
                check=True,
            )
            self.assertEqual(generated.read_bytes(), ASSET.read_bytes())


if __name__ == "__main__":
    unittest.main()
