import os
import pathlib
import sys
import tempfile
import unittest

from PIL import Image, ImageDraw


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_ROOT))

from pet_converter import (  # noqa: E402
    MAX_DECODED_PIXELS,
    MAX_INPUT_BYTES,
    PetConversionError,
    convert_gif,
)
from pet_package import render_rgba_frames  # noqa: E402


class PetConverterTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tempdir.name)

    def tearDown(self):
        self.tempdir.cleanup()

    def make_gif(
        self,
        canvas=(4, 4),
        durations=(100,),
        disposal=(1,),
        transparent=False,
        frames=None,
        name="fixture.gif",
    ):
        if frames is None:
            frames = []
            colors = ((253, 3, 7, 255), (3, 251, 11, 255), (13, 17, 249, 255))
            for index, color in enumerate(colors[: len(durations)]):
                frame = Image.new("RGBA", canvas, (0, 0, 0, 0) if transparent else (255, 255, 255, 255))
                x = (index + 1) % canvas[0]
                y = (index + 1) % canvas[1]
                ImageDraw.Draw(frame).rectangle((x, y, x, y), fill=color)
                frames.append(frame)
        path = self.root / name
        save_args = {
            "save_all": True,
            "append_images": frames[1:],
            "duration": list(durations),
            "disposal": list(disposal) if len(frames) > 1 else disposal[0],
            "loop": 0,
            "optimize": False,
        }
        if transparent:
            save_args["transparency"] = 0
        frames[0].save(path, format="GIF", **save_args)
        return path

    def make_header_only_gif(self, canvas, declared_frames):
        """Create a valid GIF stream whose images have no materialized pixels."""
        width, height = canvas
        raw = bytearray(b"GIF89a")
        raw.extend(width.to_bytes(2, "little"))
        raw.extend(height.to_bytes(2, "little"))
        raw.extend(b"\x00\x00\x00")  # no global palette
        for _ in range(declared_frames):
            raw.extend(b"\x2c")
            raw.extend(b"\x00\x00\x00\x00")
            raw.extend(width.to_bytes(2, "little"))
            raw.extend(height.to_bytes(2, "little"))
            raw.extend(b"\x00\x02\x01\x00\x00")  # image, minimum LZW, one sub-block, terminator
        raw.extend(b"\x3b")
        path = self.root / f"header-{declared_frames}.gif"
        path.write_bytes(bytes(raw))
        return path

    @staticmethod
    def gif_lzw(indices):
        """Encode GIF index samples with a clear code before every sample."""
        codes = []
        for index in indices:
            codes.extend((4, index))
        codes.append(5)
        packed = 0
        bits = 0
        output = bytearray()
        for code in codes:
            packed |= code << bits
            bits += 3
            while bits >= 8:
                output.append(packed & 0xFF)
                packed >>= 8
                bits -= 8
        if bits:
            output.append(packed)
        return bytes(output)

    def make_partial_local_palette_gif(self):
        """Build a real GIF89a with local palettes and disposal 0/1/2/3."""
        raw = bytearray(b"GIF89a\x04\x00\x04\x00\x00\x00\x00")
        colors = {
            "red": (255, 0, 0),
            "green": (0, 255, 0),
            "blue": (0, 0, 255),
            "yellow": (255, 255, 0),
            "cyan": (0, 255, 255),
        }
        frames = (
            # disposal, left, top, width, height, visible color
            (0, 0, 0, 4, 4, "red"),
            (1, 1, 1, 1, 1, "green"),
            (2, 2, 1, 1, 1, "blue"),
            (3, 3, 1, 1, 1, "yellow"),
            (1, 0, 2, 1, 1, "cyan"),
        )
        for disposal, left, top, width, height, color_name in frames:
            raw.extend((0x21, 0xF9, 0x04, disposal << 2, 10, 0, 0, 0))
            raw.extend((0x2C, left, 0, top, 0, width, 0, height, 0, 0x81))
            raw.extend((0, 0, 0, *colors[color_name], 0, 0, 0, 0, 0, 0))
            encoded = self.gif_lzw((1,) * (width * height))
            raw.extend((2, len(encoded)))
            raw.extend(encoded)
            raw.append(0)
        raw.append(0x3B)
        path = self.root / "partial-local-disposal.gif"
        path.write_bytes(bytes(raw))
        return path

    def assert_error(self, path, code, text):
        with self.assertRaisesRegex(PetConversionError, text) as raised:
            convert_gif(path)
        self.assertEqual(raised.exception.code, code)

    def test_converts_disposal_transparency_to_exact_watch_canvas(self):
        # A missing disposal compositor, alpha threshold, or package-derived
        # preview would change either the durations or the rendered pixels.
        path = self.make_gif(
            canvas=(4, 4),
            durations=(40, 180, 1200),
            disposal=(1, 2, 3),
            transparent=True,
        )
        converted = convert_gif(path)
        self.assertEqual((converted.metadata.width, converted.metadata.height), (144, 156))
        self.assertTrue(all(len(frame) == 144 * 156 * 4 for frame in converted.preview_rgba))
        self.assertEqual([frame.duration_ms for frame in converted.package.frames], [60, 360, 1000])
        self.assertEqual(converted.metadata.loop_duration_ms, 1420)
        self.assertEqual(converted.preview_rgba, render_rgba_frames(converted.package))

    def test_preserves_transparent_pixels_and_disposal_modes(self):
        frames = []
        for color, point in (((255, 0, 0, 255), (0, 0)), ((0, 255, 0, 255), (3, 3)), ((0, 0, 255, 255), (1, 1)), ((255, 255, 0, 255), (2, 2))):
            frame = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
            frame.putpixel(point, color)
            frames.append(frame)
        converted = convert_gif(self.make_gif(durations=(100, 100, 100, 100), disposal=(0, 1, 2, 3), transparent=True, frames=frames))
        transparent = bytes((0, 0, 0, 0))
        self.assertTrue(any(transparent in (frame[offset:offset + 4] for offset in range(0, len(frame), 4)) for frame in converted.preview_rgba))
        self.assertEqual(converted.preview_rgba, render_rgba_frames(converted.package))

    def test_composites_partial_local_palette_frames_for_all_disposals(self):
        converted = convert_gif(self.make_partial_local_palette_gif())
        self.assertEqual(converted.metadata.source_frames, 5)
        self.assertEqual(converted.metadata.output_frames, 5)

        def pixel(frame_index, x, y):
            frame = converted.preview_rgba[frame_index]
            offset = ((6 + y * 36 + 18) * 144 + x * 36 + 18) * 4
            return frame[offset:offset + 4]

        red = bytes((255, 0, 0, 255))
        green = bytes((0, 255, 0, 255))
        blue = bytes((0, 0, 255, 255))
        yellow = bytes((255, 255, 0, 255))
        cyan = bytes((0, 255, 255, 255))
        black = bytes((0, 0, 0, 255))
        self.assertEqual(pixel(0, 0, 0), red)  # disposal 0 base
        self.assertEqual(pixel(1, 1, 1), green)  # disposal 1 remains
        self.assertEqual(pixel(2, 2, 1), blue)  # disposal 2 frame itself
        self.assertEqual(pixel(3, 1, 1), green)
        self.assertEqual(pixel(3, 2, 1), black)  # disposal 2 restored GIF background
        self.assertEqual(pixel(3, 3, 1), yellow)  # disposal 3 frame itself
        self.assertEqual(pixel(4, 3, 1), red)  # disposal 3 restored prior canvas
        self.assertEqual(pixel(4, 0, 2), cyan)

    def test_accepts_exact_cumulative_pixel_boundary_and_rejects_next_frame(self):
        # 61 * 1024^2 is 63,963,136 and is valid; adding one exceeds 64,000,000.
        accepted = self.make_header_only_gif((1024, 1024), 61)
        rejected = self.make_header_only_gif((1024, 1024), 62)
        self.assertEqual(1024 * 1024 * 61, 63_963_136)
        self.assertLessEqual(1024 * 1024 * 61, MAX_DECODED_PIXELS)
        self.assert_error(rejected, "decoded_pixel_budget", "decoded pixel budget")
        # Inspection, not Pillow decoding, accepts the boundary stream.
        from pet_converter import _inspect_gif_blocks, _validate_inspection
        _validate_inspection(_inspect_gif_blocks(accepted.read_bytes()))

    def test_rejects_cumulative_pixel_bomb_before_frame_materialization(self):
        path = self.make_header_only_gif(canvas=(1024, 1024), declared_frames=62)
        self.assert_error(path, "decoded_pixel_budget", "decoded pixel budget")

    def test_accepts_240_source_frames_at_inspection_boundary(self):
        from pet_converter import _inspect_gif_blocks, _validate_inspection

        accepted = self.make_header_only_gif((1, 1), 240)
        rejected = self.make_header_only_gif((1, 1), 241)
        _validate_inspection(_inspect_gif_blocks(accepted.read_bytes()))
        with self.assertRaisesRegex(PetConversionError, "more than 240"):
            _validate_inspection(_inspect_gif_blocks(rejected.read_bytes()))

    def test_rejects_invalid_signatures_truncation_and_size_limit(self):
        for signature in (b"GIF00a", b"NOTGIF"):
            path = self.root / f"{signature!r}.gif"
            path.write_bytes(signature + b"\x00" * 16)
            self.assert_error(path, "invalid_gif", "GIF signature")
        truncated = self.root / "truncated.gif"
        truncated.write_bytes(b"GIF89a\x04\x00")
        self.assert_error(truncated, "truncated_gif", "truncated")
        oversized = self.root / "oversized.gif"
        oversized.write_bytes(b"GIF89a" + bytes(MAX_INPUT_BYTES - 5) + b"x")
        self.assert_error(oversized, "input_too_large", "10 MiB")

    def test_accepts_gif87a_signature_and_local_palette_partial_frames(self):
        path = self.make_gif(canvas=(5, 3), durations=(100,), disposal=(1,), transparent=False)
        source = bytearray(path.read_bytes())
        source[:6] = b"GIF87a"
        path.write_bytes(source)
        converted = convert_gif(path)
        self.assertEqual(converted.metadata.source_frames, 1)
        self.assertEqual(converted.metadata.output_frames, 1)
        self.assertEqual(converted.preview_rgba, render_rgba_frames(converted.package))

    def test_merges_duplicate_delays_and_samples_more_than_24_frames(self):
        from pet_converter import _merge_identical_frames

        # The compositor supplies complete RGBA canvases; identical consecutive
        # canvases must accumulate their raw GIF delays before delay clamping.
        collapsed = _merge_identical_frames(((b"a", 40), (b"a", 40), (b"a", 100)))
        self.assertEqual(collapsed, ((b"a", 180),))

        frames = [Image.new("RGBA", (2, 2), ((index * 37) % 255, (index * 71) % 255, (index * 113) % 255, 255)) for index in range(30)]
        sampled = convert_gif(self.make_gif(durations=(100,) * 30, disposal=(1,) * 30, frames=frames, name="many.gif"))
        self.assertEqual(sampled.metadata.source_frames, 30)
        self.assertLessEqual(sampled.metadata.output_frames, 24)
        self.assertEqual(sampled.metadata.loop_duration_ms, 3000)

    def test_aspect_fit_uses_nearest_neighbor_pixel_blocks(self):
        source = Image.new("P", (2, 1), 0)
        source.putpalette([0, 0, 0, 255, 0, 0, 0, 255, 0] + [0] * 759)
        source.putdata([1, 2])
        converted = convert_gif(self.make_gif(durations=(100,), disposal=(1,), transparent=True, frames=[source], name="pixels.gif"))
        frame = converted.preview_rgba[0]
        pixel = lambda x, y: frame[(y * 144 + x) * 4:(y * 144 + x + 1) * 4]
        self.assertEqual(pixel(0, 42), bytes((255, 0, 0, 255)))
        self.assertEqual(pixel(71, 113), bytes((255, 0, 0, 255)))
        self.assertEqual(pixel(72, 42), bytes((0, 255, 0, 255)))
        self.assertEqual(pixel(143, 113), bytes((0, 255, 0, 255)))
        self.assertEqual(pixel(0, 41), bytes((0, 0, 0, 0)))

    def test_rejects_package_that_cannot_fit_watch_limit(self):
        frames = []
        for index in range(24):
            data = os.urandom(144 * 156 * 4)
            frames.append(Image.frombytes("RGBA", (144, 156), data))
        path = self.make_gif(durations=(100,) * 24, disposal=(1,) * 24, frames=frames, name="package-too-large.gif")
        self.assert_error(path, "package_too_large", "package")


if __name__ == "__main__":
    unittest.main()
