import asyncio
import contextlib
import io
import itertools
import json
import pathlib
import struct
import sys
import tempfile
import unittest
import wave
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from ble_voice_probe import Profile, VoiceProbeClient, _build_parser
from voice_probe_crypto import derive_nonce, encrypt, encrypt_synthetic
from voice_probe_protocol import (
    DATA,
    ERROR,
    MICROPHONE_ADPCM,
    MICROPHONE_ADPCM_AES_CCM,
    START_ACK,
    STOP_SUMMARY,
    SYNTHETIC,
    SYNTHETIC_AES_CCM,
    StopSummary,
    expected_synthetic_payload,
    pack_notification,
)


class FakeClient:
    def __init__(self, events):
        self.events = events
        self.is_connected = False
        self.callback = None
        self.disconnect_callback = None
        self.operations = []

    async def connect(self):
        self.operations.append("connect")
        self.is_connected = True

    async def start_notify(self, uuid, callback):
        self.operations.append(("start_notify", uuid))
        self.callback = callback

    async def write_gatt_char(self, uuid, payload, response):
        self.operations.append(("write", uuid, payload[1], response))
        if payload[1] == 1:
            for event in self.events:
                self.callback(None, bytearray(event))

    async def stop_notify(self, uuid):
        self.operations.append(("stop_notify", uuid))

    async def disconnect(self):
        self.operations.append("disconnect")
        self.is_connected = False
        if self.disconnect_callback is not None:
            self.disconnect_callback(self)


class StalledCleanupClient(FakeClient):
    async def write_gatt_char(self, uuid, payload, response):
        await super().write_gatt_char(uuid, payload, response)
        if payload[1] == 2:
            await asyncio.sleep(0.05)

    async def stop_notify(self, uuid):
        self.operations.append(("stop_notify", uuid))
        await asyncio.sleep(0.05)

    async def disconnect(self):
        self.operations.append("disconnect")
        await asyncio.sleep(0.05)
        self.is_connected = False


class CancelOnStartClient(FakeClient):
    async def write_gatt_char(self, uuid, payload, response):
        if payload[1] == 1:
            self.operations.append(("write", uuid, payload[1], response))
            raise asyncio.CancelledError
        await super().write_gatt_char(uuid, payload, response)


class DeviceDisconnectClient(FakeClient):
    async def write_gatt_char(self, uuid, payload, response):
        self.operations.append(("write", uuid, payload[1], response))
        if payload[1] == 1:
            for event in self.events:
                self.callback(None, bytearray(event))
            self.is_connected = False
            self.disconnect_callback(self)


def start_ack(session, mode, target_bps, mtu=256, sequence=0):
    return pack_notification(
        START_ACK,
        session,
        sequence,
        0,
        struct.pack("<HBBI", mtu, mode, 0, target_bps),
    )


def watch_error(session, code, detail=0, sequence=0):
    return pack_notification(
        ERROR,
        session,
        sequence,
        0,
        struct.pack("<HH", code, detail),
    )


def synthetic_events(session=7, target_bps=8192, packets=35, payload_bytes=None):
    if payload_bytes is None:
        payload_bytes = target_bps
    base_size, remainder = divmod(payload_bytes, packets)
    sizes = [base_size] * packets
    sizes[-1] += remainder
    events = [start_ack(session, SYNTHETIC, target_bps)]
    for index, size in enumerate(sizes):
        sequence = index + 1
        timestamp_ms = round(index * 1000 / packets)
        events.append(
            pack_notification(
                DATA,
                session,
                sequence,
                timestamp_ms,
                expected_synthetic_payload(sequence, size),
            )
        )
    events.append(
        pack_notification(
            STOP_SUMMARY,
            session,
            packets + 1,
            1000,
            StopSummary(
                packets,
                packets,
                0,
                0,
                1000,
                payload_bytes,
                0,
                256,
            ).pack(),
        )
    )
    return events


def encrypted_microphone_events(session=7, packets=50):
    events = [start_ack(session, MICROPHONE_ADPCM_AES_CCM, 8192)]
    for index in range(packets):
        sequence = index + 1
        watch_ms = index * 20
        aad = struct.pack("<BBHIIH", 1, DATA, session, sequence, watch_ms, 172)
        ciphertext = encrypt(
            aad,
            bytes(164),
            derive_nonce(session, sequence, watch_ms),
        )
        events.append(
            pack_notification(DATA, session, sequence, watch_ms, ciphertext)
        )
    events.append(
        pack_notification(
            STOP_SUMMARY,
            session,
            packets + 1,
            1000,
            StopSummary(
                packets,
                packets,
                0,
                0,
                1000,
                packets * 172,
                packets * 320,
                256,
                crypto_packets=packets,
                crypto_failures=0,
                crypto_total_us=packets * 20,
                crypto_max_us=20,
                crypto_p99_us=20,
            ).pack(MICROPHONE_ADPCM_AES_CCM),
        )
    )
    return events


def encrypted_synthetic_events(
    session=7,
    target_bps=8192,
    corrupt_plaintext=False,
):
    packets = 50
    base_size, remainder = divmod(target_bps, packets)
    plaintext_sizes = [base_size] * packets
    plaintext_sizes[-1] += remainder
    events = [start_ack(session, SYNTHETIC_AES_CCM, target_bps)]
    wire_bytes = 0
    for index, plaintext_size in enumerate(plaintext_sizes):
        sequence = index + 1
        watch_ms = index * 20
        plaintext = expected_synthetic_payload(sequence, plaintext_size)
        if corrupt_plaintext and index == 0:
            plaintext = bytes([plaintext[0] ^ 0x01]) + plaintext[1:]
        aad = struct.pack(
            "<BBHIIH",
            1,
            DATA,
            session,
            sequence,
            watch_ms,
            plaintext_size + 8,
        )
        ciphertext = encrypt_synthetic(
            aad,
            plaintext,
            derive_nonce(session, sequence, watch_ms),
        )
        wire_bytes += len(ciphertext)
        events.append(
            pack_notification(DATA, session, sequence, watch_ms, ciphertext)
        )
    events.append(
        pack_notification(
            STOP_SUMMARY,
            session,
            packets + 1,
            1000,
            StopSummary(
                packets,
                packets,
                0,
                0,
                1000,
                wire_bytes,
                0,
                256,
                crypto_packets=packets,
                crypto_failures=0,
                crypto_total_us=packets * 20,
                crypto_max_us=20,
                crypto_p99_us=20,
                generated_plaintext_bytes=target_bps,
            ).pack(SYNTHETIC_AES_CCM),
        )
    )
    return events


def make_runner(fake, clock_values=None, agent_running=lambda: False):
    if clock_values is None:
        clock_values = itertools.count(start=0, step=29_000_000)

    async def find_device(_name, _service):
        return object()

    return VoiceProbeClient(
        find_device=find_device,
        client_factory=lambda _device, disconnect: _attach_disconnect_callback(
            fake, disconnect
        ),
        clock_ns=lambda: next(clock_values),
        agent_running=agent_running,
    )


def _attach_disconnect_callback(fake, callback):
    fake.disconnect_callback = callback
    return fake


class BleVoiceProbeTests(unittest.TestCase):
    def run_probe(self, runner, profile, directory, wav_path=None):
        return asyncio.run(
            runner.run(
                profile,
                report_path=directory / "report.json",
                wav_path=wav_path,
                firmware_revision="test-revision",
            )
        )

    def test_subscribes_collects_summary_and_disconnects(self):
        fake = FakeClient(synthetic_events())
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 0)
        self.assertTrue(saved["metrics"]["automated_pass"])
        self.assertEqual(saved["firmware_revision"], "test-revision")
        self.assertLess(
            fake.operations.index(("start_notify", runner.voice_tx_uuid)),
            next(
                index
                for index, operation in enumerate(fake.operations)
                if isinstance(operation, tuple) and operation[0] == "write"
            ),
        )
        self.assertEqual(fake.operations[-1], "disconnect")
        self.assertLess(
            fake.operations.index(("stop_notify", runner.voice_tx_uuid)),
            fake.operations.index("disconnect"),
        )

    def test_rejects_nonzero_start_ack_sequence(self):
        fake = FakeClient([start_ack(7, SYNTHETIC, 8192, sequence=1)])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIn("StartAck sequence", saved["exception"])
        self.assertIn("expected 0", saved["exception"])

    def test_reports_matching_watch_error_before_start_ack(self):
        fake = FakeClient([watch_error(7, code=6, detail=44)])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertEqual(
            saved["exception"], "watch reported probe error 6 (detail 44)"
        )
        self.assertFalse(saved["metrics"]["automated_pass"])

    def test_rejects_wrong_error_sequence_before_start_ack(self):
        fake = FakeClient([watch_error(7, code=6, sequence=1)])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIn("Error sequence 1", saved["exception"])
        self.assertIn("expected 0", saved["exception"])
        self.assertNotIn("watch reported probe error", saved["exception"])

    def test_still_rejects_data_and_summary_before_start_ack(self):
        premature_notifications = (
            pack_notification(DATA, 7, 0, 0, b"payload"),
            pack_notification(
                STOP_SUMMARY,
                7,
                0,
                0,
                StopSummary(0, 0, 0, 0, 1, 0, 0, 256).pack(),
            ),
        )
        for notification in premature_notifications:
            with self.subTest(kind=notification[1]):
                fake = FakeClient([notification])
                runner = make_runner(fake)
                with tempfile.TemporaryDirectory() as temporary:
                    directory = pathlib.Path(temporary)
                    exit_code = self.run_probe(
                        runner,
                        Profile(
                            session=7,
                            mode=SYNTHETIC,
                            target_bps=8192,
                            duration_ms=1000,
                        ),
                        directory,
                    )
                    saved = json.loads((directory / "report.json").read_text())

                self.assertEqual(exit_code, 1)
                self.assertIn(
                    "before matching start acknowledgment", saved["exception"]
                )

    def test_rejects_data_sequence_regression(self):
        fake = FakeClient(
            [
                start_ack(7, SYNTHETIC, 8192),
                pack_notification(
                    DATA, 7, 1, 0, expected_synthetic_payload(1, 4096)
                ),
                pack_notification(
                    DATA, 7, 1, 20, expected_synthetic_payload(1, 4096)
                ),
            ]
        )
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIn("Data sequence", saved["exception"])
        self.assertIn("expected 2", saved["exception"])

    def test_rejects_stop_summary_sequence_gap(self):
        fake = FakeClient(
            [
                start_ack(7, SYNTHETIC, 8192),
                pack_notification(
                    DATA, 7, 1, 0, expected_synthetic_payload(1, 8192)
                ),
                pack_notification(
                    STOP_SUMMARY,
                    7,
                    3,
                    1000,
                    StopSummary(1, 1, 0, 0, 1000, 8192, 0, 256).pack(),
                ),
            ]
        )
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIn("StopSummary sequence", saved["exception"])
        self.assertIn("expected 2", saved["exception"])

    def test_timeout_writes_stop_and_persists_partial_report(self):
        fake = FakeClient([start_ack(7, SYNTHETIC, 8192)])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            with mock.patch(
                "ble_voice_probe.time.monotonic_ns",
                side_effect=(0, 11_000_000_001),
            ):
                exit_code = self.run_probe(
                    runner,
                    Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                    directory,
                )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIn("timed out", saved["exception"])
        self.assertIn(("write", runner.voice_control_uuid, 2, True), fake.operations)
        self.assertEqual(fake.operations[-2], ("stop_notify", runner.voice_tx_uuid))
        self.assertEqual(fake.operations[-1], "disconnect")

    def test_malformed_notification_cleans_up_and_writes_strict_json(self):
        fake = FakeClient([start_ack(7, SYNTHETIC, 8192), b"bad"])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())
            json.dumps(saved, allow_nan=False)

        self.assertEqual(exit_code, 1)
        self.assertIn("malformed notification", saved["exception"])
        self.assertIn(("write", runner.voice_control_uuid, 2, True), fake.operations)
        self.assertEqual(fake.operations[-2], ("stop_notify", runner.voice_tx_uuid))
        self.assertEqual(fake.operations[-1], "disconnect")

    def test_cleanup_timeouts_continue_and_write_strict_partial_report(self):
        fake = StalledCleanupClient([start_ack(7, SYNTHETIC, 8192)])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            with mock.patch(
                "ble_voice_probe.CLEANUP_TIMEOUT_SECONDS", 0.01, create=True
            ), mock.patch(
                "ble_voice_probe.time.monotonic_ns",
                side_effect=(0, 11_000_000_001),
            ):
                exit_code = self.run_probe(
                    runner,
                    Profile(
                        session=7,
                        mode=SYNTHETIC,
                        target_bps=8192,
                        duration_ms=1000,
                    ),
                    directory,
                )
            saved = json.loads((directory / "report.json").read_text())
            json.dumps(saved, allow_nan=False)

        self.assertEqual(exit_code, 1)
        self.assertIn("stop command cleanup timed out", saved["exception"])
        self.assertIn("stop notifications cleanup timed out", saved["exception"])
        self.assertIn("disconnect cleanup timed out", saved["exception"])
        self.assertIsNone(saved["metrics"]["sender_generated_data_packets"])
        self.assertFalse(saved["metrics"]["automated_pass"])
        self.assertIn(("write", runner.voice_control_uuid, 2, True), fake.operations)
        self.assertIn(("stop_notify", runner.voice_tx_uuid), fake.operations)
        self.assertIn("disconnect", fake.operations)

    def test_cancellation_is_reraised_after_cleanup_and_report(self):
        fake = CancelOnStartClient([])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            with self.assertRaises(asyncio.CancelledError):
                self.run_probe(
                    runner,
                    Profile(
                        session=7,
                        mode=SYNTHETIC,
                        target_bps=8192,
                        duration_ms=1000,
                    ),
                    directory,
                )
            saved = json.loads((directory / "report.json").read_text())

        self.assertIn("cancelled", saved["exception"].lower())
        self.assertIn(("write", runner.voice_control_uuid, 2, True), fake.operations)
        self.assertEqual(fake.operations[-2], ("stop_notify", runner.voice_tx_uuid))
        self.assertEqual(fake.operations[-1], "disconnect")

    def test_failed_required_profile_returns_nonzero(self):
        events = [
            start_ack(7, SYNTHETIC, 8192),
            pack_notification(
                DATA, 7, 1, 0, expected_synthetic_payload(1, 8192)
            ),
            pack_notification(
                STOP_SUMMARY,
                7,
                2,
                1000,
                StopSummary(2, 2, 0, 0, 1000, 8192, 0, 256).pack(),
            ),
        ]
        fake = FakeClient(events)
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertTrue(saved["metrics"]["required_profile"])
        self.assertFalse(saved["metrics"]["automated_pass"])

    def test_sequence_gap_is_reported_with_sender_summary(self):
        events = [
            start_ack(7, SYNTHETIC, 8192),
            pack_notification(
                DATA, 7, 2, 0, expected_synthetic_payload(2, 4)
            ),
            pack_notification(
                DATA, 7, 3, 20, expected_synthetic_payload(3, 4)
            ),
            pack_notification(
                STOP_SUMMARY,
                7,
                4,
                1000,
                StopSummary(3, 3, 0, 0, 1000, 12, 0, 256).pack(),
            ),
        ]
        fake = FakeClient(events)
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIsNone(saved["exception"])
        self.assertEqual(saved["metrics"]["sequence_gaps"], 1)
        self.assertEqual(saved["metrics"]["sender_generated_data_packets"], 3)
        self.assertEqual(saved["metrics"]["sender_accepted_data_packets"], 3)
        self.assertEqual(saved["metrics"]["receiver_data_packets"], 2)
        self.assertIn("sequence_gaps", saved["metrics"]["failure_reasons"])

    def test_exploratory_32768_result_is_retained_and_labeled(self):
        fake = FakeClient(synthetic_events(target_bps=32768))
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=32768, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 0)
        self.assertFalse(saved["metrics"]["required_profile"])
        self.assertEqual(saved["metrics"]["acceptance_decision"], "exploratory_pass")

    def test_microphone_decodes_16khz_mono_wav_with_expected_frames(self):
        block = bytes(164)
        events = [start_ack(7, MICROPHONE_ADPCM, 8192)]
        events.extend(
            pack_notification(DATA, 7, index + 1, index * 20, block)
            for index in range(50)
        )
        events.append(
            pack_notification(
                STOP_SUMMARY,
                7,
                51,
                1000,
                StopSummary(50, 50, 0, 0, 1000, 8200, 16000, 256).pack(),
            )
        )
        fake = FakeClient(events)
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            wav_path = directory / "microphone.wav"
            exit_code = self.run_probe(
                runner,
                Profile(
                    session=7,
                    mode=MICROPHONE_ADPCM,
                    target_bps=8192,
                    duration_ms=1000,
                ),
                directory,
                wav_path=wav_path,
            )
            saved = json.loads((directory / "report.json").read_text())
            json.dumps(saved, allow_nan=False)
            with wave.open(str(wav_path), "rb") as wav:
                self.assertEqual(wav.getnchannels(), 1)
                self.assertEqual(wav.getsampwidth(), 2)
                self.assertEqual(wav.getframerate(), 16000)
                self.assertEqual(wav.getnframes(), 16000)

        self.assertEqual(exit_code, 0)
        self.assertTrue(saved["automated_pass"])
        self.assertTrue(saved["metrics"]["automated_pass"])
        self.assertEqual(saved["wav_path"], str(wav_path))
        self.assertTrue(saved["manual_audio_review_required"])

    def test_encrypted_microphone_authenticates_then_decodes_and_reports_crypto(self):
        fake = FakeClient(encrypted_microphone_events())
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            wav_path = directory / "microphone-encrypted.wav"
            exit_code = self.run_probe(
                runner,
                Profile(
                    session=7,
                    mode=MICROPHONE_ADPCM_AES_CCM,
                    target_bps=8192,
                    duration_ms=1000,
                ),
                directory,
                wav_path=wav_path,
            )
            saved = json.loads((directory / "report.json").read_text())
            with wave.open(str(wav_path), "rb") as wav:
                self.assertEqual(wav.getnframes(), 16000)

        self.assertEqual(exit_code, 0)
        self.assertEqual(saved["metrics"]["crypto_packets"], 50)
        self.assertEqual(saved["metrics"]["crypto_failures"], 0)
        self.assertEqual(saved["metrics"]["invalid_tags"], 0)
        self.assertEqual(
            saved["metrics"]["encrypted_characteristic_value_bytes"], 186
        )
        self.assertEqual(saved["metrics"]["crypto_p99_us"], 20)

    def test_encrypted_microphone_invalid_tag_fails_before_decoder(self):
        events = encrypted_microphone_events(packets=2)[:2]
        corrupted = bytearray(events[-1])
        corrupted[-1] ^= 0x01
        events[-1] = bytes(corrupted)
        fake = FakeClient(events)
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            wav_path = directory / "invalid-tag.wav"
            exit_code = self.run_probe(
                runner,
                Profile(
                    session=7,
                    mode=MICROPHONE_ADPCM_AES_CCM,
                    target_bps=8192,
                    duration_ms=1000,
                ),
                directory,
                wav_path=wav_path,
            )
            saved = json.loads((directory / "report.json").read_text())
            with wave.open(str(wav_path), "rb") as wav:
                self.assertEqual(wav.getnframes(), 0)

        self.assertEqual(exit_code, 1)
        self.assertEqual(saved["metrics"]["invalid_tags"], 1)
        self.assertIn("authentication tag", saved["exception"])

    def test_cli_exposes_encrypted_microphone_profile(self):
        arguments = _build_parser().parse_args(
            [
                "microphone-encrypted",
                "--target-bps",
                "8192",
                "--duration",
                "30",
                "--report",
                "report.json",
                "--wav",
                "capture.wav",
            ]
        )
        self.assertEqual(arguments.protocol_mode, MICROPHONE_ADPCM_AES_CCM)

    def test_encrypted_synthetic_decrypts_pattern_and_reports_wire_overhead(self):
        fake = FakeClient(encrypted_synthetic_events())
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(
                    session=7,
                    mode=SYNTHETIC_AES_CCM,
                    target_bps=8192,
                    duration_ms=1000,
                ),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 0)
        self.assertEqual(saved["metrics"]["invalid_tags"], 0)
        self.assertEqual(saved["metrics"]["payload_corruptions"], 0)
        self.assertEqual(saved["metrics"]["generated_plaintext_bytes"], 8192)
        self.assertEqual(
            saved["metrics"]["generated_encrypted_wire_bytes"], 8592
        )
        self.assertEqual(saved["metrics"]["encryption_overhead_bytes"], 400)
        self.assertFalse(saved["manual_audio_review_required"])

    def test_encrypted_synthetic_invalid_tag_fails_closed(self):
        events = encrypted_synthetic_events()[:2]
        corrupted = bytearray(events[-1])
        corrupted[-1] ^= 0x01
        events[-1] = bytes(corrupted)
        fake = FakeClient(events)
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(
                    session=7,
                    mode=SYNTHETIC_AES_CCM,
                    target_bps=8192,
                    duration_ms=1000,
                ),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertEqual(saved["metrics"]["invalid_tags"], 1)
        self.assertEqual(saved["metrics"]["receiver_plaintext_payload_bytes"], 0)
        self.assertIn("authentication tag", saved["exception"])

    def test_encrypted_synthetic_valid_tag_with_bad_pattern_fails_corruption_gate(self):
        fake = FakeClient(encrypted_synthetic_events(corrupt_plaintext=True))
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(
                    session=7,
                    mode=SYNTHETIC_AES_CCM,
                    target_bps=8192,
                    duration_ms=1000,
                ),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertEqual(saved["metrics"]["invalid_tags"], 0)
        self.assertEqual(saved["metrics"]["payload_corruptions"], 1)

    def test_cli_exposes_only_8192_and_16384_encrypted_synthetic_profiles(self):
        parser = _build_parser()
        for target_bps in (8192, 16384):
            arguments = parser.parse_args(
                [
                    "synthetic-encrypted",
                    "--target-bps",
                    str(target_bps),
                    "--duration",
                    "60",
                    "--report",
                    "report.json",
                ]
            )
            self.assertEqual(arguments.protocol_mode, SYNTHETIC_AES_CCM)

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                parser.parse_args(
                    [
                        "synthetic-encrypted",
                        "--target-bps",
                        "32768",
                        "--duration",
                        "60",
                        "--report",
                        "report.json",
                    ]
                )

    def test_device_initiated_disconnect_fails_and_reports_partial_state(self):
        fake = DeviceDisconnectClient([start_ack(7, SYNTHETIC, 8192)])
        runner = make_runner(fake)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())
            json.dumps(saved, allow_nan=False)

        self.assertEqual(exit_code, 1)
        self.assertIn("BLE device disconnected", saved["exception"])
        self.assertTrue(saved["metrics"]["disconnected"])
        self.assertIn(("stop_notify", runner.voice_tx_uuid), fake.operations)
        self.assertNotIn("disconnect", fake.operations)

    def test_refuses_when_installed_agent_is_running(self):
        fake = FakeClient([])
        runner = make_runner(fake, agent_running=lambda: True)
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            exit_code = self.run_probe(
                runner,
                Profile(session=7, mode=SYNTHETIC, target_bps=8192, duration_ms=1000),
                directory,
            )
            saved = json.loads((directory / "report.json").read_text())

        self.assertEqual(exit_code, 1)
        self.assertIn("StopWatch Codex Agent", saved["exception"])
        self.assertEqual(fake.operations, [])

    def test_cli_rejects_allow_running_agent(self):
        arguments = [
            "synthetic",
            "--target-bps",
            "8192",
            "--duration",
            "60",
            "--report",
            "report.json",
            "--allow-running-agent",
        ]
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                _build_parser().parse_args(arguments)


if __name__ == "__main__":
    unittest.main()
