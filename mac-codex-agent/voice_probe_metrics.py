import math
from typing import Optional

from voice_probe_protocol import DATA, MICROPHONE_ADPCM, SYNTHETIC, Notification, StopSummary, expected_synthetic_payload


class ProbeMetrics:
    def __init__(self, session: int, mode: int, target_bps: int):
        self.session = session
        self.mode = mode
        self.target_bps = target_bps
        self._data_packets_received = 0
        self._receiver_payload_bytes = 0
        self._sequence_gaps = 0
        self._sequence_regressions = 0
        self._payload_corruptions = 0
        self._invalid_arrival_timing = 0
        self._previous_sequence: Optional[int] = None
        self._previous_watch_ms: Optional[int] = None
        self._previous_arrival_ns: Optional[float] = None
        self._arrival_deviations_ms: list[float] = []
        self._arrival_gaps_ms: list[float] = []

    def observe(self, notification: Notification, arrival_ns: int) -> None:
        if notification.kind != DATA or notification.session != self.session:
            return

        self._data_packets_received += 1
        self._receiver_payload_bytes += len(notification.payload)
        if self._previous_sequence is not None:
            sequence_delta = notification.sequence - self._previous_sequence
            if sequence_delta > 1:
                self._sequence_gaps += sequence_delta - 1
            elif sequence_delta <= 0:
                self._sequence_regressions += 1

        if self.mode == SYNTHETIC and notification.payload != expected_synthetic_payload(
            notification.sequence, len(notification.payload)
        ):
            self._payload_corruptions += 1

        timing_inputs_valid = self._is_valid_watch_ms(
            notification.watch_ms
        ) and self._is_finite_number(arrival_ns)
        if not timing_inputs_valid:
            self._invalid_arrival_timing += 1
        elif self._previous_watch_ms is not None and self._previous_arrival_ns is not None:
            try:
                watch_delta_ms = (notification.watch_ms - self._previous_watch_ms) & 0xFFFFFFFF
                arrival_delta_ms = (arrival_ns - self._previous_arrival_ns) / 1_000_000
                deviation_ms = abs(arrival_delta_ms - watch_delta_ms)
            except (ArithmeticError, TypeError, ValueError):
                self._invalid_arrival_timing += 1
            else:
                if not self._is_finite_number(arrival_delta_ms) or not self._is_finite_number(
                    deviation_ms
                ):
                    self._invalid_arrival_timing += 1
                else:
                    self._arrival_gaps_ms.append(arrival_delta_ms)
                    self._arrival_deviations_ms.append(deviation_ms)

        self._previous_sequence = notification.sequence
        if timing_inputs_valid:
            self._previous_watch_ms = notification.watch_ms
            self._previous_arrival_ns = arrival_ns

    def finish(
        self,
        summary: StopSummary,
        disconnected: bool,
        decoded_pcm_samples: Optional[int] = None,
    ) -> dict:
        p95_arrival_deviation_ms = self._nearest_rank_p95(self._arrival_deviations_ms)
        max_arrival_gap_ms = max(self._arrival_gaps_ms, default=0.0)
        summary_semantically_valid = self._summary_is_semantically_valid(summary)
        counter_parity = (
            summary.generated_data_packets
            == summary.accepted_data_packets
            == self._data_packets_received
        )
        payload_byte_parity = (
            self.mode != SYNTHETIC
            or summary.generated_payload_bytes == self._receiver_payload_bytes
        )
        throughput_bps, throughput_error_percent = self._synthetic_throughput(summary)
        wav_duration_error_percent = self._wav_duration_error(summary, decoded_pcm_samples)
        required_profile = not (self.mode == SYNTHETIC and self.target_bps == 32768)

        failure_reasons = []
        if not summary_semantically_valid:
            failure_reasons.append("invalid_summary")
        if required_profile and self._data_packets_received < 2:
            failure_reasons.append("insufficient_data_packets")
        if self._sequence_gaps:
            failure_reasons.append("sequence_gaps")
        if self._sequence_regressions:
            failure_reasons.append("sequence_regressions")
        if self._payload_corruptions:
            failure_reasons.append("payload_corruptions")
        if summary.queue_overflows:
            failure_reasons.append("queue_overflows")
        if summary.encode_failures:
            failure_reasons.append("encode_failures")
        if not counter_parity:
            failure_reasons.append("counter_parity")
        if not payload_byte_parity:
            failure_reasons.append("payload_byte_parity")
        if self.mode == SYNTHETIC and (
            throughput_error_percent is None or throughput_error_percent > 1
        ):
            failure_reasons.append("throughput_error_percent")
        if p95_arrival_deviation_ms > 40:
            failure_reasons.append("p95_arrival_deviation_ms")
        if max_arrival_gap_ms > 100:
            failure_reasons.append("max_arrival_gap_ms")
        if disconnected:
            failure_reasons.append("disconnected")
        if self._invalid_arrival_timing:
            failure_reasons.append("invalid_arrival_timing")
        if (
            self.mode == MICROPHONE_ADPCM
            and (wav_duration_error_percent is None or wav_duration_error_percent > 1)
        ):
            failure_reasons.append("wav_duration_error_percent")

        automated_pass = not failure_reasons
        acceptance_decision = (
            "pass" if automated_pass else "fail"
        ) if required_profile else (
            "exploratory_pass" if automated_pass else "exploratory_fail"
        )
        return {
            "session": self.session,
            "mode": self.mode,
            "target_bps": self.target_bps,
            "sender_generated_data_packets": summary.generated_data_packets,
            "sender_accepted_data_packets": summary.accepted_data_packets,
            "receiver_data_packets": self._data_packets_received,
            "receiver_payload_bytes": self._receiver_payload_bytes,
            "queue_overflows": summary.queue_overflows,
            "encode_failures": summary.encode_failures,
            "elapsed_ms": summary.elapsed_ms,
            "generated_payload_bytes": summary.generated_payload_bytes,
            "captured_pcm_samples": summary.captured_pcm_samples,
            "decoded_pcm_samples": decoded_pcm_samples,
            "negotiated_mtu": summary.negotiated_mtu,
            "disconnected": disconnected,
            "sequence_gaps": self._sequence_gaps,
            "sequence_regressions": self._sequence_regressions,
            "payload_corruptions": self._payload_corruptions,
            "invalid_arrival_timing": self._invalid_arrival_timing,
            "summary_semantically_valid": summary_semantically_valid,
            "sender_receiver_counter_parity": counter_parity,
            "sender_receiver_payload_byte_parity": payload_byte_parity,
            "p95_arrival_deviation_ms": p95_arrival_deviation_ms,
            "max_arrival_gap_ms": max_arrival_gap_ms,
            "synthetic_throughput_bps": throughput_bps,
            "throughput_error_percent": throughput_error_percent,
            "wav_duration_error_percent": wav_duration_error_percent,
            "failure_reasons": failure_reasons,
            "automated_pass": automated_pass,
            "required_profile": required_profile,
            "manual_audio_review_required": self.mode == MICROPHONE_ADPCM,
            "acceptance_decision": acceptance_decision,
        }

    @staticmethod
    def _nearest_rank_p95(values: list[float]) -> float:
        if not values:
            return 0.0
        return sorted(values)[math.ceil(0.95 * len(values)) - 1]

    @staticmethod
    def _is_finite_number(value: object) -> bool:
        if isinstance(value, bool):
            return False
        if isinstance(value, int):
            return True
        return isinstance(value, float) and math.isfinite(value)

    @staticmethod
    def _is_valid_watch_ms(value: object) -> bool:
        return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 0xFFFFFFFF

    @classmethod
    def _summary_is_semantically_valid(cls, summary: StopSummary) -> bool:
        numeric_fields = (
            summary.generated_data_packets,
            summary.accepted_data_packets,
            summary.queue_overflows,
            summary.encode_failures,
            summary.elapsed_ms,
            summary.generated_payload_bytes,
            summary.captured_pcm_samples,
            summary.negotiated_mtu,
        )
        if not all(isinstance(value, int) and not isinstance(value, bool) and value >= 0 for value in numeric_fields):
            return False
        if summary.accepted_data_packets > summary.generated_data_packets:
            return False
        if summary.generated_data_packets == 0 and summary.generated_payload_bytes != 0:
            return False
        return not (
            summary.generated_data_packets > 0 and summary.generated_payload_bytes == 0
        )

    def _synthetic_throughput(self, summary: StopSummary) -> tuple[Optional[float], Optional[float]]:
        if self.mode != SYNTHETIC:
            return None, None
        if summary.elapsed_ms <= 0 or self.target_bps <= 0:
            return None, None
        throughput_bps = summary.generated_payload_bytes * 1000 / summary.elapsed_ms
        return throughput_bps, abs(throughput_bps - self.target_bps) / self.target_bps * 100

    def _wav_duration_error(
        self, summary: StopSummary, decoded_pcm_samples: Optional[int]
    ) -> Optional[float]:
        if self.mode != MICROPHONE_ADPCM or summary.captured_pcm_samples <= 0:
            return None
        decoded_samples = 0 if decoded_pcm_samples is None else decoded_pcm_samples
        return (
            abs(decoded_samples - summary.captured_pcm_samples)
            / summary.captured_pcm_samples
            * 100
        )
