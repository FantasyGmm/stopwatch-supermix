"""IMA ADPCM block decoding shared by voice probe and production sessions."""

import struct


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
