import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from protocol import encode_message


class EncodeMessageTests(unittest.TestCase):
    def test_splits_payload_and_numbers_fragments(self):
        payload = b'{"codex_model":"gpt-5"}'

        frames = encode_message(
            payload,
            message_type=1,
            sequence=0x1234,
            max_write_size=13,
        )

        self.assertEqual(
            [frame[:6] for frame in frames],
            [
                bytes([1, 1, 0x34, 0x12, 0, 4]),
                bytes([1, 1, 0x34, 0x12, 1, 4]),
                bytes([1, 1, 0x34, 0x12, 2, 4]),
                bytes([1, 1, 0x34, 0x12, 3, 4]),
            ],
        )
        self.assertEqual(b"".join(frame[6:] for frame in frames), payload)


if __name__ == "__main__":
    unittest.main()
