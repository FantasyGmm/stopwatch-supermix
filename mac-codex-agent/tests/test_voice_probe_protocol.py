import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_probe_protocol import (
    DATA,
    ERROR,
    MICROPHONE_ADPCM,
    MICROPHONE_ADPCM_AES_CCM,
    START_ACK,
    STOP_SUMMARY,
    SYNTHETIC,
    StopSummary,
    decode_ima_adpcm_block,
    decode_error,
    decode_notification,
    decode_start_ack,
    encode_start,
    encode_stop,
    expected_synthetic_payload,
    notification_aad,
    pack_notification,
)


class VoiceProbeProtocolTests(unittest.TestCase):
    def test_control_commands_match_firmware_layout(self):
        self.assertEqual(
            encode_start(0x1234, SYNTHETIC, 8192, 60000),
            bytes.fromhex("0101010034120020000060ea0000"),
        )
        self.assertEqual(
            encode_stop(0x1234),
            bytes.fromhex("0102010034120000000000000000"),
        )
        self.assertEqual(
            encode_start(0x1234, MICROPHONE_ADPCM_AES_CCM, 8192, 60000),
            bytes.fromhex("0101030034120020000060ea0000"),
        )

    def test_rejects_control_values_forbidden_by_firmware(self):
        invalid_start_commands = (
            (0, SYNTHETIC, 8192, 60000),
            (0x10000, SYNTHETIC, 8192, 60000),
            (1, 0, 8192, 60000),
            (1, 4, 8192, 60000),
            (1, SYNTHETIC, 8192, 999),
            (1, SYNTHETIC, 8192, 120001),
            (1, SYNTHETIC, 1023, 60000),
            (1, SYNTHETIC, 32769, 60000),
            (1, MICROPHONE_ADPCM, 8191, 60000),
            (1, MICROPHONE_ADPCM, 8193, 60000),
            (1, MICROPHONE_ADPCM_AES_CCM, 8191, 60000),
            (1, MICROPHONE_ADPCM_AES_CCM, 8193, 60000),
        )
        for command in invalid_start_commands:
            with self.subTest(command=command), self.assertRaises(ValueError):
                encode_start(*command)

        for session in (0, 0x10000):
            with self.subTest(session=session), self.assertRaises(ValueError):
                encode_stop(session)

    def test_decodes_notification_and_rejects_bad_payload_length(self):
        raw = struct.pack("<BBHIIH", 1, DATA, 7, 3, 100, 3) + b"abc"
        packet = decode_notification(raw)
        self.assertEqual((packet.session, packet.sequence, packet.payload), (7, 3, b"abc"))
        with self.assertRaises(ValueError):
            decode_notification(raw[:-1])
        with self.assertRaises(ValueError):
            decode_notification(bytes([2]) + raw[1:])
        with self.assertRaises(ValueError):
            decode_notification(bytes([1, 0]) + raw[2:])
        self.assertEqual(notification_aad(packet), raw[:14])

    def test_synthetic_pattern_uses_packet_sequence(self):
        self.assertEqual(expected_synthetic_payload(0x102, 4), b"\x02\x03\x04\x05")

    def test_decodes_known_ima_blocks(self):
        self.assertEqual(
            decode_ima_adpcm_block(b"\x00\x00\x00\x00\x00\x00", sample_count=5),
            [0] * 5,
        )
        self.assertEqual(
            decode_ima_adpcm_block(b"\x00\x00\x00\x00\x04", sample_count=2),
            [0, 7],
        )

    def test_decodes_fixed_payloads(self):
        ack = decode_start_ack(struct.pack("<HBBI", 256, MICROPHONE_ADPCM, 0, 8192))
        self.assertEqual((ack.negotiated_mtu, ack.mode, ack.target_bps), (256, 2, 8192))
        self.assertEqual(decode_error(struct.pack("<HH", 2, 0x1234)), (2, 0x1234))

        summary = StopSummary(10, 9, 1, 2, 60000, 8192, 960000, 256)
        self.assertEqual(len(summary.pack()), 30)
        packet = decode_notification(
            pack_notification(STOP_SUMMARY, 7, 3, 100, summary.pack())
        )
        self.assertEqual(StopSummary.from_payload(packet.payload), summary)

        encrypted_summary = StopSummary(
            10,
            9,
            1,
            2,
            60000,
            1720,
            3200,
            256,
            crypto_packets=10,
            crypto_failures=0,
            crypto_total_us=500,
            crypto_max_us=75,
            crypto_p99_us=70,
        )
        encrypted_payload = encrypted_summary.pack(MICROPHONE_ADPCM_AES_CCM)
        self.assertEqual(len(encrypted_payload), 50)
        self.assertEqual(
            StopSummary.from_payload(
                encrypted_payload, mode=MICROPHONE_ADPCM_AES_CCM
            ),
            encrypted_summary,
        )
        with self.assertRaises(ValueError):
            StopSummary.from_payload(
                summary.pack(), mode=MICROPHONE_ADPCM_AES_CCM
            )

        for decoder, payload in ((decode_start_ack, b"\x00"), (decode_error, b"\x00")):
            with self.assertRaises(ValueError):
                decoder(payload)

    def test_adpcm_uses_low_nibble_first_and_exact_size(self):
        self.assertEqual(
            decode_ima_adpcm_block(b"\x00\x00\x00\x00\x40", sample_count=2),
            [0, 0],
        )
        with self.assertRaises(ValueError):
            decode_ima_adpcm_block(b"\x00\x00\x00\x00", sample_count=2)
        with self.assertRaises(ValueError):
            decode_ima_adpcm_block(b"\x00\x00\x00\x00\x00", sample_count=1)


if __name__ == "__main__":
    unittest.main()
