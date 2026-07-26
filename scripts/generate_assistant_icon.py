#!/usr/bin/env python3
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


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
INNER_GLOW_RADIUS = 7
INNER_GLOW_ALPHA = 190
OUTER_GLOW_RADIUS = 18
OUTER_GLOW_ALPHA = 90
GLOW_PAD = OUTER_GLOW_RADIUS * 3


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


def padded_bar_mask(width, height, pad):
    mask = Image.new("L", (width + pad * 2, height + pad * 2), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (pad, pad, pad + width - 1, pad + height - 1),
        radius=10 * SCALE,
        fill=255,
    )
    return mask


def padded_bar_gradient(width, height, pad):
    gradient = diagonal_gradient(width, height)
    field = Image.new("RGBA", (width + pad * 2, height + pad * 2))
    field.paste(gradient, (pad, pad))

    field.paste(gradient.crop((0, 0, 1, height)).resize((pad, height)), (0, pad))
    field.paste(
        gradient.crop((width - 1, 0, width, height)).resize((pad, height)),
        (pad + width, pad),
    )
    field.paste(gradient.crop((0, 0, width, 1)).resize((width, pad)), (pad, 0))
    field.paste(
        gradient.crop((0, height - 1, width, height)).resize((width, pad)),
        (pad, pad + height),
    )

    corners = (
        ((0, 0), (0, 0)),
        ((width - 1, 0), (pad + width, 0)),
        ((0, height - 1), (0, pad + height)),
        ((width - 1, height - 1), (pad + width, pad + height)),
    )
    for source, destination in corners:
        field.paste(Image.new("RGBA", (pad, pad), gradient.getpixel(source)), destination)
    return field


def add_glow_layer(canvas, prepared_bars, radius, alpha):
    for left, top, field, mask, pad in prepared_bars:
        blurred = mask.filter(ImageFilter.GaussianBlur(radius * SCALE)).point(
            lambda value: value * alpha // 255
        )
        glow = field.copy()
        glow.putalpha(blurred)
        canvas.alpha_composite(glow, (left * SCALE - pad, top * SCALE - pad))


def add_waveform(canvas):
    pad = GLOW_PAD * SCALE
    prepared_bars = []
    for left, top, width, height in BARS:
        pixel_width = width * SCALE
        pixel_height = height * SCALE
        prepared_bars.append(
            (
                left,
                top,
                padded_bar_gradient(pixel_width, pixel_height, pad),
                padded_bar_mask(pixel_width, pixel_height, pad),
                pad,
            )
        )

    add_glow_layer(
        canvas, prepared_bars, OUTER_GLOW_RADIUS, OUTER_GLOW_ALPHA
    )
    add_glow_layer(
        canvas, prepared_bars, INNER_GLOW_RADIUS, INNER_GLOW_ALPHA
    )

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
