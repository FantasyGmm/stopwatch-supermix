#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


WORK_SIZE = 800
OUTPUT_SIZE = 200
SCALE = WORK_SIZE // OUTPUT_SIZE
BARS = (
    (34, 79, 52, 121),
    (63, 60, 81, 140),
    (91, 38, 109, 162),
    (119, 60, 137, 140),
    (148, 79, 166, 121),
)
BODY_COLORS = (
    (74, 231, 213, 255),
    (66, 182, 240, 255),
    (128, 100, 240, 255),
)
GLOW_COLORS = (
    (37, 217, 219, 255),
    (54, 138, 246, 255),
    (112, 72, 238, 255),
)


def horizontal_gradient(size, colors):
    image = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(image)
    stops = len(colors) - 1
    for x in range(size):
        position = x / (size - 1)
        segment = min(int(position * stops), stops - 1)
        local = position * stops - segment
        start, end = colors[segment], colors[segment + 1]
        color = tuple(
            round(start[i] + (end[i] - start[i]) * local) for i in range(4)
        )
        draw.line((x, 0, x, size), fill=color)
    return image


def waveform_mask():
    mask = Image.new("L", (WORK_SIZE, WORK_SIZE), 0)
    draw = ImageDraw.Draw(mask)
    for left, top, right, bottom in BARS:
        draw.rounded_rectangle(
            (
                left * SCALE,
                top * SCALE,
                right * SCALE,
                bottom * SCALE,
            ),
            radius=9 * SCALE,
            fill=255,
        )
    return mask


def add_waveform(canvas):
    mask = waveform_mask()

    glow_alpha = mask.filter(ImageFilter.GaussianBlur(68)).point(
        lambda value: value * 180 // 255
    )
    glow = horizontal_gradient(WORK_SIZE, GLOW_COLORS)
    glow.putalpha(glow_alpha)
    canvas.alpha_composite(glow)
    canvas.alpha_composite(glow)

    fill = horizontal_gradient(WORK_SIZE, BODY_COLORS)
    fill.putalpha(mask)
    canvas.alpha_composite(fill)


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
