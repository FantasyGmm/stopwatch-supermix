"""Bounded conversion of animated GIFs into canonical CXP1 pet packages."""

from __future__ import annotations

import dataclasses
import io
import pathlib
from typing import Iterable

from PIL import Image, UnidentifiedImageError

from pet_package import PetFrame, PetPackage, PetPackageError, encode_package, render_rgba_frames


MAX_INPUT_BYTES = 10 * 1024 * 1024
MAX_CANVAS = (1024, 1024)
MAX_SOURCE_FRAMES = 240
MAX_DECODED_PIXELS = 64_000_000
MAX_OUTPUT_FRAMES = 24
MIN_DELAY_MS = 60
MAX_DELAY_MS = 1000
WATCH_WIDTH = 144
WATCH_HEIGHT = 156


class PetConversionError(ValueError):
    """A GIF was malformed or outside the conversion safety contract."""

    def __init__(self, code: str, detail: str):
        self.code = code
        self.detail = detail
        super().__init__(f"{code}: {detail}")


@dataclasses.dataclass(frozen=True)
class ConversionMetadata:
    source_frames: int
    output_frames: int
    loop_duration_ms: int
    width: int
    height: int
    package_bytes: int


@dataclasses.dataclass(frozen=True)
class ConvertedPet:
    package: PetPackage
    preview_rgba: tuple[bytes, ...]
    metadata: ConversionMetadata


@dataclasses.dataclass(frozen=True)
class _GifInspection:
    width: int
    height: int
    frame_count: int


def _error(code: str, detail: str) -> PetConversionError:
    return PetConversionError(code, detail)


def _read_bounded(path: pathlib.Path, maximum: int) -> bytes:
    try:
        with path.open("rb") as source:
            data = source.read(maximum + 1)
    except OSError as exc:
        raise _error("input_unreadable", f"cannot read GIF: {exc}") from exc
    if len(data) > maximum:
        raise _error("input_too_large", "GIF exceeds the 10 MiB input limit")
    return data


def _need(source: bytes, cursor: int, count: int) -> None:
    if count < 0 or cursor < 0 or cursor + count > len(source):
        raise _error("truncated_gif", "truncated GIF block stream")


def _skip_sub_blocks(source: bytes, cursor: int) -> int:
    while True:
        _need(source, cursor, 1)
        length = source[cursor]
        cursor += 1
        if length == 0:
            return cursor
        _need(source, cursor, length)
        cursor += length


def _inspect_gif_blocks(source: bytes) -> _GifInspection:
    """Validate the GIF grammar and count image descriptors without canvases."""

    _need(source, 0, 13)
    if source[:6] not in (b"GIF87a", b"GIF89a"):
        raise _error("invalid_gif", "GIF signature must be GIF87a or GIF89a")
    width = int.from_bytes(source[6:8], "little")
    height = int.from_bytes(source[8:10], "little")
    if width == 0 or height == 0:
        raise _error("invalid_gif", "logical GIF canvas must be non-empty")
    packed = source[10]
    cursor = 13
    if packed & 0x80:
        table_size = 3 * (1 << ((packed & 0x07) + 1))
        _need(source, cursor, table_size)
        cursor += table_size

    frame_count = 0
    saw_trailer = False
    while cursor < len(source):
        marker = source[cursor]
        cursor += 1
        if marker == 0x3B:
            saw_trailer = True
            break
        if marker == 0x21:
            _need(source, cursor, 1)
            cursor = _skip_sub_blocks(source, cursor + 1)
            continue
        if marker != 0x2C:
            raise _error("invalid_gif", "unknown GIF block marker")

        _need(source, cursor, 9)
        left = int.from_bytes(source[cursor:cursor + 2], "little")
        top = int.from_bytes(source[cursor + 2:cursor + 4], "little")
        frame_width = int.from_bytes(source[cursor + 4:cursor + 6], "little")
        frame_height = int.from_bytes(source[cursor + 6:cursor + 8], "little")
        image_packed = source[cursor + 8]
        cursor += 9
        if frame_width == 0 or frame_height == 0 or left + frame_width > width or top + frame_height > height:
            raise _error("invalid_gif", "image descriptor lies outside logical canvas")
        if image_packed & 0x80:
            table_size = 3 * (1 << ((image_packed & 0x07) + 1))
            _need(source, cursor, table_size)
            cursor += table_size
        _need(source, cursor, 1)  # LZW minimum code size
        cursor = _skip_sub_blocks(source, cursor + 1)
        frame_count += 1

    if not saw_trailer:
        raise _error("truncated_gif", "truncated GIF is missing its trailer")
    if cursor != len(source):
        raise _error("invalid_gif", "GIF has trailing bytes after trailer")
    if frame_count == 0:
        raise _error("invalid_gif", "GIF contains no image frames")
    return _GifInspection(width, height, frame_count)


def _validate_inspection(inspection: _GifInspection) -> None:
    if inspection.width > MAX_CANVAS[0] or inspection.height > MAX_CANVAS[1]:
        raise _error("canvas_too_large", "GIF logical canvas exceeds 1024 x 1024")
    pixels = inspection.width * inspection.height * inspection.frame_count
    if pixels > MAX_DECODED_PIXELS:
        raise _error("decoded_pixel_budget", "GIF decoded pixel budget exceeds 64,000,000")
    if inspection.frame_count > MAX_SOURCE_FRAMES:
        raise _error("source_frame_limit", "GIF has more than 240 source frames")


def _coalesce_with_pillow(source: bytes, inspection: _GifInspection) -> tuple[tuple[bytes, int], ...]:
    """Materialize Pillow's logical RGBA canvas only after bounded inspection."""

    try:
        image = Image.open(io.BytesIO(source))
        frames = []
        for index in range(inspection.frame_count):
            image.seek(index)
            # GifImagePlugin applies the preceding frame's disposal extent when
            # seeking.  Copy the complete logical canvas before the next seek.
            rgba = image.convert("RGBA").copy()
            if rgba.size != (inspection.width, inspection.height):
                raise _error("invalid_gif", "Pillow produced the wrong logical canvas")
            frames.append((rgba.tobytes(), int(image.info.get("duration", 0))))
        try:
            image.seek(inspection.frame_count)
        except EOFError:
            pass
        else:
            raise _error("invalid_gif", "GIF image count changed during decode")
    except PetConversionError:
        raise
    except (OSError, SyntaxError, UnidentifiedImageError, ValueError) as exc:
        raise _error("decode_failed", f"Pillow could not decode GIF: {exc}") from exc
    return tuple(frames)


def _merge_identical_frames(frames: Iterable[tuple[bytes, int]]) -> tuple[tuple[bytes, int], ...]:
    merged: list[tuple[bytes, int]] = []
    for rgba, duration in frames:
        duration = max(0, duration)
        if merged and merged[-1][0] == rgba:
            merged[-1] = (rgba, merged[-1][1] + duration)
        else:
            merged.append((rgba, duration))
    return tuple(merged)


def _bounded_delay(duration: int) -> int:
    return max(MIN_DELAY_MS, min(MAX_DELAY_MS, duration))


def _sample_timeline(timed: tuple[tuple[bytes, int], ...], maximum: int) -> tuple[tuple[bytes, int], ...]:
    if not timed:
        raise _error("invalid_gif", "GIF has no decoded frames")
    if len(timed) <= maximum:
        return tuple((rgba, _bounded_delay(duration)) for rgba, duration in timed)

    total = sum(max(0, duration) for _, duration in timed)
    total = max(total, len(timed))
    boundaries = [0]
    elapsed = 0
    for _, duration in timed:
        elapsed += max(0, duration)
        boundaries.append(elapsed)
    sample_boundaries = [round(index * total / maximum) for index in range(maximum + 1)]
    output: list[tuple[bytes, int]] = []
    frame_index = 0
    for index in range(maximum):
        timestamp = sample_boundaries[index]
        while frame_index + 1 < len(timed) and boundaries[frame_index + 1] <= timestamp:
            frame_index += 1
        rgba = timed[frame_index][0]
        duration = sample_boundaries[index + 1] - timestamp
        if output and output[-1][0] == rgba:
            output[-1] = (rgba, output[-1][1] + duration)
        else:
            output.append((rgba, duration))
    return tuple((rgba, _bounded_delay(duration)) for rgba, duration in output)


def _fit_nearest(rgba: bytes, source_width: int, source_height: int) -> Image.Image:
    source = Image.frombytes("RGBA", (source_width, source_height), rgba)
    scale = min(WATCH_WIDTH / source_width, WATCH_HEIGHT / source_height)
    width = max(1, round(source_width * scale))
    height = max(1, round(source_height * scale))
    resized = source.resize((width, height), Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", (WATCH_WIDTH, WATCH_HEIGHT), (0, 0, 0, 0))
    canvas.alpha_composite(resized, ((WATCH_WIDTH - width) // 2, (WATCH_HEIGHT - height) // 2))
    return canvas


def _rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def _quantize_global(canvases: tuple[Image.Image, ...], colors: int) -> tuple[tuple[int, ...], int, tuple[PetFrame, ...]]:
    if not canvases:
        raise _error("invalid_gif", "GIF has no frames to quantize")
    rgb_rows = []
    alpha_masks = []
    for canvas in canvases:
        alpha = canvas.getchannel("A")
        alpha_masks.append(alpha)
        rgb = Image.new("RGB", canvas.size)
        rgb.paste(canvas.convert("RGB"), mask=alpha.point(lambda value: 255 if value >= 128 else 0))
        rgb_rows.append(rgb)
    joined = Image.new("RGB", (WATCH_WIDTH, WATCH_HEIGHT * len(rgb_rows)))
    for index, rgb in enumerate(rgb_rows):
        joined.paste(rgb, (0, index * WATCH_HEIGHT))
    quantized = joined.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette_data = quantized.getpalette("RGB")
    color_map: dict[int, int] = {}
    palette: list[int] = []
    indices = []
    for index, alpha in enumerate(alpha_masks):
        region = quantized.crop((0, index * WATCH_HEIGHT, WATCH_WIDTH, (index + 1) * WATCH_HEIGHT))
        raw = region.tobytes()
        alpha_values = alpha.tobytes()
        frame = bytearray()
        for source_index, opacity in zip(raw, alpha_values):
            if opacity < 128:
                frame.append(0)
                continue
            offset = source_index * 3
            color = _rgb565(palette_data[offset], palette_data[offset + 1], palette_data[offset + 2])
            destination_index = color_map.get(color)
            if destination_index is None:
                destination_index = len(palette)
                color_map[color] = destination_index
                palette.append(color)
            frame.append(destination_index)
        indices.append(frame)
    if not palette:
        palette.append(0)
    transparent_index = len(palette)
    frames = []
    for canvas_indices, alpha in zip(indices, alpha_masks):
        alpha_values = alpha.tobytes()
        for offset, opacity in enumerate(alpha_values):
            if opacity < 128:
                canvas_indices[offset] = transparent_index
        frames.append(PetFrame(MIN_DELAY_MS, bytes(canvas_indices)))
    return tuple(palette), transparent_index, tuple(frames)


def _make_result(package: PetPackage, source_frames: int) -> ConvertedPet:
    preview = render_rgba_frames(package)
    metadata = ConversionMetadata(
        source_frames=source_frames,
        output_frames=len(package.frames),
        loop_duration_ms=sum(frame.duration_ms for frame in package.frames),
        width=package.width,
        height=package.height,
        package_bytes=len(package.encoded),
    )
    return ConvertedPet(package, preview, metadata)


def convert_gif(path: pathlib.Path) -> ConvertedPet:
    """Convert one safely bounded GIF into a canonical watch pet package."""

    source = _read_bounded(pathlib.Path(path), MAX_INPUT_BYTES)
    inspection = _inspect_gif_blocks(source)
    _validate_inspection(inspection)
    decoded = _coalesce_with_pillow(source, inspection)
    timed = _sample_timeline(_merge_identical_frames(decoded), MAX_OUTPUT_FRAMES)
    canvases = tuple(_fit_nearest(rgba, inspection.width, inspection.height) for rgba, _ in timed)
    palette, transparent_index, bare_frames = _quantize_global(canvases, 255)
    frames = tuple(PetFrame(duration, frame.indices) for (_, duration), frame in zip(timed, bare_frames))
    try:
        package = encode_package(WATCH_WIDTH, WATCH_HEIGHT, palette, transparent_index, frames)
    except PetPackageError as exc:
        raise _error("package_too_large", f"pet package is not valid for the watch: {exc}") from exc
    return _make_result(package, inspection.frame_count)
