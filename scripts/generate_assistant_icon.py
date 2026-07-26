#!/usr/bin/env python3
import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


WORK_SIZE = 800
OUTPUT_SIZE = 200


def star_points(cx, cy, outer_radius, inner_radius):
    points = []
    for index in range(8):
        angle = math.radians(-90 + index * 45)
        radius = outer_radius if index % 2 == 0 else inner_radius
        points.append((cx + math.cos(angle) * radius, cy + math.sin(angle) * radius))
    return points


def vertical_gradient(size, colors):
    image = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(image)
    stops = len(colors) - 1
    for y in range(size):
        position = y / (size - 1)
        segment = min(int(position * stops), stops - 1)
        local = position * stops - segment
        start, end = colors[segment], colors[segment + 1]
        color = tuple(
            round(start[i] + (end[i] - start[i]) * local) for i in range(4)
        )
        draw.line((0, y, size, y), fill=color)
    return image


def add_star(canvas, center, outer, inner, colors, glow_color, glow_radius):
    mask = Image.new("L", canvas.size, 0)
    draw = ImageDraw.Draw(mask)
    draw.polygon(star_points(center[0], center[1], outer, inner), fill=255)

    glow_alpha = mask.filter(ImageFilter.GaussianBlur(glow_radius)).point(
        lambda value: value * glow_color[3] // 255
    )
    glow = Image.new("RGBA", canvas.size, (*glow_color[:3], 0))
    glow.putalpha(glow_alpha)
    canvas.alpha_composite(glow)
    canvas.alpha_composite(glow)

    fill = vertical_gradient(WORK_SIZE, colors)
    fill.putalpha(mask)
    canvas.alpha_composite(fill)


def render_icon():
    image = Image.new("RGBA", (WORK_SIZE, WORK_SIZE), (0, 0, 0, 255))
    add_star(
        image,
        (400, 400),
        244,
        78,
        (
            (248, 254, 255, 255),
            (104, 226, 255, 255),
            (157, 103, 255, 255),
        ),
        (74, 166, 255, 220),
        84,
    )
    add_star(
        image,
        (192, 216),
        104,
        34,
        ((244, 255, 255, 255), (74, 220, 255, 255)),
        (68, 218, 255, 230),
        70,
    )
    add_star(
        image,
        (620, 604),
        96,
        32,
        ((246, 239, 255, 255), (157, 104, 255, 255)),
        (143, 96, 255, 230),
        68,
    )
    ImageDraw.Draw(image).ellipse((340, 340, 460, 460), fill=(255, 255, 255, 246))
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
