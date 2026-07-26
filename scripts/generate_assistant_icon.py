#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


WORK_SIZE = 800
OUTPUT_SIZE = 200
SCALE = WORK_SIZE // OUTPUT_SIZE
BARS = (
    (27, 77, 20, 46),
    (59, 56, 20, 88),
    (90, 32, 20, 136),
    (121, 56, 20, 88),
    (153, 77, 20, 46),
)
BODY_STOPS = (
    (0.0, (74, 231, 213, 255)),
    (0.46, (66, 182, 240, 255)),
    (1.0, (128, 100, 240, 255)),
)
GLOW_STOPS = (
    (0.0, (56, 220, 233, 140)),
    (0.58, (115, 86, 237, 77)),
    (1.0, (0, 0, 0, 0)),
)
GLOW_RADIUS = 86


def color_at(stops, position):
    for index in range(len(stops) - 1):
        start_position, start = stops[index]
        end_position, end = stops[index + 1]
        if position <= end_position:
            local = (position - start_position) / (end_position - start_position)
            return tuple(
                round(start[channel] + (end[channel] - start[channel]) * local)
                for channel in range(4)
            )
    return stops[-1][1]


def radial_glow():
    glow = Image.new("RGBA", (WORK_SIZE, WORK_SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    center = WORK_SIZE // 2
    radius = GLOW_RADIUS * SCALE
    for current_radius in range(radius, -1, -1):
        color = color_at(GLOW_STOPS, current_radius / radius)
        draw.ellipse(
            (
                center - current_radius,
                center - current_radius,
                center + current_radius,
                center + current_radius,
            ),
            fill=color,
        )
    return glow


def diagonal_gradient(width, height):
    pixels = []
    for y in range(height):
        vertical = y / max(height - 1, 1)
        for x in range(width):
            horizontal = x / max(width - 1, 1)
            pixels.append(color_at(BODY_STOPS, (horizontal + vertical) / 2))
    image = Image.new("RGBA", (width, height))
    image.putdata(pixels)
    return image


def add_waveform(canvas):
    canvas.alpha_composite(radial_glow())

    for left, top, width, height in BARS:
        pixel_width = width * SCALE
        pixel_height = height * SCALE
        mask = Image.new("L", (pixel_width, pixel_height), 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            (0, 0, pixel_width - 1, pixel_height - 1),
            radius=10 * SCALE,
            fill=255,
        )
        fill = diagonal_gradient(pixel_width, pixel_height)
        fill.putalpha(mask)
        canvas.alpha_composite(fill, (left * SCALE, top * SCALE))


def render_icon():
    image = Image.new("RGBA", (WORK_SIZE, WORK_SIZE), (0, 0, 0, 255))
    add_waveform(image)
    return image.convert("RGB").resize(
        (OUTPUT_SIZE, OUTPUT_SIZE), Image.Resampling.LANCZOS
    )


def rgb565_bytes(image):
    data = bytearray()
    for red, green, blue in image.getdata():
        value = (
            ((red * 31 // 255) << 11)
            | ((green * 63 // 255) << 5)
            | (blue * 31 // 255)
        )
        data.extend((value & 0xFF, value >> 8))
    return bytes(data)


def write_c_asset(path, data):
    lines = []
    for offset in range(0, len(data), 16):
        values = ", ".join(
            f"0x{value:02x}" for value in data[offset : offset + 16]
        )
        lines.append(f"    {values},")
    path.write_text(
        """#ifdef __has_include
#if __has_include(\"lvgl.h\")
#ifndef LV_LVGL_H_INCLUDE_SIMPLE
#define LV_LVGL_H_INCLUDE_SIMPLE
#endif
#endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include \"lvgl.h\"
#else
#include \"lvgl/lvgl.h\"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_ICON_ASSISTANT
#define LV_ATTRIBUTE_IMAGE_ICON_ASSISTANT
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_ICON_ASSISTANT uint8_t icon_assistant_map[] = {
"""
        + "\n".join(lines)
        + """
};

const lv_image_dsc_t icon_assistant = {
    .header.cf    = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w     = 200,
    .header.h     = 200,
    .data_size    = 40000 * 2,
    .data         = icon_assistant_map,
};
"""
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-c", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    image = render_icon()
    write_c_asset(args.output_c, rgb565_bytes(image))
    if args.preview:
        image.save(args.preview)


if __name__ == "__main__":
    main()
