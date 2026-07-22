import json
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_probe_metrics import ProbeMetrics
from voice_probe_protocol import (
    DATA,
    MICROPHONE_ADPCM,
    SYNTHETIC,
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


if __name__ == "__main__":
    unittest.main()
