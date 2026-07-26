# Assistant Soft Spark Icon Design

## Goal

Replace the current Assistant launcher icon with the approved Soft Spark Core design while preserving the visual scale and glow language of the existing StopWatch launcher icon family.

## Scope

- Replace only the `icon_assistant` launcher asset.
- Keep the Assistant app name, launcher behavior, and runtime UI unchanged.
- Keep the existing asset contract: 200 x 200 pixels, black background, LVGL `RGB565`, and the exported symbol name `icon_assistant`.

## Approved composition

- One large four-point star centered at `(100, 100)`.
- Two smaller four-point stars placed diagonally around the main star: one toward the upper left and one toward the lower right.
- The main star carries the strongest cyan-to-violet gradient and a small white center highlight.
- The two secondary stars are quieter: the upper-left star leans cyan-white, while the lower-right star leans violet.
- All three stars use soft, boundary-free glow that fades directly into the pure-black canvas.

The icon must not contain a circle, orbit, enclosing ring, badge outline, container, or visible frame around the main star.

## Family consistency and size

The existing launcher icons use bright central subjects with soft peripheral glow. At a brightness threshold of 40/255, their visible bounds are generally 148-184 pixels wide and 148-173 pixels high. The new Assistant icon should target:

- Main visible subject and strong glow: approximately 155-170 pixels on the longer axis.
- Faint outer glow: approximately 180-190 pixels on the longer axis.
- At least 5 pixels of pure-black safety margin on every canvas edge.
- Optical center aligned with the other 200 x 200 launcher icons; the two small stars may create slight diagonal balance without making the icon appear shifted.

The icon must remain recognizable when previewed at both 72 x 72 and 45 x 45 pixels.

## Color and light

- Canvas: `#000000`.
- Main highlight: white to ice cyan.
- Main body: cyan through cool blue into soft violet.
- Glow: cyan and violet with smooth alpha falloff and no hard circular boundary.
- Avoid excessive bloom that merges the three stars or washes out the four-point silhouettes.

## Implementation

Create the approved icon deterministically at a higher working resolution, downsample it with high-quality filtering to 200 x 200, quantize it to RGB565, and replace `main/assets/images/icon_assistant.c` without changing the descriptor dimensions or symbol name.

Keep any generation or conversion helper outside the shipped firmware source unless it is already part of the repository's asset pipeline. Do not add a new runtime rendering path for this icon.

## Verification

- Confirm the descriptor remains 200 x 200, RGB565, and exactly 80,000 bytes.
- Decode the generated C asset back to a PNG and visually compare it beside the existing launcher icons.
- Measure brightness-threshold bounds to confirm the icon stays within the target footprint.
- Verify no circular or ring-shaped pixels are present around the main star.
- Build the unified firmware and run existing firmware asset/contract tests.
- Flash the watch through the backup-gated workflow and confirm the launcher displays the new icon at a size consistent with adjacent icons.
