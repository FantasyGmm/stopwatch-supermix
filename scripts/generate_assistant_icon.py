#!/usr/bin/env python3
"""Render the approved C2-B twin-star Assistant launcher icon."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

from compress_launcher_icons import write_compressed_asset

WORK_SIZE = 800
OUTPUT_SIZE = 200
SCALE = WORK_SIZE // OUTPUT_SIZE
ICON_SCALE = 1.20
ICON_CENTER = (100.0, 100.0)
ICON_OFFSET = (-8.0, 0.0)

MAIN_PATH = (
    ((94, 47), (100, 76), (116, 92), (145, 98)),
    ((145, 98), (116, 104), (100, 120), (94, 149)),
    ((94, 149), (88, 120), (72, 104), (43, 98)),
    ((43, 98), (72, 92), (88, 76), (94, 47)),
)
SMALL_PATH = (
    ((151, 108), (154, 125), (164, 135), (181, 138)),
    ((181, 138), (164, 141), (154, 151), (151, 168)),
    ((151, 168), (148, 151), (138, 141), (121, 138)),
    ((121, 138), (138, 135), (148, 125), (151, 108)),
)

MAIN_STOPS = (
    (0.0, (154, 118, 255)),
    (0.48, (89, 111, 245)),
    (1.0, (55, 189, 233)),
)
SMALL_STOPS = (
    (0.0, (101, 242, 214)),
    (1.0, (66, 186, 241)),
)

OUTER_GLOW_RADIUS = 17.0
OUTER_GLOW_STROKE = 6.0
OUTER_GLOW_ALPHA = 0.62
OUTER_GLOW_GAMMA = 0.65
EDGE_STROKE = 1.4


def interpolate(stops, position):
    position = max(0.0, min(1.0, position))
    for index in range(len(stops) - 1):
        start_position, start = stops[index]
        end_position, end = stops[index + 1]
        if position <= end_position:
            local = (position - start_position) / (end_position - start_position)
            return tuple(
                round(start[channel] + (end[channel] - start[channel]) * local)
                for channel in range(len(start))
            )
    return stops[-1][1]


def scale_point(point):
    return (
        ICON_CENTER[0]
        + (point[0] - ICON_CENTER[0]) * ICON_SCALE
        + ICON_OFFSET[0],
        ICON_CENTER[1]
        + (point[1] - ICON_CENTER[1]) * ICON_SCALE
        + ICON_OFFSET[1],
    )


def cubic_points(segments, samples=80):
    points = []
    for start, control_a, control_b, end in segments:
        for step in range(samples):
            t = step / samples
            inverse = 1.0 - t
            x = (
                inverse**3 * start[0]
                + 3 * inverse**2 * t * control_a[0]
                + 3 * inverse * t**2 * control_b[0]
                + t**3 * end[0]
            )
            y = (
                inverse**3 * start[1]
                + 3 * inverse**2 * t * control_a[1]
                + 3 * inverse * t**2 * control_b[1]
                + t**3 * end[1]
            )
            x, y = scale_point((x, y))
            points.append((round(x * SCALE), round(y * SCALE)))
    return points


def diagonal_gradient(stops, bounds, full_canvas=False):
    left, top, right, bottom = bounds
    left, top = scale_point((left, top))
    right, bottom = scale_point((right, bottom))
    image = Image.new("RGBA", (WORK_SIZE, WORK_SIZE), (0, 0, 0, 0))
    pixels = image.load()
    span_x = max((right - left) * SCALE, 1)
    span_y = max((bottom - top) * SCALE, 1)
    y_start = 0 if full_canvas else round(top * SCALE)
    y_end = WORK_SIZE if full_canvas else round(bottom * SCALE) + 1
    x_start = 0 if full_canvas else round(left * SCALE)
    x_end = WORK_SIZE if full_canvas else round(right * SCALE) + 1
    for y in range(y_start, y_end):
        vertical = (y - top * SCALE) / span_y
        for x in range(x_start, x_end):
            horizontal = (x - left * SCALE) / span_x
            red, green, blue = interpolate(stops, (horizontal + vertical) / 2)
            pixels[x, y] = (red, green, blue, 255)
    return image


def expanded_blur(mask, stroke_width, blur_radius, alpha, gamma=1.0):
    dilation_radius = max(1, round(stroke_width * SCALE / 2))
    expanded = mask.filter(ImageFilter.MaxFilter(dilation_radius * 2 + 1))
    blurred = expanded.filter(ImageFilter.GaussianBlur(blur_radius * SCALE))
    return blurred.point(
        lambda value: round(255 * alpha * ((value / 255) ** gamma))
    )


def add_star(canvas, segments, stops, bounds, outer_stroke=OUTER_GLOW_STROKE):
    mask = Image.new("L", (WORK_SIZE, WORK_SIZE), 0)
    ImageDraw.Draw(mask).polygon(cubic_points(segments), fill=255)

    glow_field = diagonal_gradient(stops, bounds, full_canvas=True)
    outer = glow_field.copy()
    outer.putalpha(
        expanded_blur(
            mask,
            outer_stroke,
            OUTER_GLOW_RADIUS,
            OUTER_GLOW_ALPHA,
            gamma=OUTER_GLOW_GAMMA,
        )
    )
    canvas.alpha_composite(outer)

    edge_radius = max(1, round(EDGE_STROKE * SCALE / 2))
    edge = glow_field.copy()
    edge.putalpha(mask.filter(ImageFilter.MaxFilter(edge_radius * 2 + 1)))
    canvas.alpha_composite(edge)

    fill = diagonal_gradient(stops, bounds)
    fill.putalpha(mask)
    canvas.alpha_composite(fill)


def render_icon():
    canvas = Image.new("RGBA", (WORK_SIZE, WORK_SIZE), (0, 0, 0, 255))
    add_star(canvas, MAIN_PATH, MAIN_STOPS, (43, 47, 145, 149))
    add_star(
        canvas,
        SMALL_PATH,
        SMALL_STOPS,
        (121, 108, 181, 168),
        outer_stroke=5.7,
    )
    return canvas.convert("RGB").resize(
        (OUTPUT_SIZE, OUTPUT_SIZE), Image.Resampling.LANCZOS
    )


def rgb565_preview(image):
    converted = Image.new("RGB", image.size)
    converted.putdata(
        [
            (
                ((red * 31 // 255) * 255 // 31),
                ((green * 63 // 255) * 255 // 63),
                ((blue * 31 // 255) * 255 // 31),
            )
            for red, green, blue in image.getdata()
        ]
    )
    return converted


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
    write_compressed_asset(path, "assistant", data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-c", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    image = render_icon()
    write_c_asset(args.output_c, rgb565_bytes(image))
    if args.preview:
        rgb565_preview(image).save(args.preview)


if __name__ == "__main__":
    main()
