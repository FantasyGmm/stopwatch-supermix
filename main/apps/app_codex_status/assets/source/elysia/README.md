# Elysia pet source

`spritesheet.png` is the selected 2043 × 770, six-column, two-row pixel-art
source for the firmware pet. It was generated with the built-in ImageGen; no
API or CLI image-generation workflow was used. The character reference and
attribution are recorded in [`third_party_notices/elysia-pet.md`](../../../../../../third_party_notices/elysia-pet.md).

Regenerate the firmware resource with Pillow installed:

```sh
python3 scripts/generate_elysia_pet_assets.py
```

The script writes the twelve 96 × 104 RGB565A8 frames and their timing tables
to `main/apps/app_codex_status/assets/codex_pet_assets.cpp`. Previews go to the
ignored `artifacts/elysia-pet-preview/` directory. The atlas is cut at y=391
between the idle and typing rows; the script keeps one crop and scale per
animation. Each sequence reuses frame zero's body and alpha, then applies only
the authored eye and hand motion, so redraw noise does not make the silhouette
flicker. Frame order and durations remain defined in the generator.

The generator expects the current 2043 × 770 atlas. If its dimensions or grid
change, revise the row cut and frame extraction in the script before regenerating.
