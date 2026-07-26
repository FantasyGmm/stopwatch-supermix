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
        strong_long_axis = max(strong[2] - strong[0], strong[3] - strong[1])
        self.assertGreaterEqual(strong_long_axis, 145)
        self.assertLessEqual(strong_long_axis, 165)

    def test_has_approved_five_bar_aurora_waveform(self):
        expected_runs = {
            37: (77, 123),
            69: (56, 144),
            100: (32, 168),
            131: (56, 144),
            163: (77, 123),
        }
        for x, expected in expected_runs.items():
            actual = bright_run_in_column(self.pixels, x)
            self.assertLessEqual(abs(actual[0] - expected[0]), 3)
            self.assertLessEqual(abs(actual[1] - expected[1]), 3)

        bar_middles = [self.pixel(x, 100) for x in (37, 69, 100, 131, 163)]
        for pixel in bar_middles:
            self.assertGreater(pixel[1], pixel[0])
            self.assertGreater(pixel[2], pixel[0])
            self.assertLess(max(pixel), 245)
        for channel in range(3):
            values = [pixel[channel] for pixel in bar_middles]
            self.assertLessEqual(max(values) - min(values), 10)

        top = self.pixel(100, 45)
        bottom = self.pixel(100, 155)
        self.assertGreater(top[1] - bottom[1], 35)
        self.assertGreater(bottom[0] - top[0], 12)

        edge_halo = [
            self.pixel(20, 100),
            self.pixel(180, 100),
            self.pixel(100, 25),
            self.pixel(100, 175),
        ]
        for pixel in edge_halo:
            self.assertGreaterEqual(brightness(pixel), 14)
            self.assertGreater(pixel[2], pixel[0])

        glow_left = self.pixel(50, 70)
        glow_right = self.pixel(150, 70)
        for pixel in (glow_left, glow_right):
            self.assertGreaterEqual(brightness(pixel), 28)
            self.assertGreater(pixel[2], pixel[0])

        for point in ((5, 100), (194, 100), (100, 5), (100, 194)):
            self.assertLessEqual(brightness(self.pixel(*point)), 12)


if __name__ == "__main__":
    unittest.main()
