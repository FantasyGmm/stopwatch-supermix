#!/usr/bin/env python3
"""Losslessly encode the 200x200 RGB565 Launcher icons as LVGL LZ4 assets."""

import argparse
import re
import struct
from collections import defaultdict, deque
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSET_DIR = ROOT / "main/assets/images"
ICON_NAMES = ("assistant", "badge", "clock", "codex", "fft", "imu", "setup")
WIDTH = 200
HEIGHT = 200
STRIDE = WIDTH * 2
DECOMPRESSED_SIZE = STRIDE * HEIGHT
LZ4_METHOD = 2


def _write_extended_length(output, length):
    while length >= 255:
        output.append(255)
        length -= 255
    output.append(length)


def lz4_compress_block(source):
    """Return a valid raw LZ4 block using a bounded high-compression search."""
    output = bytearray()
    positions = defaultdict(lambda: deque(maxlen=64))
    anchor = 0
    cursor = 0
    source_size = len(source)

    # LZ4 blocks require the final five bytes to be literals and the final
    # match to start at least twelve bytes before the end of the input.
    while cursor <= source_size - 12:
        key = source[cursor : cursor + 4]
        best_position = None
        best_length = 3
        maximum_length = source_size - 5 - cursor

        for candidate in reversed(positions[key]):
            if cursor - candidate > 0xFFFF:
                break
            match_length = 4
            while (
                match_length < maximum_length
                and source[candidate + match_length]
                == source[cursor + match_length]
            ):
                match_length += 1
            if match_length > best_length:
                best_position = candidate
                best_length = match_length
        positions[key].append(cursor)

        if best_position is None:
            cursor += 1
            continue

        literal_length = cursor - anchor
        output.append(
            (min(literal_length, 15) << 4) | min(best_length - 4, 15)
        )
        if literal_length >= 15:
            _write_extended_length(output, literal_length - 15)
        output.extend(source[anchor:cursor])

        match_offset = cursor - best_position
        output.extend((match_offset & 0xFF, match_offset >> 8))
        if best_length >= 19:
            _write_extended_length(output, best_length - 19)

        match_start = cursor
        cursor += best_length
        anchor = cursor
        for position in range(match_start + 1, min(cursor, source_size - 3)):
            positions[source[position : position + 4]].append(position)

    literal_length = source_size - anchor
    output.append(min(literal_length, 15) << 4)
    if literal_length >= 15:
        _write_extended_length(output, literal_length - 15)
    output.extend(source[anchor:])
    return bytes(output)


def lz4_decompress_block(data, output_size):
    """Decode the subset of raw LZ4 blocks emitted above."""
    output = bytearray()
    cursor = 0
    while cursor < len(data):
        token = data[cursor]
        cursor += 1
        literal_length = token >> 4
        if literal_length == 15:
            while True:
                value = data[cursor]
                cursor += 1
                literal_length += value
                if value != 255:
                    break
        output.extend(data[cursor : cursor + literal_length])
        cursor += literal_length
        if cursor == len(data):
            break

        match_offset = data[cursor] | (data[cursor + 1] << 8)
        cursor += 2
        if match_offset == 0 or match_offset > len(output):
            raise ValueError("invalid LZ4 match offset")
        match_length = (token & 0x0F) + 4
        if (token & 0x0F) == 15:
            while True:
                value = data[cursor]
                cursor += 1
                match_length += value
                if value != 255:
                    break
        match_start = len(output) - match_offset
        for index in range(match_length):
            output.append(output[match_start + index])

    if len(output) != output_size:
        raise ValueError(f"decoded {len(output)} bytes, expected {output_size}")
    return bytes(output)


def compressed_payload(raw):
    if len(raw) != DECOMPRESSED_SIZE:
        raise ValueError(
            f"expected {DECOMPRESSED_SIZE} RGB565 bytes, received {len(raw)}"
        )
    compressed = lz4_compress_block(raw)
    return struct.pack("<III", LZ4_METHOD, len(compressed), len(raw)) + compressed


def extract_pixels(asset_path, name):
    source = asset_path.read_text()
    body = source.split(f"icon_{name}_map[] = {{", 1)[1].split("};", 1)[0]
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body))
    if "LV_IMAGE_FLAGS_COMPRESSED" not in source:
        return data

    method, compressed_size, decompressed_size = struct.unpack("<III", data[:12])
    if method != LZ4_METHOD or compressed_size != len(data) - 12:
        raise ValueError(f"unsupported compressed image data in {asset_path}")
    return lz4_decompress_block(data[12:], decompressed_size)


def write_compressed_asset(asset_path, name, raw):
    data = compressed_payload(raw)
    macro = f"LV_ATTRIBUTE_IMAGE_ICON_{name.upper()}"
    rows = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16])
        rows.append(f"    {values},")

    asset_path.write_text(
        f'''#ifdef __has_include
#if __has_include("lvgl.h")
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#define LV_LVGL_H_INCLUDE_SIMPLE
#endif
#endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef {macro}
#define {macro}
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {macro} uint8_t icon_{name}_map[] = {{
'''
        + "\n".join(rows)
        + f'''
}};

const lv_image_dsc_t icon_{name} = {{
    .header.cf    = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.flags = LV_IMAGE_FLAGS_COMPRESSED,
    .header.w     = {WIDTH},
    .header.h     = {HEIGHT},
    .header.stride = {STRIDE},
    .data_size    = sizeof(icon_{name}_map),
    .data         = icon_{name}_map,
}};
'''
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset-dir", type=Path, default=DEFAULT_ASSET_DIR)
    parser.add_argument("--icon", action="append", choices=ICON_NAMES)
    args = parser.parse_args()

    for name in args.icon or ICON_NAMES:
        asset_path = args.asset_dir / f"icon_{name}.c"
        raw = extract_pixels(asset_path, name)
        write_compressed_asset(asset_path, name, raw)


if __name__ == "__main__":
    main()
