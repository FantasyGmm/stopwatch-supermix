import hashlib
import re
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASSET_DIR = ROOT / "main/assets/images"
ICON_HASHES = {
    "assistant": "876473fd2931b364cf274cb036dcaabf2a8533d64c66c1252806ea635c5d6412",
    "badge": "ebc954d749256efe1d8b95d769b7106a5c0cbdca320a3ae7d13e27b958ade965",
    "clock": "8eb478b4a5aa94e6e9ac4a189e01ed910e1a193eb74e59d4b508c0c5bf9a514f",
    "codex": "8a6ce5cd85f770450628d9b68b10da546ef13582508e954876603558f3433287",
    "fft": "603e615cc2acf9f53168cd084ce638591a093fb800331627f6eabce800eb69a5",
    "imu": "d7fd191fff472ee6bc53c05ca232af261d4a7612cbad38b38cefe2cf013b7f53",
    "setup": "462ac67c8aedbf8809a35304442ee270e7a8c9b544855005c77ffeca84401c76",
}


def load_asset(name):
    source = (ASSET_DIR / f"icon_{name}.c").read_text()
    body = source.split(f"icon_{name}_map[] = {{", 1)[1].split("};", 1)[0]
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body))
    return source, data


def lz4_decompress_block(data, output_size):
    output = bytearray()
    offset = 0
    while offset < len(data):
        token = data[offset]
        offset += 1

        literal_length = token >> 4
        if literal_length == 15:
            while True:
                value = data[offset]
                offset += 1
                literal_length += value
                if value != 255:
                    break
        output.extend(data[offset : offset + literal_length])
        offset += literal_length
        if offset == len(data):
            break

        match_offset = data[offset] | (data[offset + 1] << 8)
        offset += 2
        if match_offset == 0 or match_offset > len(output):
            raise ValueError("invalid LZ4 match offset")

        match_length = (token & 0x0F) + 4
        if (token & 0x0F) == 15:
            while True:
                value = data[offset]
                offset += 1
                match_length += value
                if value != 255:
                    break
        match_start = len(output) - match_offset
        for index in range(match_length):
            output.append(output[match_start + index])

    if len(output) != output_size:
        raise ValueError(f"decoded {len(output)} bytes, expected {output_size}")
    return bytes(output)


class LauncherIconCompressionTests(unittest.TestCase):
    def test_lz4_decoder_is_enabled_in_defaults_and_active_config(self):
        for filename in ("sdkconfig.defaults", "sdkconfig"):
            config = (ROOT / filename).read_text()
            self.assertIn("CONFIG_LV_BIN_DECODER_RAM_LOAD=y", config)
            self.assertIn("CONFIG_LV_USE_LZ4=y", config)
            self.assertIn("CONFIG_LV_USE_LZ4_INTERNAL=y", config)

    def test_assets_are_lossless_lz4_and_keep_the_launcher_contract(self):
        total_size = 0
        for name, expected_hash in ICON_HASHES.items():
            with self.subTest(icon=name):
                source, data = load_asset(name)
                self.assertIn("LV_IMAGE_FLAGS_COMPRESSED", source)
                self.assertIn(".header.cf    = LV_COLOR_FORMAT_RGB565", source)
                self.assertIn(".header.w     = 200", source)
                self.assertIn(".header.h     = 200", source)

                method, compressed_size, decompressed_size = struct.unpack("<III", data[:12])
                self.assertEqual(method, 2)
                self.assertEqual(compressed_size, len(data) - 12)
                self.assertEqual(decompressed_size, 80000)
                decoded = lz4_decompress_block(data[12:], decompressed_size)
                self.assertEqual(hashlib.sha256(decoded).hexdigest(), expected_hash)
                total_size += len(data)

        # The original seven RGB565 maps occupy 560,000 bytes. Keep enough
        # margin that adding the decoder still yields a material firmware win.
        self.assertLessEqual(total_size, 120000)


if __name__ == "__main__":
    unittest.main()
