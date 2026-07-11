"""BLE framing shared by the StopWatch Codex status agent."""

from typing import List


PROTOCOL_VERSION = 1
HEADER_SIZE = 6
MAX_MESSAGE_SIZE = 2048
STATUS_MESSAGE = 1
EVENT_MESSAGE = 2


def encode_message(
    payload: bytes,
    message_type: int,
    sequence: int,
    max_write_size: int,
) -> List[bytes]:
    """Split one JSON payload into ordered BLE GATT writes."""
    chunk_size = max_write_size - HEADER_SIZE
    if chunk_size <= 0 or not payload or len(payload) > MAX_MESSAGE_SIZE:
        raise ValueError("invalid BLE message size")
    if message_type not in (STATUS_MESSAGE, EVENT_MESSAGE):
        raise ValueError("invalid BLE message type")

    count = (len(payload) + chunk_size - 1) // chunk_size
    if count > 255:
        raise ValueError("too many BLE fragments")

    sequence &= 0xFFFF
    frames = []
    for index in range(count):
        header = bytes(
            (
                PROTOCOL_VERSION,
                message_type,
                sequence & 0xFF,
                (sequence >> 8) & 0xFF,
                index,
                count,
            )
        )
        start = index * chunk_size
        frames.append(header + payload[start : start + chunk_size])
    return frames
