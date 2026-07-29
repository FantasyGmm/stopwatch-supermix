import dataclasses
import struct
from typing import Optional

from ima_adpcm import decode_ima_adpcm_block


PROTOCOL_VERSION = 1
START = 1
STOP = 2
SYNTHETIC = 1
MICROPHONE_ADPCM = 2
MICROPHONE_ADPCM_AES_CCM = 3
SYNTHETIC_AES_CCM = 4
START_ACK = 1
DATA = 2
STOP_SUMMARY = 3
ERROR = 4

CONTROL = struct.Struct("<BBBBHII")
HEADER = struct.Struct("<BBHIIH")
START_ACK_PAYLOAD = struct.Struct("<HBBI")
STOP_SUMMARY_PAYLOAD = struct.Struct("<IIIIIIIH")
ENCRYPTED_STOP_SUMMARY_PAYLOAD = struct.Struct("<IIIIIIIHIIIII")
ENCRYPTED_SYNTHETIC_STOP_SUMMARY_PAYLOAD = struct.Struct("<IIIIIIIHIIIIII")
ERROR_PAYLOAD = struct.Struct("<HH")

_NOTIFICATION_KINDS = {START_ACK, DATA, STOP_SUMMARY, ERROR}
@dataclasses.dataclass(frozen=True)
class Notification:
    kind: int
    session: int
    sequence: int
    watch_ms: int
    payload: bytes


@dataclasses.dataclass(frozen=True)
class StartAck:
    negotiated_mtu: int
    mode: int
    target_bps: int


@dataclasses.dataclass(frozen=True)
class StopSummary:
    generated_data_packets: int
    accepted_data_packets: int
    queue_overflows: int
    encode_failures: int
    elapsed_ms: int
    generated_payload_bytes: int
    captured_pcm_samples: int
    negotiated_mtu: int
    crypto_packets: int = 0
    crypto_failures: int = 0
    crypto_total_us: int = 0
    crypto_max_us: int = 0
    crypto_p99_us: int = 0
    generated_plaintext_bytes: int = 0

    def pack(self, mode: int = SYNTHETIC) -> bytes:
        values = (
            self.generated_data_packets,
            self.accepted_data_packets,
            self.queue_overflows,
            self.encode_failures,
            self.elapsed_ms,
            self.generated_payload_bytes,
            self.captured_pcm_samples,
            self.negotiated_mtu,
        )
        if mode in (MICROPHONE_ADPCM_AES_CCM, SYNTHETIC_AES_CCM):
            encrypted_values = (
                *values,
                self.crypto_packets,
                self.crypto_failures,
                self.crypto_total_us,
                self.crypto_max_us,
                self.crypto_p99_us,
            )
            if mode == SYNTHETIC_AES_CCM:
                return ENCRYPTED_SYNTHETIC_STOP_SUMMARY_PAYLOAD.pack(
                    *encrypted_values,
                    self.generated_plaintext_bytes,
                )
            return ENCRYPTED_STOP_SUMMARY_PAYLOAD.pack(*encrypted_values)
        return STOP_SUMMARY_PAYLOAD.pack(*values)

    @classmethod
    def from_payload(cls, payload: bytes, mode: Optional[int] = None) -> "StopSummary":
        if mode == MICROPHONE_ADPCM_AES_CCM:
            if len(payload) != ENCRYPTED_STOP_SUMMARY_PAYLOAD.size:
                raise ValueError("invalid encrypted stop summary size")
            return cls(*ENCRYPTED_STOP_SUMMARY_PAYLOAD.unpack(payload))
        if mode == SYNTHETIC_AES_CCM:
            if len(payload) != ENCRYPTED_SYNTHETIC_STOP_SUMMARY_PAYLOAD.size:
                raise ValueError("invalid encrypted synthetic stop summary size")
            return cls(*ENCRYPTED_SYNTHETIC_STOP_SUMMARY_PAYLOAD.unpack(payload))
        if len(payload) != STOP_SUMMARY_PAYLOAD.size:
            raise ValueError("invalid stop summary size")
        return cls(*STOP_SUMMARY_PAYLOAD.unpack(payload))


def encode_start(session: int, mode: int, target_bps: int, duration_ms: int) -> bytes:
    if not 1 <= session <= 0xFFFF:
        raise ValueError("session must be 1..65535")
    if mode not in (
        SYNTHETIC,
        MICROPHONE_ADPCM,
        MICROPHONE_ADPCM_AES_CCM,
        SYNTHETIC_AES_CCM,
    ):
        raise ValueError("invalid mode")
    if not 1000 <= duration_ms <= 120000:
        raise ValueError("duration must be 1000..120000 ms")
    if mode == SYNTHETIC and not 1024 <= target_bps <= 32768:
        raise ValueError("synthetic target bitrate must be 1024..32768")
    if mode == SYNTHETIC_AES_CCM and target_bps not in (8192, 16384):
        raise ValueError("encrypted synthetic target bitrate must be 8192 or 16384")
    if mode in (MICROPHONE_ADPCM, MICROPHONE_ADPCM_AES_CCM) and target_bps != 8192:
        raise ValueError("microphone target bitrate must be 8192")
    return CONTROL.pack(PROTOCOL_VERSION, START, mode, 0, session, target_bps, duration_ms)


def encode_stop(session: int) -> bytes:
    if not 1 <= session <= 0xFFFF:
        raise ValueError("session must be 1..65535")
    return CONTROL.pack(PROTOCOL_VERSION, STOP, SYNTHETIC, 0, session, 0, 0)


def pack_notification(
    kind: int,
    session: int,
    sequence: int,
    watch_ms: int,
    payload: bytes,
) -> bytes:
    return HEADER.pack(
        PROTOCOL_VERSION,
        kind,
        session,
        sequence,
        watch_ms,
        len(payload),
    ) + payload


def decode_notification(raw: bytes) -> Notification:
    if len(raw) < HEADER.size:
        raise ValueError("notification is shorter than its header")
    version, kind, session, sequence, watch_ms, payload_length = HEADER.unpack_from(raw)
    if version != PROTOCOL_VERSION:
        raise ValueError("unsupported protocol version")
    if kind not in _NOTIFICATION_KINDS:
        raise ValueError("unknown notification kind")
    if len(raw) != HEADER.size + payload_length:
        raise ValueError("invalid notification payload size")
    return Notification(kind, session, sequence, watch_ms, raw[HEADER.size:])


def notification_aad(notification: Notification) -> bytes:
    return HEADER.pack(
        PROTOCOL_VERSION,
        notification.kind,
        notification.session,
        notification.sequence,
        notification.watch_ms,
        len(notification.payload),
    )


def decode_start_ack(payload: bytes) -> StartAck:
    if len(payload) != START_ACK_PAYLOAD.size:
        raise ValueError("invalid start acknowledgment size")
    negotiated_mtu, mode, _reserved, target_bps = START_ACK_PAYLOAD.unpack(payload)
    return StartAck(negotiated_mtu, mode, target_bps)


def decode_error(payload: bytes) -> tuple[int, int]:
    if len(payload) != ERROR_PAYLOAD.size:
        raise ValueError("invalid error payload size")
    return ERROR_PAYLOAD.unpack(payload)


def expected_synthetic_payload(sequence: int, length: int) -> bytes:
    return bytes((sequence + index) & 0xFF for index in range(length))
