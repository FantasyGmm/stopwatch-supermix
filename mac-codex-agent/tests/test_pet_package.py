import hashlib
import pathlib
import struct
import sys
import unittest


AGENT_ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AGENT_ROOT))

from pet_package import (  # noqa: E402
    HEADER,
    FRAME_ENTRY,
    PetFrame,
    PetPackageError,
    encode_package,
    packbits_decode,
    packbits_encode,
    parse_package,
    render_rgba_frames,
)


PIXELS = 144 * 156


class PetPackageTests(unittest.TestCase):
    def minimal_frames(self):
        return (
            PetFrame(180, bytes([0]) * PIXELS),
            PetFrame(240, bytes([1]) * PIXELS),
        )

    def test_cxp1_round_trip_and_fixed_header(self):
        package = encode_package(144, 156, (0xFFFF,), 1, self.minimal_frames())
        self.assertEqual(package.encoded[:4], b"CXP1")
        self.assertEqual(len(package.sha256), 32)
        self.assertEqual(HEADER.size, 72)
        parsed = parse_package(package.encoded)
        self.assertEqual(parsed.frames, self.minimal_frames())
        self.assertEqual(parsed.palette_rgb565, (0xFFFF,))
        self.assertEqual(parsed.transparent_index, 1)
        self.assertEqual(parsed.sha256, hashlib.sha256(package.encoded[:40] + bytes(32) + package.encoded[72:]).digest())

    def test_packbits_round_trip_and_deterministic_run_preference(self):
        data = bytes([7]) * 3 + bytes(range(128)) + bytes([9]) * 130
        encoded = packbits_encode(data)
        self.assertEqual(encoded, packbits_encode(data))
        self.assertEqual(packbits_decode(encoded, len(data)), data)
        self.assertEqual(packbits_encode(bytes([4]) * 3), bytes([0xFF, 4]))
        self.assertEqual(packbits_decode(packbits_encode(bytes(range(129))), 129), bytes(range(129)))

    def test_packbits_rejects_overrun_and_trailing_output(self):
        with self.assertRaises(PetPackageError):
            packbits_decode(bytes([0xFE, 0x04]), expected_size=2)
        with self.assertRaises(PetPackageError):
            packbits_decode(bytes([0x00, 0x04, 0x80]), expected_size=1)
        with self.assertRaises(PetPackageError):
            packbits_decode(bytes([0x80]), expected_size=0)

    def test_packbits_rejects_invalid_expected_size(self):
        for expected in (-1, 0x10000000):
            with self.assertRaises(PetPackageError):
                packbits_decode(b"", expected)

    def test_validation_rejects_bounds_and_bad_frame_data(self):
        valid = self.minimal_frames()
        cases = [
            (143, 156, (0xFFFF,), 1, valid),
            (144, 157, (0xFFFF,), 1, valid),
            (144, 156, (), 0, valid),
            (144, 156, (0xFFFF,), 0, valid),
            (144, 156, (0xFFFF,), 1, (PetFrame(59, bytes(PIXELS)),)),
            (144, 156, (0xFFFF,), 1, (PetFrame(1001, bytes(PIXELS)),)),
            (144, 156, (0xFFFF,), 1, ()),
            (144, 156, (0xFFFF,), 1, tuple(PetFrame(100, bytes(PIXELS)) for _ in range(25))),
            (144, 156, (0xFFFF,), 1, (PetFrame(100, bytes(PIXELS - 1)),)),
            (144, 156, (0xFFFF, 0x10000), 2, valid),
            (144, 156, (0xFFFF,), 1, (PetFrame(100, bytes([2]) * PIXELS),)),
        ]
        for args in cases:
            with self.subTest(args=args[:4]):
                with self.assertRaises(PetPackageError):
                    encode_package(*args)

    def test_parse_rejects_digest_offsets_reserved_and_payload_errors(self):
        package = encode_package(144, 156, (0xFFFF,), 1, self.minimal_frames())
        encoded = bytearray(package.encoded)
        encoded[-1] ^= 1
        with self.assertRaises(PetPackageError):
            parse_package(bytes(encoded))

        # Frame reserved field must be zero.
        encoded = bytearray(package.encoded)
        struct.pack_into("<H", encoded, HEADER.size + 2, 1)
        with self.assertRaises(PetPackageError):
            parse_package(bytes(encoded))

        def redigest(raw):
            raw[40:72] = bytes(32)
            raw[40:72] = hashlib.sha256(raw).digest()

        # Total length and payload offset/spans are checked before slicing,
        # even when the attacker has recomputed the package digest.
        for field_offset, value in ((32, len(encoded) + 1), (28, 1)):
            malformed = bytearray(package.encoded)
            struct.pack_into("<I", malformed, field_offset, value)
            redigest(malformed)
            with self.assertRaises(PetPackageError):
                parse_package(bytes(malformed))

        malformed = bytearray(package.encoded)
        struct.pack_into("<I", malformed, HEADER.size + FRAME_ENTRY.size + 4, 98)
        redigest(malformed)
        with self.assertRaises(PetPackageError):
            parse_package(bytes(malformed))

    def test_render_reconstructs_rgba_from_package(self):
        package = encode_package(
            144,
            156,
            (0xF800, 0x07E0),
            2,
            (PetFrame(100, bytes([0, 1]) * (PIXELS // 2)),),
        )
        frame = render_rgba_frames(package)[0]
        self.assertEqual(len(frame), PIXELS * 4)
        self.assertEqual(frame[:4], bytes((255, 0, 0, 255)))
        self.assertEqual(frame[4:8], bytes((0, 255, 0, 255)))

        transparent = encode_package(
            144,
            156,
            (0xF800,),
            1,
            (PetFrame(100, bytes([1]) * PIXELS),),
        )
        self.assertEqual(render_rgba_frames(transparent)[0][:4], bytes((0, 0, 0, 0)))

    def test_fixture_is_complete_lowercase_canonical_hex(self):
        package = encode_package(144, 156, (0xFFFF,), 1, self.minimal_frames())
        fixture = pathlib.Path(__file__).resolve().parents[2] / "tests/fixtures/codex_pet/cxp1_minimal.hex"
        self.assertEqual(fixture.read_text(encoding="ascii"), package.encoded.hex() + "\n")


if __name__ == "__main__":
    unittest.main()
