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


def bright_run_in_column(pixels, x, threshold=180):
    ys = [
        y
        for y in range(200)
        if brightness(pixels[y * 200 + x]) >= threshold
    ]
    return min(ys), max(ys) + 1


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
        self.assertGreater(left[1], left[0])
        self.assertGreater(left[2], left[0])
        self.assertGreater(center[2], center[0])
        self.assertGreater(right[2], right[0])
        self.assertGreater(right[0] - left[0], 25)
        self.assertGreater(left[1] - right[1], 30)
        self.assertLess(max(left), 245)
        self.assertLess(max(center), 245)
        self.assertLess(max(right), 245)

        for point in ((5, 100), (194, 100), (100, 5), (100, 194)):
            self.assertLessEqual(brightness(self.pixel(*point)), 12)


if __name__ == "__main__":
    unittest.main()
