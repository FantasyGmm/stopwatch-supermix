# Assistant Aurora Wave Icon Design

## Goal

Replace only the Assistant launcher icon with the user-selected H2 Aurora Wave design while preserving the StopWatch launcher icon family's scale, black canvas, rounded geometry, and soft glow.

## Approved design

- Five centered vertical waveform bars with rounded ends.
- The H2 reference uses a symmetric `42 / 80 / 124 / 80 / 42` silhouette. The watch asset scales the five bars uniformly by about 10% to `46 / 88 / 136 / 88 / 46`, following the user's on-device size correction.
- Each bar independently uses the H2 diagonal gradient from teal `#4AE7D5`, through cyan-blue `#42B6F0` at 46%, to restrained violet `#8064F0`; no bar is pure white.
- The glow follows the five waveform silhouettes instead of using a centered radial field. The approved contour treatment removes the center circle entirely and composites two gradient-colored layers behind the bars: a 7 px inner halo at alpha 190 that hugs every rounded edge, plus a 26 px outer halo at alpha 110 that falls off smoothly. The outer layer is deliberately wider and stronger than the prior 18 px / alpha 90 treatment so its low-brightness footprint matches the larger launcher-family glows.
- The design remains legible at 72 x 72 and 45 x 45 pixels.

## Family constraints

- Canvas remains pure black.
- The visible subject stays centered and has similar optical weight to Setup, Badge, and Codex.
- Cyan remains the dominant family color; blue adds depth and violet is limited to the gradient tail and glow.
- The glow must be clearly visible around the outside edge of all five bars against pure black. It must not concentrate only behind the center bar, and its outer edge must fade before the canvas boundary without producing a circle, rectangle, or clipped edge.
- No runtime drawing, animation, launcher layout, label, or Assistant behavior changes.

## Asset contract

- Output: `main/assets/images/icon_assistant.c`.
- LVGL format: `LV_COLOR_FORMAT_RGB565`.
- Dimensions: 200 x 200.
- Byte count: 80,000.
- Exported symbol remains `icon_assistant`.
- Generation remains deterministic through `scripts/generate_assistant_icon.py`.

## Verification and release scope

- Update the focused asset contract first and observe it fail against the current 18 px / alpha 90 contour glow.
- Generate the refined H2 asset, run the focused contract, and inspect decoded 200 px, 72 px, and 45 px previews beside the launcher family. At the low-brightness threshold, the Assistant footprint should expand from about 174 px toward 190 px without changing the five bright bar silhouettes.
- Build the unified firmware; do not run unrelated runtime test suites unless the focused checks or build expose a broader issue.
- For this asset-only change, flash the verified build directly without a new full-flash backup, then validate boot to the launcher and confirm the Mac Codex Agent remains running.
- Physical on-watch appearance remains a separate visual acceptance check after flashing.
