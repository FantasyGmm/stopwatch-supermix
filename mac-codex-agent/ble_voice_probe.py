#!/usr/bin/env python3
"""Standalone BLE throughput and microphone probe for StopWatch."""

import argparse
import asyncio
import dataclasses
import datetime
import json
import pathlib
import secrets
import subprocess
import struct
import time
import wave
from typing import Any, Callable, Optional

from cryptography.exceptions import InvalidTag

from voice_probe_crypto import decrypt, derive_nonce
from voice_probe_metrics import ProbeMetrics
from voice_probe_protocol import (
    DATA,
    ERROR,
    MICROPHONE_ADPCM,
    MICROPHONE_ADPCM_AES_CCM,
    START_ACK,
    STOP_SUMMARY,
    SYNTHETIC,
    Notification,
    StopSummary,
    decode_error,
    decode_ima_adpcm_block,
    decode_notification,
    decode_start_ack,
    encode_start,
    encode_stop,
    notification_aad,
)


DEVICE_NAME = "StopWatch-Codex"
SERVICE_UUID = "7a230005-5474-4f50-5741-544348434f44"
VOICE_CONTROL_UUID = "7a230003-5474-4f50-5741-544348434f44"
VOICE_TX_UUID = "7a230004-5474-4f50-5741-544348434f44"
_AGENT_PROCESS_PATTERN = "StopWatch Codex Agent.app/Contents/MacOS/StopWatchCodexAgent"
_MICROPHONE_SAMPLES_PER_BLOCK = 320
CLEANUP_TIMEOUT_SECONDS = 1.0


class ProbeError(RuntimeError):
    """A failed probe lifecycle or protocol contract."""


@dataclasses.dataclass(frozen=True)
class Profile:
    session: int
    mode: int
    target_bps: int
    duration_ms: int


def stopwatch_agent_running() -> bool:
    result = subprocess.run(
        ["pgrep", "-f", _AGENT_PROCESS_PATTERN],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


async def find_watch(device_name: str, service_uuid: str) -> Any:
    try:
        from bleak import BleakScanner
    except ImportError as error:
        raise RuntimeError(
            "bleak is not installed; run pip install -r mac-codex-agent/requirements.txt"
        ) from error

    service_uuid = service_uuid.lower()

    def matches(device: Any, advertisement: Any) -> bool:
        names = {
            getattr(device, "name", None),
            getattr(advertisement, "local_name", None),
        }
        services = {
            service.lower()
            for service in (getattr(advertisement, "service_uuids", None) or [])
        }
        return device_name in names or service_uuid in services

    return await BleakScanner.find_device_by_filter(matches, timeout=8.0)


def make_bleak_client(device: Any, disconnected_callback: Callable[[Any], None]) -> Any:
    try:
        from bleak import BleakClient
    except ImportError as error:
        raise RuntimeError(
            "bleak is not installed; run pip install -r mac-codex-agent/requirements.txt"
        ) from error
    return BleakClient(device, disconnected_callback=disconnected_callback)


class VoiceProbeClient:
    def __init__(
        self,
        find_device=None,
        client_factory=None,
        clock_ns=time.monotonic_ns,
        agent_running=None,
    ):
        self.voice_control_uuid = VOICE_CONTROL_UUID
        self.voice_tx_uuid = VOICE_TX_UUID
        self.device_name = DEVICE_NAME
        self._find_device = find_device or find_watch
        self._client_factory = client_factory or make_bleak_client
        self._clock_ns = clock_ns
        self._agent_running = agent_running or stopwatch_agent_running

    async def run(
        self,
        profile: Profile,
        report_path: pathlib.Path,
        wav_path: Optional[pathlib.Path] = None,
        firmware_revision: str = "",
        allow_running_agent: bool = False,
    ) -> int:
        report_path = pathlib.Path(report_path)
        wav_path = pathlib.Path(wav_path) if wav_path is not None else None
        metrics = ProbeMetrics(profile.session, profile.mode, profile.target_bps)
        client = None
        notifications_started = False
        disconnected = False
        cleanup_disconnect = False
        start_ack = None
        summary = None
        pcm_samples: list[int] = []
        exception_text = ""
        cancelled_error: Optional[asyncio.CancelledError] = None
        report_error: Optional[Exception] = None
        expected_sequence = 0
        invalid_tags = 0
        encrypted_value_sizes: set[int] = set()
        loop = asyncio.get_running_loop()
        notifications: asyncio.Queue[Any] = asyncio.Queue()

        def enqueue(item: Any) -> None:
            try:
                loop.call_soon_threadsafe(notifications.put_nowait, item)
            except RuntimeError:
                # A late platform callback after the run loop has closed cannot affect the report.
                pass

        def on_disconnect(_client: Any) -> None:
            nonlocal disconnected
            if cleanup_disconnect:
                return
            disconnected = True
            enqueue(ProbeError("BLE device disconnected"))

        def on_notification(_sender: Any, raw: bytearray) -> None:
            try:
                notification = decode_notification(bytes(raw))
            except ValueError as error:
                enqueue(ProbeError(f"malformed notification: {error}"))
                return
            enqueue((notification, self._clock_ns()))

        try:
            if self._agent_running() and not allow_running_agent:
                raise ProbeError(
                    "StopWatch Codex Agent is running; quit it before starting the BLE voice probe"
                )

            device = await self._find_device(self.device_name, SERVICE_UUID)
            if device is None:
                raise ProbeError("StopWatch was not found by name or voice-probe service")

            client = self._client_factory(device, on_disconnect)
            await client.connect()
            if not bool(getattr(client, "is_connected", False)):
                raise ProbeError("BLE connection did not become active")

            await client.start_notify(self.voice_tx_uuid, on_notification)
            notifications_started = True
            await client.write_gatt_char(
                self.voice_control_uuid,
                encode_start(
                    profile.session,
                    profile.mode,
                    profile.target_bps,
                    profile.duration_ms,
                ),
                response=True,
            )

            timeout_seconds = profile.duration_ms / 1000 + 10
            deadline_ns = time.monotonic_ns() + int(timeout_seconds * 1_000_000_000)
            while summary is None:
                remaining_seconds = (deadline_ns - time.monotonic_ns()) / 1_000_000_000
                if remaining_seconds <= 0:
                    raise ProbeError("probe timed out waiting for notification")
                try:
                    item = await asyncio.wait_for(notifications.get(), remaining_seconds)
                except asyncio.TimeoutError as error:
                    raise ProbeError("probe timed out waiting for notification") from error

                if isinstance(item, BaseException):
                    raise item

                notification, arrival_ns = item
                if notification.session != profile.session:
                    raise ProbeError(
                        "unexpected notification session "
                        f"{notification.session}; expected {profile.session}"
                    )

                if start_ack is None:
                    if notification.kind == ERROR:
                        if notification.sequence != expected_sequence:
                            raise ProbeError(
                                f"Error sequence {notification.sequence}; "
                                f"expected {expected_sequence}"
                            )
                        code, detail = decode_error(notification.payload)
                        raise ProbeError(
                            f"watch reported probe error {code} (detail {detail})"
                        )
                    if notification.kind != START_ACK:
                        raise ProbeError("received data before matching start acknowledgment")
                    if notification.sequence != expected_sequence:
                        raise ProbeError(
                            f"StartAck sequence {notification.sequence}; "
                            f"expected {expected_sequence}"
                        )
                    start_ack = decode_start_ack(notification.payload)
                    if (
                        start_ack.mode != profile.mode
                        or start_ack.target_bps != profile.target_bps
                    ):
                        raise ProbeError("start acknowledgment does not match requested profile")
                    expected_sequence += 1
                    continue

                if notification.kind == DATA:
                    if notification.sequence < expected_sequence:
                        raise ProbeError(
                            f"Data sequence {notification.sequence}; "
                            f"expected {expected_sequence}"
                        )
                    sequence_gap = notification.sequence > expected_sequence
                    decode_payload = notification.payload
                    if profile.mode == MICROPHONE_ADPCM_AES_CCM:
                        encrypted_value_sizes.add(
                            14 + len(notification.payload)
                        )
                        try:
                            decode_payload = decrypt(
                                notification_aad(notification),
                                notification.payload,
                                derive_nonce(
                                    notification.session,
                                    notification.sequence,
                                    notification.watch_ms,
                                ),
                            )
                        except InvalidTag as error:
                            invalid_tags += 1
                            raise ProbeError(
                                "invalid AES-CCM authentication tag"
                            ) from error
                        except ValueError as error:
                            raise ProbeError(
                                f"invalid encrypted microphone packet: {error}"
                            ) from error
                    metrics.observe(
                        notification,
                        arrival_ns,
                        expected_sequence=expected_sequence,
                    )
                    expected_sequence = notification.sequence + 1
                    if profile.mode in (
                        MICROPHONE_ADPCM,
                        MICROPHONE_ADPCM_AES_CCM,
                    ) and not sequence_gap:
                        pcm_samples.extend(
                            decode_ima_adpcm_block(
                                decode_payload, _MICROPHONE_SAMPLES_PER_BLOCK
                            )
                        )
                    continue

                if notification.kind == STOP_SUMMARY:
                    if notification.sequence != expected_sequence:
                        raise ProbeError(
                            f"StopSummary sequence {notification.sequence}; "
                            f"expected {expected_sequence}"
                        )
                    summary = StopSummary.from_payload(
                        notification.payload, mode=profile.mode
                    )
                    continue

                if notification.kind == ERROR:
                    if notification.sequence != expected_sequence:
                        raise ProbeError(
                            f"Error sequence {notification.sequence}; "
                            f"expected {expected_sequence}"
                        )
                    code, detail = decode_error(notification.payload)
                    raise ProbeError(f"watch reported probe error {code} (detail {detail})")

                raise ProbeError(f"unexpected notification kind {notification.kind}")

            if start_ack is None:
                raise ProbeError("incomplete probe: start acknowledgment was not received")
        except asyncio.CancelledError as error:
            cancelled_error = error
            exception_text = "probe cancelled"
        except Exception as error:
            exception_text = str(error) or error.__class__.__name__
        finally:
            cleanup_errors = []

            async def bounded_cleanup(label: str, awaitable: Any) -> None:
                nonlocal cancelled_error
                try:
                    await asyncio.wait_for(awaitable, CLEANUP_TIMEOUT_SECONDS)
                except asyncio.TimeoutError:
                    cleanup_errors.append(f"{label} cleanup timed out")
                except asyncio.CancelledError as error:
                    if cancelled_error is None:
                        cancelled_error = error
                    cleanup_errors.append(f"{label} cleanup cancelled")
                except Exception as error:
                    cleanup_errors.append(f"{label} cleanup failed: {error}")

            if client is not None and bool(getattr(client, "is_connected", False)):
                await bounded_cleanup(
                    "stop command",
                    client.write_gatt_char(
                        self.voice_control_uuid,
                        encode_stop(profile.session),
                        response=True,
                    ),
                )
            if client is not None and notifications_started:
                await bounded_cleanup(
                    "stop notifications", client.stop_notify(self.voice_tx_uuid)
                )
            if client is not None and bool(getattr(client, "is_connected", False)):
                cleanup_disconnect = True
                await bounded_cleanup("disconnect", client.disconnect())

            if cleanup_errors:
                cleanup_text = "; ".join(cleanup_errors)
                exception_text = (
                    f"{exception_text}; {cleanup_text}"
                    if exception_text
                    else cleanup_text
                )

            try:
                self._write_wav(wav_path, pcm_samples)
            except Exception as error:
                if not exception_text:
                    exception_text = f"failed to write WAV: {error}"
            try:
                metrics_result = self._metrics_result(
                    metrics,
                    summary,
                    disconnected,
                    pcm_samples,
                    exception_text,
                    invalid_tags,
                    encrypted_value_sizes,
                )
            except Exception as error:
                if not exception_text:
                    exception_text = f"failed to finalize metrics: {error}"
                metrics_result = self._metrics_result(
                    metrics,
                    None,
                    disconnected,
                    pcm_samples,
                    exception_text,
                    invalid_tags,
                    encrypted_value_sizes,
                )
            report = {
                "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "firmware_revision": firmware_revision,
                "device_name": self.device_name,
                "requested_profile": dataclasses.asdict(profile),
                "negotiated_mtu": (
                    start_ack.negotiated_mtu
                    if start_ack is not None
                    else (summary.negotiated_mtu if summary is not None else None)
                ),
                "metrics": metrics_result,
                "automated_pass": bool(metrics_result["automated_pass"]),
                "manual_audio_review_required": profile.mode in (
                    MICROPHONE_ADPCM,
                    MICROPHONE_ADPCM_AES_CCM,
                ),
                "exception": exception_text or None,
                "wav_path": str(wav_path) if wav_path is not None else None,
            }
            try:
                self._write_report(report_path, report)
            except Exception as error:
                report_error = error

        if cancelled_error is not None:
            raise cancelled_error
        if report_error is not None:
            raise report_error
        return 0 if not exception_text and metrics_result["automated_pass"] else 1

    @staticmethod
    def _write_wav(path: Optional[pathlib.Path], samples: list[int]) -> None:
        if path is None:
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(path), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(16000)
            wav.writeframes(struct.pack(f"<{len(samples)}h", *samples))

    @staticmethod
    def _metrics_result(
        metrics: ProbeMetrics,
        summary: Optional[StopSummary],
        disconnected: bool,
        pcm_samples: list[int],
        exception_text: str,
        invalid_tags: int,
        encrypted_value_sizes: set[int],
    ) -> dict:
        if summary is not None:
            result = metrics.finish(
                summary,
                disconnected=disconnected,
                decoded_pcm_samples=len(pcm_samples),
            )
            if exception_text:
                result["automated_pass"] = False
                result["failure_reasons"] = [
                    *result["failure_reasons"], "client_exception"
                ]
                result["acceptance_decision"] = (
                    "fail" if result["required_profile"] else "exploratory_fail"
                )
            return VoiceProbeClient._add_crypto_report_fields(
                result,
                metrics.mode,
                invalid_tags,
                encrypted_value_sizes,
            )

        required_profile = not (
            metrics.mode == SYNTHETIC and metrics.target_bps == 32768
        )
        result = {
            "session": metrics.session,
            "mode": metrics.mode,
            "target_bps": metrics.target_bps,
            "sender_generated_data_packets": None,
            "sender_accepted_data_packets": None,
            "receiver_data_packets": metrics._data_packets_received,
            "receiver_payload_bytes": metrics._receiver_payload_bytes,
            "queue_overflows": None,
            "encode_failures": None,
            "crypto_packets": None,
            "crypto_failures": None,
            "crypto_total_us": None,
            "crypto_max_us": None,
            "crypto_p99_us": None,
            "elapsed_ms": None,
            "generated_payload_bytes": None,
            "captured_pcm_samples": None,
            "decoded_pcm_samples": len(pcm_samples),
            "negotiated_mtu": None,
            "disconnected": disconnected,
            "sequence_gaps": metrics._sequence_gaps,
            "sequence_regressions": metrics._sequence_regressions,
            "payload_corruptions": metrics._payload_corruptions,
            "invalid_arrival_timing": metrics._invalid_arrival_timing,
            "summary_semantically_valid": False,
            "sender_receiver_counter_parity": False,
            "sender_receiver_payload_byte_parity": False,
            "microphone_block_byte_parity": False,
            "p95_arrival_deviation_ms": None,
            "max_arrival_gap_ms": None,
            "synthetic_throughput_bps": None,
            "throughput_error_percent": None,
            "wav_duration_error_percent": None,
            "failure_reasons": ["incomplete_summary"],
            "automated_pass": False,
            "required_profile": required_profile,
            "manual_audio_review_required": metrics.mode in (
                MICROPHONE_ADPCM,
                MICROPHONE_ADPCM_AES_CCM,
            ),
            "acceptance_decision": "fail" if required_profile else "exploratory_fail",
        }
        return VoiceProbeClient._add_crypto_report_fields(
            result,
            metrics.mode,
            invalid_tags,
            encrypted_value_sizes,
        )

    @staticmethod
    def _add_crypto_report_fields(
        result: dict,
        mode: int,
        invalid_tags: int,
        encrypted_value_sizes: set[int],
    ) -> dict:
        encrypted_value_bytes = (
            next(iter(encrypted_value_sizes))
            if len(encrypted_value_sizes) == 1
            else None
        )
        result["invalid_tags"] = invalid_tags
        result["encrypted_characteristic_value_bytes"] = encrypted_value_bytes
        if mode != MICROPHONE_ADPCM_AES_CCM:
            return result

        failure_reasons = list(result["failure_reasons"])
        if invalid_tags and "invalid_tags" not in failure_reasons:
            failure_reasons.append("invalid_tags")
        if (
            encrypted_value_bytes != 186
            and "encrypted_characteristic_value_bytes" not in failure_reasons
        ):
            failure_reasons.append("encrypted_characteristic_value_bytes")
        result["failure_reasons"] = failure_reasons
        result["automated_pass"] = not failure_reasons
        result["acceptance_decision"] = (
            "pass" if result["automated_pass"] else "fail"
        )
        return result

    @staticmethod
    def _write_report(path: pathlib.Path, report: dict) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n",
            encoding="utf-8",
        )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="mode", required=True)

    for name, mode, choices, wav_required in (
        ("synthetic", SYNTHETIC, (8192, 16384, 32768), False),
        ("microphone", MICROPHONE_ADPCM, (8192,), True),
        (
            "microphone-encrypted",
            MICROPHONE_ADPCM_AES_CCM,
            (8192,),
            True,
        ),
    ):
        command = subcommands.add_parser(name)
        command.set_defaults(protocol_mode=mode)
        command.add_argument("--target-bps", type=int, choices=choices, required=True)
        command.add_argument("--duration", type=int, required=True)
        command.add_argument("--report", type=pathlib.Path, required=True)
        command.add_argument("--wav", type=pathlib.Path, required=wav_required)
        command.add_argument("--device-name", default=DEVICE_NAME)
        command.add_argument("--firmware-revision", default="")

    return parser


async def _run_cli(arguments: argparse.Namespace) -> int:
    profile = Profile(
        session=secrets.randbelow(0xFFFF) + 1,
        mode=arguments.protocol_mode,
        target_bps=arguments.target_bps,
        duration_ms=arguments.duration * 1000,
    )
    client = VoiceProbeClient()
    client.device_name = arguments.device_name
    return await client.run(
        profile,
        report_path=arguments.report,
        wav_path=arguments.wav,
        firmware_revision=arguments.firmware_revision,
    )


def main(argv: Optional[list[str]] = None) -> int:
    parser = _build_parser()
    arguments = parser.parse_args(argv)
    return asyncio.run(_run_cli(arguments))


if __name__ == "__main__":
    raise SystemExit(main())
