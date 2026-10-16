#!/usr/bin/env python3
"""Pack the selected Elysia atlas into the existing LVGL RGB565A8 pet ABI.

Requires Pillow, like generate_trae_robot_assets.py. This is deterministic
asset conversion, not artwork generation. One body/alpha matte per state keeps
the generated atlas's incidental redraw noise out of the animation.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "main/apps/app_codex_status/assets"
SOURCE = ASSETS / "source/elysia/spritesheet.png"
PREVIEW = ROOT / "artifacts/elysia-pet-preview"
WIDTH, HEIGHT = 96, 104
STATES = (
    ("idle", 0, 391, (280, 110, 110, 140, 140, 320)),
    ("typing", 391, 770, (120, 120, 120, 120, 120, 220)),
)
# Coordinates refer to the final 96x104 pixel grid. These regions are inside
# the opaque body, away from the hair, ears, ornament and skirt silhouette.
EYE_REGIONS = ((32, 41, 47, 51), (51, 41, 66, 51))
HAND_REGIONS = {
    "idle": (31, 58, 67, 76),
    "typing": (29, 62, 68, 77),
}


def frames_from_atlas(atlas: Image.Image, top: int, bottom: int) -> list[Image.Image]:
    cells = [
        atlas.crop((round(i * atlas.width / 6), top,
                    round((i + 1) * atlas.width / 6), bottom))
        for i in range(6)
    ]
    # Use one crop and one scale for the whole sequence, never per-frame
    # auto-cropping: blinking and hand motion must not move the character.
    boxes = [cell.getchannel("A").point(lambda a: 255 if a >= 16 else 0).getbbox()
             for cell in cells]
    if any(box is None for box in boxes):
        raise ValueError("an atlas frame is empty")
    crop = (min(box[0] for box in boxes) - 2,
            min(box[1] for box in boxes) - 2,
            max(box[2] for box in boxes) + 2,
            max(box[3] for box in boxes) + 2)
    result = []
    for cell in cells:
        sprite = cell.crop(crop)
        sprite.thumbnail((WIDTH - 4, HEIGHT - 4), Image.Resampling.NEAREST)
        frame = Image.new("RGBA", (WIDTH, HEIGHT))
        # paste without a mask copies alpha rather than multiplying it twice.
        frame.paste(sprite, ((WIDTH - sprite.width) // 2, HEIGHT - 2 - sprite.height))
        result.append(frame)
    return result


def rgb565a8_bytes(frame: Image.Image) -> bytes:
    color, alpha = bytearray(), bytearray()
    for red, green, blue, opacity in frame.getdata():
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        color.extend((value & 0xFF, value >> 8))
        alpha.append(opacity)
    return bytes(color + alpha)


def stabilize_motion(frames: list[Image.Image], state: str) -> list[Image.Image]:
    """Keep the approved art; animate only its authored eyes and hands.

    The atlas redraws the whole sprite slightly in every cell. Even with a
    shared crop, downsampling turns those changes into sparkling edge pixels.
    Reuse frame zero's body and alpha instead of averaging frames (which would
    blur the pixel clusters or leave a ghost of an open eye).
    """
    body = frames[0]
    # The source has low-alpha red/magenta speckles outside the outline.
    # Keep the visible antialiased matte, but remove those stray pixels once.
    alpha = body.getchannel("A").point(lambda a: 0 if a < 16 else a)
    result = []
    for i, source in enumerate(frames):
        frame = body.copy()
        # Both closed-eye holds use the same clean artwork; the open-eye
        # frames already inherit the reference body. Preserve the blink timing.
        if i in (2, 3):
            for box in EYE_REGIONS:
                frame.paste(frames[2].crop(box), box)
        box = HAND_REGIONS[state]
        frame.paste(source.crop(box), box)
        frame.putalpha(alpha)
        result.append(frame)
    return result


def decoded_rgb565a8(frame: Image.Image) -> Image.Image:
    """Preview the packed 5/6/5 colors and alpha, not the unencoded source."""
    data = rgb565a8_bytes(frame)
    count = WIDTH * HEIGHT
    pixels = []
    for i in range(count):
        value = data[2 * i] | (data[2 * i + 1] << 8)
        red, green, blue = (value >> 11) & 31, (value >> 5) & 63, value & 31
        pixels.append(((red << 3) | (red >> 2),
                       (green << 2) | (green >> 4),
                       (blue << 3) | (blue >> 2), data[2 * count + i]))
    decoded = Image.new("RGBA", (WIDTH, HEIGHT))
    decoded.putdata(pixels)
    return decoded


def resource(name: str, frame: Image.Image) -> str:
    data = rgb565a8_bytes(frame)
    rows = ["    " + ", ".join(f"0x{v:02X}" for v in data[i:i + 24]) + ","
            for i in range(0, len(data), 24)]
    return f"""alignas(4) static const uint8_t {name}_map[] = {{
{chr(10).join(rows)}
}};

const lv_image_dsc_t {name} = {{
    {{ LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RGB565A8, 0, CODEX_PET_FRAME_W, CODEX_PET_FRAME_H, CODEX_PET_FRAME_STRIDE, 0 }},
    sizeof({name}_map),
    {name}_map,
    nullptr,
    nullptr,
}};
"""


def main() -> None:
    atlas = Image.open(SOURCE).convert("RGBA")
    if atlas.size != (2043, 770):
        raise ValueError("unexpected atlas dimensions; update the recorded row cuts first")
    PREVIEW.mkdir(parents=True, exist_ok=True)
    parts = ['// Generated by scripts/generate_elysia_pet_assets.py. Do not edit.\n',
             '#include "codex_pet_assets.h"\n']
    contact = Image.new("RGB", (WIDTH * 6, HEIGHT * 2), "black")
    for row, (state, top, bottom, durations) in enumerate(STATES):
        frames = stabilize_motion(frames_from_atlas(atlas, top, bottom), state)
        rendered = []
        names = [f"codex_pet_{state}_{i}" for i in range(6)]
        for i, (name, frame) in enumerate(zip(names, frames)):
            parts.append(resource(name, frame))
            decoded = decoded_rgb565a8(frame)
            decoded.save(PREVIEW / f"{state}-{i}.png")
            black = Image.new("RGBA", frame.size, "black")
            black.alpha_composite(decoded)
            black = black.convert("RGB")
            contact.paste(black, (i * WIDTH, row * HEIGHT))
            rendered.append(black)
        parts.append(f"const lv_image_dsc_t* const codex_pet_{state}_frames[6] = {{\n"
                     + "\n".join(f"    &{name}," for name in names) + "\n};\n")
        parts.append(f"const uint32_t codex_pet_{state}_durations_ms[6] = {{"
                     + ", ".join(map(str, durations)) + "};\n")
        # One palette and no dithering avoid introducing GIF-only shimmer.
        palette_source = Image.new("RGB", (WIDTH * 6, HEIGHT))
        for i, frame in enumerate(rendered):
            palette_source.paste(frame, (i * WIDTH, 0))
        palette = palette_source.quantize(colors=256)
        gif_frames = [frame.quantize(palette=palette, dither=Image.Dither.NONE)
                      for frame in rendered]
        gif_frames[0].save(PREVIEW / f"{state}.gif", save_all=True,
                         append_images=gif_frames[1:], duration=durations,
                         loop=0, disposal=2)
    (ASSETS / "codex_pet_assets.cpp").write_text("\n".join(parts), encoding="utf-8")
    contact.save(PREVIEW / "contact.png")
    contact.resize((WIDTH * 18, HEIGHT * 6), Image.Resampling.NEAREST).save(
        PREVIEW / "contact-3x.png")
    preview = Image.new("RGB", (WIDTH * 2, HEIGHT), "black")
    preview.paste(contact.crop((0, 0, WIDTH, HEIGHT)), (0, 0))
    preview.paste(contact.crop((0, HEIGHT, WIDTH, HEIGHT * 2)), (WIDTH, 0))
    preview.resize((WIDTH * 6, HEIGHT * 3), Image.Resampling.NEAREST).save(
        PREVIEW / "preview.png")
    print(f"Packed 12 RGB565A8 frames: {12 * WIDTH * HEIGHT * 3:,} bytes")
    print(f"Previews: {PREVIEW}")


if __name__ == "__main__":
    main()
