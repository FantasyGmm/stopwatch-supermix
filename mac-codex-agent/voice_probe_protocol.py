import dataclasses
import struct
from typing import Optional


PROTOCOL_VERSION = 1
START = 1
STOP = 2
SYNTHETIC = 1
MICROPHONE_ADPCM = 2
MICROPHONE_ADPCM_AES_CCM = 3
START_ACK = 1
DATA = 2
STOP_SUMMARY = 3
ERROR = 4

CONTROL = struct.Struct("<BBBBHII")
HEADER = struct.Struct("<BBHIIH")
START_ACK_PAYLOAD = struct.Struct("<HBBI")
STOP_SUMMARY_PAYLOAD = struct.Struct("<IIIIIIIH")
ENCRYPTED_STOP_SUMMARY_PAYLOAD = struct.Struct("<IIIIIIIHIIIII")
ERROR_PAYLOAD = struct.Struct("<HH")

_NOTIFICATION_KINDS = {START_ACK, DATA, STOP_SUMMARY, ERROR}
_INDEX_TABLE = (
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
)
_STEP_TABLE = (
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767,
)


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
        if mode == MICROPHONE_ADPCM_AES_CCM:
            return ENCRYPTED_STOP_SUMMARY_PAYLOAD.pack(
                *values,
                self.crypto_packets,
                self.crypto_failures,
                self.crypto_total_us,
                self.crypto_max_us,
                self.crypto_p99_us,
            )
        return STOP_SUMMARY_PAYLOAD.pack(*values)

    @classmethod
    def from_payload(cls, payload: bytes, mode: Optional[int] = None) -> "StopSummary":
        if mode == MICROPHONE_ADPCM_AES_CCM:
            if len(payload) != ENCRYPTED_STOP_SUMMARY_PAYLOAD.size:
                raise ValueError("invalid encrypted stop summary size")
            return cls(*ENCRYPTED_STOP_SUMMARY_PAYLOAD.unpack(payload))
        if len(payload) != STOP_SUMMARY_PAYLOAD.size:
            raise ValueError("invalid stop summary size")
        return cls(*STOP_SUMMARY_PAYLOAD.unpack(payload))


def encode_start(session: int, mode: int, target_bps: int, duration_ms: int) -> bytes:
    if not 1 <= session <= 0xFFFF:
        raise ValueError("session must be 1..65535")
    if mode not in (SYNTHETIC, MICROPHONE_ADPCM, MICROPHONE_ADPCM_AES_CCM):
        raise ValueError("invalid mode")
    if not 1000 <= duration_ms <= 120000:
        raise ValueError("duration must be 1000..120000 ms")
    if mode == SYNTHETIC and not 1024 <= target_bps <= 32768:
        raise ValueError("synthetic target bitrate must be 1024..32768")
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


def decode_ima_adpcm_block(payload: bytes, sample_count: int) -> list[int]:
    if sample_count < 1:
        raise ValueError("sample count must be positive")
    expected_size = 4 + sample_count // 2
    if len(payload) != expected_size:
        raise ValueError("invalid IMA ADPCM block size")

    predictor = struct.unpack_from("<h", payload)[0]
    index = min(payload[2], len(_STEP_TABLE) - 1)
    samples = [predictor]

    for sample_index in range(1, sample_count):
        encoded = payload[4 + (sample_index - 1) // 2]
        nibble = encoded & 0x0F if sample_index & 1 else encoded >> 4
        step = _STEP_TABLE[index]
        delta = step >> 3
        if nibble & 4:
            delta += step
        if nibble & 2:
            delta += step >> 1
        if nibble & 1:
            delta += step >> 2

        predictor += -delta if nibble & 8 else delta
        predictor = min(32767, max(-32768, predictor))
        index = min(88, max(0, index + _INDEX_TABLE[nibble]))
        samples.append(predictor)

    return samples
