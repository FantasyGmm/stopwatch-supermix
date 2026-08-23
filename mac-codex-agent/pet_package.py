"""Canonical CXP1 indexed pet package codec.

The package is intentionally self-contained: frames are indexed with one
global RGB565 palette, and each frame's complete index plane is PackBits
encoded.  The same bytes are consumed by the watch-side C++ decoder.
"""

from __future__ import annotations

import dataclasses
import hashlib
import struct
from typing import Iterable


MAGIC = b"CXP1"
SCHEMA_VERSION = 1
WIDTH = 144
HEIGHT = 156
MAX_FRAMES = 24
MAX_PACKAGE_BYTES = 256 * 1024
MAX_DECODED_PIXELS = 64 * 1024 * 1024
HEADER = struct.Struct("<4s8H5I32s")
FRAME_ENTRY = struct.Struct("<HHII")


class PetPackageError(ValueError):
    """Raised when a CXP1 package or PackBits stream is not canonical."""


@dataclasses.dataclass(frozen=True)
class PetFrame:
    duration_ms: int
    indices: bytes


@dataclasses.dataclass(frozen=True)
class PetPackage:
    width: int
    height: int
    palette_rgb565: tuple[int, ...]
    transparent_index: int
    frames: tuple[PetFrame, ...]
    encoded: bytes
    sha256: bytes


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _check_pixel_count(width: int, height: int, frame_count: int) -> int:
    if not (_is_int(width) and _is_int(height) and _is_int(frame_count)):
        raise PetPackageError("dimensions and frame count must be integers")
    if width != WIDTH or height != HEIGHT:
        raise PetPackageError("CXP1 production packages must be 144 x 156")
    if not 1 <= frame_count <= MAX_FRAMES:
        raise PetPackageError("frame count out of range")
    pixels = width * height
    decoded_pixels = pixels * frame_count
    if pixels <= 0 or decoded_pixels > MAX_DECODED_PIXELS:
        raise PetPackageError("decoded pixel count out of range")
    return pixels


def packbits_encode(data: bytes) -> bytes:
    """Encode *data* using canonical PackBits literal/repeat runs.

    Repeat runs of three or more bytes always win over literals.  A repeat
    control byte ``c`` represents ``258 - c`` copies (3..130); literal
    controls 0..127 represent one to 128 bytes.
    """

    if not isinstance(data, bytes):
        raise PetPackageError("PackBits input must be bytes")
    encoded = bytearray()
    cursor = 0
    length = len(data)
    while cursor < length:
        run = 1
        while cursor + run < length and run < 130 and data[cursor + run] == data[cursor]:
            run += 1
        if run >= 3:
            encoded.extend((258 - run, data[cursor]))
            cursor += run
            continue

        literal_start = cursor
        cursor += run
        while cursor < length and cursor - literal_start < 128:
            next_run = 1
            while cursor + next_run < length and next_run < 130 and data[cursor + next_run] == data[cursor]:
                next_run += 1
            if next_run >= 3:
                break
            if cursor - literal_start + next_run > 128:
                break
            cursor += next_run
        literal = data[literal_start:cursor]
        encoded.append(len(literal) - 1)
        encoded.extend(literal)
    return bytes(encoded)


def packbits_decode(data: bytes, expected_size: int) -> bytes:
    """Decode a complete PackBits stream with an exact output size."""

    if not isinstance(data, bytes):
        raise PetPackageError("PackBits input must be bytes")
    if not _is_int(expected_size) or expected_size < 0 or expected_size > MAX_DECODED_PIXELS:
        raise PetPackageError("PackBits output size out of range")
    output = bytearray()
    cursor = 0
    while cursor < len(data):
        control = data[cursor]
        cursor += 1
        if control <= 0x7F:
            count = control + 1
            if count > len(data) - cursor or count > expected_size - len(output):
                raise PetPackageError("PackBits literal overruns output")
            output.extend(data[cursor:cursor + count])
            cursor += count
        else:
            count = 258 - control
            if cursor >= len(data) or count > expected_size - len(output):
                raise PetPackageError("PackBits repeat overruns output")
            output.extend((data[cursor],) * count)
            cursor += 1
    if len(output) != expected_size:
        raise PetPackageError("PackBits output size mismatch")
    return bytes(output)


def _validate_palette(palette_rgb565: Iterable[int], transparent_index: int) -> tuple[int, ...]:
    try:
        palette = tuple(palette_rgb565)
    except TypeError as exc:
        raise PetPackageError("palette must be iterable") from exc
    if not 1 <= len(palette) <= 255:
        raise PetPackageError("palette count out of range")
    if not _is_int(transparent_index) or transparent_index != len(palette):
        raise PetPackageError("transparent index must equal palette count")
    if any(not _is_int(color) or not 0 <= color <= 0xFFFF for color in palette):
        raise PetPackageError("palette color out of range")
    return palette


def _validate_frames(frames: Iterable[PetFrame], pixels: int) -> tuple[PetFrame, ...]:
    try:
        frame_tuple = tuple(frames)
    except TypeError as exc:
        raise PetPackageError("frames must be iterable") from exc
    _check_pixel_count(WIDTH, HEIGHT, len(frame_tuple))
    for frame in frame_tuple:
        if not isinstance(frame, PetFrame):
            raise PetPackageError("frames must contain PetFrame values")
        if not _is_int(frame.duration_ms) or not 60 <= frame.duration_ms <= 1000:
            raise PetPackageError("frame duration out of range")
        if not isinstance(frame.indices, bytes) or len(frame.indices) != pixels:
            raise PetPackageError("frame index plane has the wrong size")
    return frame_tuple


def encode_package(
    width: int,
    height: int,
    palette_rgb565: tuple[int, ...],
    transparent_index: int,
    frames: tuple[PetFrame, ...],
) -> PetPackage:
    """Build a canonical, digest-protected CXP1 package."""

    if not (_is_int(width) and _is_int(height)):
        raise PetPackageError("dimensions must be integers")
    palette = _validate_palette(palette_rgb565, transparent_index)
    try:
        frame_input = tuple(frames)
    except TypeError as exc:
        raise PetPackageError("frames must be iterable") from exc
    pixels = _check_pixel_count(width, height, len(frame_input))
    frame_tuple = _validate_frames(frame_input, pixels)
    for frame in frame_tuple:
        if frame.indices and max(frame.indices) > transparent_index:
            raise PetPackageError("frame index exceeds palette")
    frame_table_offset = HEADER.size
    palette_offset = frame_table_offset + FRAME_ENTRY.size * len(frame_tuple)
    payload_offset = palette_offset + 2 * len(palette)
    entries = bytearray()
    payload = bytearray()
    prior_end = payload_offset
    for frame in frame_tuple:
        encoded_frame = packbits_encode(frame.indices)
        current_offset = payload_offset + len(payload)
        if current_offset < prior_end:
            raise PetPackageError("payload offsets must increase")
        entries.extend(FRAME_ENTRY.pack(frame.duration_ms, 0, current_offset, len(encoded_frame)))
        payload.extend(encoded_frame)
        prior_end = current_offset + len(encoded_frame)
    total_length = payload_offset + len(payload)
    if total_length > MAX_PACKAGE_BYTES:
        raise PetPackageError("package exceeds maximum size")
    decoded_pixels = width * height * len(frame_tuple)
    header = HEADER.pack(
        MAGIC,
        SCHEMA_VERSION,
        HEADER.size,
        width,
        height,
        len(frame_tuple),
        len(palette),
        transparent_index,
        0,
        frame_table_offset,
        palette_offset,
        payload_offset,
        total_length,
        decoded_pixels,
        bytes(32),
    )
    raw = bytearray(header)
    raw.extend(entries)
    raw.extend(struct.pack("<" + "H" * len(palette), *palette))
    raw.extend(payload)
    digest = hashlib.sha256(raw).digest()
    raw[40:72] = digest
    encoded = bytes(raw)
    return PetPackage(width, height, palette, transparent_index, frame_tuple, encoded, digest)


def parse_package(data: bytes) -> PetPackage:
    """Validate and parse a complete canonical CXP1 package."""

    if not isinstance(data, bytes) or len(data) < HEADER.size:
        raise PetPackageError("package is shorter than the fixed header")
    fields = HEADER.unpack_from(data, 0)
    (
        magic,
        schema,
        header_size,
        width,
        height,
        frame_count,
        palette_count,
        transparent_index,
        flags,
        frame_table_offset,
        palette_offset,
        payload_offset,
        total_length,
        decoded_pixel_count,
        digest,
    ) = fields
    if magic != MAGIC or schema != SCHEMA_VERSION or header_size != HEADER.size:
        raise PetPackageError("invalid CXP1 header")
    pixels = _check_pixel_count(width, height, frame_count)
    if not 1 <= palette_count <= 255 or transparent_index != palette_count or flags != 0:
        raise PetPackageError("invalid palette or flags")
    if total_length != len(data) or total_length > MAX_PACKAGE_BYTES:
        raise PetPackageError("invalid package length")
    if decoded_pixel_count != width * height * frame_count:
        raise PetPackageError("invalid decoded pixel count")
    frame_table_end = frame_table_offset + FRAME_ENTRY.size * frame_count
    palette_end = palette_offset + palette_count * 2
    if frame_table_offset != HEADER.size or palette_offset != frame_table_end:
        raise PetPackageError("invalid table offsets")
    if payload_offset != palette_end or frame_table_end > total_length or palette_end > total_length:
        raise PetPackageError("invalid payload offset")

    canonical = bytearray(data)
    canonical[40:72] = bytes(32)
    calculated_digest = hashlib.sha256(canonical).digest()
    if digest != calculated_digest:
        raise PetPackageError("digest mismatch")

    palette = struct.unpack_from("<" + "H" * palette_count, data, palette_offset)
    frames = []
    previous_end = payload_offset
    for index in range(frame_count):
        entry_offset = frame_table_offset + FRAME_ENTRY.size * index
        duration, reserved, frame_offset, frame_length = FRAME_ENTRY.unpack_from(data, entry_offset)
        if reserved != 0 or not 60 <= duration <= 1000:
            raise PetPackageError("invalid frame entry")
        frame_end = frame_offset + frame_length
        if frame_length == 0 or frame_offset < payload_offset or frame_end > total_length:
            raise PetPackageError("invalid frame payload span")
        if frame_offset < previous_end:
            raise PetPackageError("payload spans must increase")
        previous_end = frame_end
        frame_data = data[frame_offset:frame_end]
        indices = packbits_decode(frame_data, pixels)
        if indices and max(indices) > transparent_index:
            raise PetPackageError("frame index exceeds palette")
        frames.append(PetFrame(duration, indices))
    if previous_end != total_length:
        raise PetPackageError("payload has trailing bytes")
    return PetPackage(WIDTH, HEIGHT, tuple(palette), transparent_index, tuple(frames), data, digest)


def render_rgba_frames(package: PetPackage) -> tuple[bytes, ...]:
    """Reconstruct preview RGBA frames from the parsed package bytes."""

    parsed = parse_package(package.encoded)
    rgba_frames = []
    rgb = []
    for color in parsed.palette_rgb565:
        red5 = (color >> 11) & 0x1F
        green6 = (color >> 5) & 0x3F
        blue5 = color & 0x1F
        red = (red5 << 3) | (red5 >> 2)
        green = (green6 << 2) | (green6 >> 4)
        blue = (blue5 << 3) | (blue5 >> 2)
        rgb.append((red, green, blue))
    for frame in parsed.frames:
        output = bytearray()
        for index in frame.indices:
            if index == parsed.transparent_index:
                output.extend((0, 0, 0, 0))
            elif index >= len(rgb):
                raise PetPackageError("frame index exceeds palette")
            else:
                output.extend((*rgb[index], 255))
        rgba_frames.append(bytes(output))
    return tuple(rgba_frames)
