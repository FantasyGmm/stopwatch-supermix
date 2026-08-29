#!/usr/bin/env python3
"""Render exact preview strings from the LVGL MontserratSemiBold26 bitmap."""

from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
FONT_SOURCE = ROOT / "main/assets/fonts/MontserratSemiBold26.c"
OUTPUT_DIR = Path(__file__).resolve().parent / "assets"


def parse_font():
    source = FONT_SOURCE.read_text(encoding="utf-8")
    bitmap_body = source.split("glyph_bitmap[] = {", 1)[1].split(
        "};\n\n/*---------------------\n *  GLYPH DESCRIPTION", 1
    )[0]
    bitmap = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", bitmap_body))

    descriptor_body = source.split("glyph_dsc[] = {", 1)[1].split(
        "};\n\n/*---------------------\n *  CHARACTER MAPPING", 1
    )[0]
    pattern = re.compile(
        r"\.bitmap_index = (-?\d+), \.adv_w = (-?\d+), "
        r"\.box_w = (-?\d+), \.box_h = (-?\d+), "
        r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)"
    )
    descriptors = [tuple(map(int, match.groups())) for match in pattern.finditer(descriptor_body)]
    if len(descriptors) != 96:
        raise RuntimeError(f"expected 96 descriptors, found {len(descriptors)}")
    return bitmap, descriptors


def render(text: str, destination: Path):
    bitmap, descriptors = parse_font()
    advances = []
    for character in text:
        codepoint = ord(character)
        if not 32 <= codepoint <= 126:
            raise ValueError(f"unsupported character: {character!r}")
        advances.append((descriptors[codepoint - 31][1] + 8) // 16)

    image = Image.new("RGBA", (sum(advances), 27), (0, 0, 0, 0))
    pixels = image.load()
    cursor_x = 0
    baseline_y = 27 - 5
    for character, advance in zip(text, advances):
        bitmap_index, _, box_w, box_h, ofs_x, ofs_y = descriptors[ord(character) - 31]
        top = baseline_y - box_h - ofs_y
        for pixel_index in range(box_w * box_h):
            packed = bitmap[bitmap_index + pixel_index // 4]
            alpha_2bpp = (packed >> (6 - 2 * (pixel_index % 4))) & 0x03
            if alpha_2bpp:
                x = cursor_x + ofs_x + pixel_index % box_w
                y = top + pixel_index // box_w
                if 0 <= x < image.width and 0 <= y < image.height:
                    pixels[x, y] = (242, 246, 248, alpha_2bpp * 85)
        cursor_x += advance

    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination)


if __name__ == "__main__":
    render("gpt-5.6-sol", OUTPUT_DIR / "model-gpt-5.6-sol.png")
    render("HIGH", OUTPUT_DIR / "effort-high.png")
