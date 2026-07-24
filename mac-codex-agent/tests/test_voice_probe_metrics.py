import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_probe_metrics import ProbeMetrics
from voice_probe_protocol import (
    DATA,
    MICROPHONE_ADPCM,
    MICROPHONE_ADPCM_AES_CCM,
    SYNTHETIC,
    SYNTHETIC_AES_CCM,
    Notification,
    StopSummary,
    expected_synthetic_payload,
)


def synthetic_summary(
    packets=3,
    accepted_packets=None,
    elapsed_ms=1000,
    payload_bytes=8192,
    queue_overflows=0,
    encode_failures=0,
):
    if accepted_packets is None:
        accepted_packets = packets
    return StopSummary(
        packets,
        accepted_packets,
        queue_overflows,
        encode_failures,
        elapsed_ms,
        payload_bytes,
        0,
        256,
    )


class VoiceProbeMetricsTests(unittest.TestCase):
    def test_encrypted_16384_profile_is_capacity_evidence(self):
        metrics = ProbeMetrics(
            session=9,
            mode=SYNTHETIC_AES_CCM,
            target_bps=16384,
        )
        result = metrics.finish(
            summary=StopSummary(
                0,
                0,
                0,
                0,
                1000,
                0,
                0,
                256,
                generated_plaintext_bytes=0,
            ),
            disconnected=False,
        )

        self.assertFalse(result["required_profile"])
        self.assertEqual(result["acceptance_decision"], "exploratory_fail")

    def test_encrypted_synthetic_separates_wire_and_plaintext_accounting(self):
        metrics = ProbeMetrics(
            session=9,
            mode=SYNTHETIC_AES_CCM,
            target_bps=8192,
        )
        plaintext_sizes = [164] * 49 + [156]
        wire_bytes = 0
        for index, plaintext_size in enumerate(plaintext_sizes):
            sequence = index + 1
            timestamp_ms = index * 20
            plaintext = expected_synthetic_payload(sequence, plaintext_size)
            wire_payload = bytes(plaintext_size + 8)
            wire_bytes += len(wire_payload)
            metrics.observe(
                Notification(DATA, 9, sequence, timestamp_ms, wire_payload),
                arrival_ns=timestamp_ms * 1_000_000,
                plaintext_payload=plaintext,
            )

        result = metrics.finish(
            summary=StopSummary(
                50,
                50,
                0,
                0,
                1000,
                wire_bytes,
                0,
                256,
                crypto_packets=50,
                crypto_failures=0,
                crypto_total_us=1000,
                crypto_max_us=20,
                crypto_p99_us=20,
                generated_plaintext_bytes=8192,
            ),
            disconnected=False,
        )

        self.assertTrue(result["automated_pass"])
        self.assertEqual(result["generated_plaintext_bytes"], 8192)
        self.assertEqual(result["receiver_plaintext_payload_bytes"], 8192)
        self.assertEqual(result["generated_encrypted_wire_bytes"], 8592)
        self.assertEqual(result["receiver_encrypted_wire_bytes"], 8592)
        self.assertTrue(result["sender_receiver_payload_byte_parity"])
        self.assertTrue(result["sender_receiver_plaintext_byte_parity"])
        self.assertEqual(result["synthetic_throughput_bps"], 8192)
        self.assertEqual(result["encryption_overhead_bytes"], 400)
        self.assertEqual(result["payload_corruptions"], 0)

    def test_encrypted_synthetic_pattern_checker_uses_decrypted_plaintext(self):
        metrics = ProbeMetrics(
            session=9,
            mode=SYNTHETIC_AES_CCM,
            target_bps=8192,
        )
        metrics.observe(
            Notification(DATA, 9, 1, 0, bytes(172)),
            arrival_ns=0,
            plaintext_payload=b"bad",
        )
        result = metrics.finish(
            summary=StopSummary(
                1,
                1,
                0,
                0,
                1000,
                172,
                0,
                256,
                crypto_packets=1,
                crypto_failures=0,
                crypto_total_us=20,
                crypto_max_us=20,
                crypto_p99_us=20,
                generated_plaintext_bytes=164,
            ),
            disconnected=False,
        )
        self.assertEqual(result["payload_corruptions"], 1)
        self.assertIn("payload_corruptions", result["failure_reasons"])

    def test_encrypted_microphone_uses_ciphertext_size_and_crypto_gate(self):
        metrics = ProbeMetrics(
            session=9,
            mode=MICROPHONE_ADPCM_AES_CCM,
            target_bps=8192,
        )
        for sequence, timestamp_ms in ((1, 0), (2, 20)):
            metrics.observe(
                Notification(DATA, 9, sequence, timestamp_ms, bytes(172)),
                arrival_ns=timestamp_ms * 1_000_000,
            )
        result = metrics.finish(
            summary=StopSummary(
                2,
                2,
                0,
                0,
                40,
                344,
                640,
                256,
                crypto_packets=2,
                crypto_failures=0,
                crypto_total_us=30,
                crypto_max_us=20,
                crypto_p99_us=20,
            ),
            disconnected=False,
            decoded_pcm_samples=640,
        )
        self.assertTrue(result["microphone_block_byte_parity"])
        self.assertEqual(result["crypto_packets"], 2)
        self.assertEqual(result["crypto_p99_us"], 20)
        self.assertNotIn("crypto_failures", result["failure_reasons"])

    def test_encrypted_microphone_rejects_crypto_failure_and_p99_over_gate(self):
        metrics = ProbeMetrics(
            session=9,
            mode=MICROPHONE_ADPCM_AES_CCM,
            target_bps=8192,
        )
        for sequence, timestamp_ms in ((1, 0), (2, 20)):
            metrics.observe(
                Notification(DATA, 9, sequence, timestamp_ms, bytes(172)),
                arrival_ns=timestamp_ms * 1_000_000,
            )
        result = metrics.finish(
            summary=StopSummary(
                2,
                2,
                0,
                0,
                40,
                344,
                640,
                256,
                crypto_packets=3,
                crypto_failures=1,
                crypto_total_us=1200,
                crypto_max_us=1100,
                crypto_p99_us=1100,
            ),
            disconnected=False,
            decoded_pcm_samples=640,
        )
        self.assertFalse(result["automated_pass"])
        self.assertIn("crypto_failures", result["failure_reasons"])
        self.assertIn("crypto_p99_us", result["failure_reasons"])

    def test_clean_required_profile_passes_at_requested_rate(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        sizes = [234] * 34 + [236]
        for index, size in enumerate(sizes):
            sequence = index + 1
            timestamp_ms = round(index * 1000 / len(sizes))
            metrics.observe(
                Notification(
                    DATA,
                    9,
                    sequence,
                    timestamp_ms,
                    expected_synthetic_payload(sequence, size),
                ),
                arrival_ns=timestamp_ms * 1_000_000,
            )
        result = metrics.finish(
            summary=StopSummary(35, 35, 0, 0, 1000, 8192, 0, 256),
            disconnected=False,
        )
        self.assertTrue(result["automated_pass"])
        self.assertTrue(result["required_profile"])
        self.assertEqual(result["sequence_gaps"], 0)
        self.assertEqual(result["payload_corruptions"], 0)
        self.assertEqual(result["throughput_error_percent"], 0.0)

    def test_loss_corruption_congestion_and_timing_fail(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 1000, b"bad"),
            arrival_ns=1_000_000_000,
        )
        metrics.observe(
            Notification(DATA, 9, 3, 1020, expected_synthetic_payload(3, 4)),
            arrival_ns=1_150_000_000,
        )
        result = metrics.finish(
            summary=StopSummary(3, 2, 1, 0, 40, 7, 0, 256),
            disconnected=False,
        )
        self.assertFalse(result["automated_pass"])
        self.assertEqual(result["sequence_gaps"], 1)
        self.assertEqual(result["payload_corruptions"], 1)
        self.assertEqual(result["max_arrival_gap_ms"], 150.0)
        self.assertIn("queue_overflows", result["failure_reasons"])
        self.assertIn("counter_parity", result["failure_reasons"])

    def test_microphone_duration_uses_decoded_samples(self):
        metrics = ProbeMetrics(session=9, mode=MICROPHONE_ADPCM, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 1000, bytes(164)),
            arrival_ns=1_000_000_000,
        )
        result = metrics.finish(
            summary=StopSummary(1, 1, 0, 0, 2000, 164, 32000, 256),
            disconnected=False,
            decoded_pcm_samples=32000,
        )
        self.assertEqual(result["wav_duration_error_percent"], 0.0)
        self.assertTrue(result["manual_audio_review_required"])
        self.assertTrue(result["sender_receiver_payload_byte_parity"])
        self.assertTrue(result["microphone_block_byte_parity"])

    def test_microphone_requires_receiver_and_fixed_block_byte_parity(self):
        cases = (
            (
                "receiver_mismatch",
                (bytes(164), bytes(163)),
                StopSummary(2, 2, 0, 0, 2000, 328, 32000, 256),
                "payload_byte_parity",
            ),
            (
                "block_mismatch",
                (bytes(165), bytes(165)),
                StopSummary(2, 2, 0, 0, 2000, 330, 32000, 256),
                "microphone_block_byte_parity",
            ),
        )
        for name, payloads, summary, failure_reason in cases:
            with self.subTest(name=name):
                metrics = ProbeMetrics(
                    session=9,
                    mode=MICROPHONE_ADPCM,
                    target_bps=8192,
                )
                for sequence, payload in enumerate(payloads, start=1):
                    metrics.observe(
                        Notification(DATA, 9, sequence, sequence * 20, payload),
                        arrival_ns=sequence * 20_000_000,
                    )
                result = metrics.finish(
                    summary=summary,
                    disconnected=False,
                    decoded_pcm_samples=32000,
                )
                self.assertFalse(result["automated_pass"])
                self.assertIn(failure_reason, result["failure_reasons"])


    def test_p95_deviation_over_40_ms_fails(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        for sequence, watch_ms, arrival_ms in ((1, 0, 0), (2, 50, 91), (3, 100, 182)):
            metrics.observe(
                Notification(
                    DATA,
                    9,
                    sequence,
                    watch_ms,
                    expected_synthetic_payload(sequence, 4),
                ),
                arrival_ns=arrival_ms * 1_000_000,
            )
        result = metrics.finish(summary=synthetic_summary(), disconnected=False)
        self.assertEqual(result["p95_arrival_deviation_ms"], 41.0)
        self.assertFalse(result["automated_pass"])
        self.assertIn("p95_arrival_deviation_ms", result["failure_reasons"])

    def test_steady_state_arrival_gap_over_100_ms_fails(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        for sequence, timestamp_ms in ((1, 0), (2, 101), (3, 202)):
            metrics.observe(
                Notification(
                    DATA,
                    9,
                    sequence,
                    timestamp_ms,
                    expected_synthetic_payload(sequence, 4),
                ),
                arrival_ns=timestamp_ms * 1_000_000,
            )
        result = metrics.finish(summary=synthetic_summary(), disconnected=False)
        self.assertEqual(result["max_arrival_gap_ms"], 101.0)
        self.assertFalse(result["automated_pass"])
        self.assertIn("max_arrival_gap_ms", result["failure_reasons"])

    def test_sequence_regression_is_separate_from_loss(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        for sequence in (1, 3, 2, 2):
            metrics.observe(
                Notification(
                    DATA,
                    9,
                    sequence,
                    sequence * 10,
                    expected_synthetic_payload(sequence, 4),
                ),
                arrival_ns=sequence * 10_000_000,
            )
        result = metrics.finish(
            summary=synthetic_summary(packets=4),
            disconnected=False,
        )
        self.assertEqual(result["sequence_gaps"], 1)
        self.assertEqual(result["sequence_regressions"], 2)
        self.assertIn("sequence_regressions", result["failure_reasons"])

    def test_watch_timestamp_delta_is_uint32_wrap_safe(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 0xFFFFFFF0, expected_synthetic_payload(1, 4)),
            arrival_ns=0,
        )
        metrics.observe(
            Notification(DATA, 9, 2, 0x10, expected_synthetic_payload(2, 4)),
            arrival_ns=32_000_000,
        )
        result = metrics.finish(summary=synthetic_summary(packets=2), disconnected=False)
        self.assertEqual(result["p95_arrival_deviation_ms"], 0.0)
        self.assertEqual(result["max_arrival_gap_ms"], 32.0)

    def test_sender_receiver_counter_parity_is_required(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        for sequence in (1, 2, 3):
            metrics.observe(
                Notification(DATA, 9, sequence, sequence, expected_synthetic_payload(sequence, 4)),
                arrival_ns=sequence * 1_000_000,
            )
        result = metrics.finish(
            summary=synthetic_summary(packets=3, accepted_packets=2),
            disconnected=False,
        )
        self.assertFalse(result["sender_receiver_counter_parity"])
        self.assertIn("counter_parity", result["failure_reasons"])

    def test_32768_bps_profile_is_exploratory(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=32768)
        metrics.observe(
            Notification(DATA, 9, 1, 0, expected_synthetic_payload(1, 4)),
            arrival_ns=0,
        )
        metrics.observe(
            Notification(DATA, 9, 2, 100, expected_synthetic_payload(2, 4)),
            arrival_ns=101_000_000,
        )
        result = metrics.finish(
            summary=synthetic_summary(packets=2, payload_bytes=32768),
            disconnected=False,
        )
        self.assertFalse(result["required_profile"])
        self.assertFalse(result["automated_pass"])
        self.assertEqual(result["acceptance_decision"], "exploratory_fail")

    def test_missing_synthetic_elapsed_time_fails_throughput_gate(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 0, expected_synthetic_payload(1, 4)),
            arrival_ns=0,
        )
        result = metrics.finish(
            summary=synthetic_summary(packets=1, elapsed_ms=0),
            disconnected=False,
        )
        self.assertFalse(result["automated_pass"])
        self.assertIn("throughput_error_percent", result["failure_reasons"])
        self.assertFalse(result["summary_semantically_valid"])
        self.assertIn("invalid_summary", result["failure_reasons"])

    def test_required_profiles_need_at_least_two_matching_data_packets(self):
        cases = (
            (
                "synthetic_zero",
                SYNTHETIC,
                (),
                synthetic_summary(packets=0, payload_bytes=0),
                None,
            ),
            (
                "synthetic_one",
                SYNTHETIC,
                (Notification(DATA, 9, 1, 0, expected_synthetic_payload(1, 8192)),),
                synthetic_summary(packets=1),
                None,
            ),
            (
                "microphone_zero",
                MICROPHONE_ADPCM,
                (),
                StopSummary(0, 0, 0, 0, 2000, 0, 0, 256),
                None,
            ),
            (
                "microphone_one",
                MICROPHONE_ADPCM,
                (Notification(DATA, 9, 1, 0, bytes(164)),),
                StopSummary(1, 1, 0, 0, 2000, 164, 32000, 256),
                32000,
            ),
        )
        for name, mode, notifications, summary, decoded_samples in cases:
            with self.subTest(name=name):
                metrics = ProbeMetrics(session=9, mode=mode, target_bps=8192)
                for notification in notifications:
                    metrics.observe(notification, arrival_ns=0)
                result = metrics.finish(
                    summary=summary,
                    disconnected=False,
                    decoded_pcm_samples=decoded_samples,
                )
                self.assertTrue(result["required_profile"])
                self.assertFalse(result["automated_pass"])
                self.assertIn("insufficient_data_packets", result["failure_reasons"])

    def test_synthetic_receiver_payload_bytes_must_match_sender_summary(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 0, expected_synthetic_payload(1, 4096)),
            arrival_ns=0,
        )
        metrics.observe(
            Notification(DATA, 9, 2, 100, expected_synthetic_payload(2, 4095)),
            arrival_ns=100_000_000,
        )
        result = metrics.finish(
            summary=synthetic_summary(packets=2, payload_bytes=8192),
            disconnected=False,
        )
        self.assertEqual(result["receiver_payload_bytes"], 8191)
        self.assertFalse(result["sender_receiver_payload_byte_parity"])
        self.assertIn("payload_byte_parity", result["failure_reasons"])

    def test_non_finite_arrival_timing_fails_closed_and_stays_strict_json(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 0, expected_synthetic_payload(1, 4096)),
            arrival_ns=0,
        )
        metrics.observe(
            Notification(DATA, 9, 2, 100, expected_synthetic_payload(2, 4096)),
            arrival_ns=float("nan"),
        )
        metrics.observe(
            Notification(DATA, 9, 3, 200, expected_synthetic_payload(3, 0)),
            arrival_ns=200_000_000,
        )
        result = metrics.finish(
            summary=synthetic_summary(packets=3, payload_bytes=8192),
            disconnected=False,
        )
        self.assertFalse(result["automated_pass"])
        self.assertIn("invalid_arrival_timing", result["failure_reasons"])
        self.assertEqual(result["sequence_gaps"], 0)
        json.dumps(result, allow_nan=False)

    def test_contradictory_summary_is_rejected(self):
        metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
        metrics.observe(
            Notification(DATA, 9, 1, 0, expected_synthetic_payload(1, 4096)),
            arrival_ns=0,
        )
        metrics.observe(
            Notification(DATA, 9, 2, 100, expected_synthetic_payload(2, 4096)),
            arrival_ns=100_000_000,
        )
        result = metrics.finish(
            summary=StopSummary(1, 2, 0, 0, 1000, 8192, 0, 256),
            disconnected=False,
        )
        self.assertFalse(result["summary_semantically_valid"])
        self.assertIn("invalid_summary", result["failure_reasons"])

    def test_microphone_rejects_invalid_decoded_sample_counts_as_strict_json(self):
        invalid_samples = (None, 32000.0, float("nan"), float("inf"), -1, True)
        for decoded_samples in invalid_samples:
            with self.subTest(decoded_samples=decoded_samples):
                metrics = ProbeMetrics(
                    session=9,
                    mode=MICROPHONE_ADPCM,
                    target_bps=8192,
                )
                for sequence, timestamp_ms in ((1, 0), (2, 100)):
                    metrics.observe(
                        Notification(DATA, 9, sequence, timestamp_ms, bytes(164)),
                        arrival_ns=timestamp_ms * 1_000_000,
                    )
                result = metrics.finish(
                    summary=StopSummary(2, 2, 0, 0, 2000, 328, 32000, 256),
                    disconnected=False,
                    decoded_pcm_samples=decoded_samples,
                )
                self.assertFalse(result["automated_pass"])
                self.assertIn(
                    "invalid_decoded_pcm_samples",
                    result["failure_reasons"],
                )
                self.assertIsNone(result["decoded_pcm_samples"])
                json.dumps(result, allow_nan=False)

    def test_summary_rejects_nonpositive_elapsed_and_small_mtu(self):
        summaries = (
            StopSummary(2, 2, 0, 0, 0, 8192, 0, 256),
            StopSummary(2, 2, 0, 0, 1000, 8192, 0, 0),
        )
        for summary in summaries:
            with self.subTest(elapsed_ms=summary.elapsed_ms, mtu=summary.negotiated_mtu):
                metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
                for sequence, size in ((1, 4096), (2, 4096)):
                    metrics.observe(
                        Notification(
                            DATA,
                            9,
                            sequence,
                            sequence * 100,
                            expected_synthetic_payload(sequence, size),
                        ),
                        arrival_ns=sequence * 100_000_000,
                    )
                result = metrics.finish(summary=summary, disconnected=False)
                self.assertFalse(result["summary_semantically_valid"])
                self.assertIn("invalid_summary", result["failure_reasons"])

    def test_microphone_summary_requires_positive_plausible_capture_count(self):
        summaries = (
            StopSummary(2, 2, 0, 0, 2000, 328, 0, 256),
            StopSummary(2, 2, 0, 0, 1, 328, 32000, 256),
        )
        for summary in summaries:
            with self.subTest(
                elapsed_ms=summary.elapsed_ms,
                captured_pcm_samples=summary.captured_pcm_samples,
            ):
                metrics = ProbeMetrics(
                    session=9,
                    mode=MICROPHONE_ADPCM,
                    target_bps=8192,
                )
                for sequence, timestamp_ms in ((1, 0), (2, 1)):
                    metrics.observe(
                        Notification(DATA, 9, sequence, timestamp_ms, bytes(164)),
                        arrival_ns=timestamp_ms * 1_000_000,
                    )
                result = metrics.finish(
                    summary=summary,
                    disconnected=False,
                    decoded_pcm_samples=summary.captured_pcm_samples,
                )
                self.assertFalse(result["summary_semantically_valid"])
                self.assertIn("invalid_summary", result["failure_reasons"])

    def test_microphone_summary_allows_one_final_block_of_capture_quantization(self):
        metrics = ProbeMetrics(
            session=9,
            mode=MICROPHONE_ADPCM,
            target_bps=8192,
        )
        for sequence, timestamp_ms in ((1, 0), (2, 100)):
            metrics.observe(
                Notification(DATA, 9, sequence, timestamp_ms, bytes(164)),
                arrival_ns=timestamp_ms * 1_000_000,
            )
        result = metrics.finish(
            summary=StopSummary(2, 2, 0, 0, 1019, 328, 16000, 256),
            disconnected=False,
            decoded_pcm_samples=16000,
        )
        self.assertTrue(result["summary_semantically_valid"])
        self.assertNotIn("invalid_summary", result["failure_reasons"])

    def test_summary_enforces_nimble_att_mtu_upper_bound(self):
        for mtu, expected_valid in ((527, True), (528, False)):
            with self.subTest(mtu=mtu):
                metrics = ProbeMetrics(session=9, mode=SYNTHETIC, target_bps=8192)
                for sequence, size in ((1, 4096), (2, 4096)):
                    timestamp_ms = (sequence - 1) * 100
                    metrics.observe(
                        Notification(
                            DATA,
                            9,
                            sequence,
                            timestamp_ms,
                            expected_synthetic_payload(sequence, size),
                        ),
                        arrival_ns=timestamp_ms * 1_000_000,
                    )
                result = metrics.finish(
                    summary=StopSummary(2, 2, 0, 0, 1000, 8192, 0, mtu),
                    disconnected=False,
                )
                self.assertEqual(
                    result["summary_semantically_valid"],
                    expected_valid,
                )
                self.assertEqual(
                    "invalid_summary" in result["failure_reasons"],
                    not expected_valid,
                )


if __name__ == "__main__":
    unittest.main()
