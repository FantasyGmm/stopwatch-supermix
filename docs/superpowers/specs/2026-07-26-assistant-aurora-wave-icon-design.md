# Assistant Aurora Wave Icon Design

## Goal

Replace only the Assistant launcher icon with the user-selected H2 Aurora Wave design while preserving the StopWatch launcher icon family's scale, black canvas, rounded geometry, and soft glow.

## Approved design

- Five centered vertical waveform bars with rounded ends.
- Bar heights form a symmetric `42 / 80 / 124 / 80 / 42` silhouette at 200 x 200 output size.
- The bars use a single diagonal aurora gradient from teal through cyan-blue to restrained violet; no bar is pure white.
- A soft cyan-violet glow surrounds the waveform without a circle, ring, frame, or hard boundary.
- The design remains legible at 72 x 72 and 45 x 45 pixels.

## Family constraints

- Canvas remains pure black.
- The visible subject stays centered and has similar optical weight to Setup, Badge, and Codex.
- Cyan remains the dominant family color; blue adds depth and violet is limited to the gradient tail and glow.
- No runtime drawing, animation, launcher layout, label, or Assistant behavior changes.

## Asset contract

- Output: `main/assets/images/icon_assistant.c`.
- LVGL format: `LV_COLOR_FORMAT_RGB565`.
- Dimensions: 200 x 200.
- Byte count: 80,000.
- Exported symbol remains `icon_assistant`.
- Generation remains deterministic through `scripts/generate_assistant_icon.py`.

## Verification and release scope

- Update the focused asset contract first and observe it fail against the current star asset.
- Generate the H2 asset, run the focused contract, and inspect decoded 200 px, 72 px, and 45 px previews beside the launcher family.
- Build the unified firmware; do not run unrelated runtime test suites unless the focused checks or build expose a broader issue.
- Use the existing full-flash-backup-gated flash workflow, validate boot to the launcher, and restore the Mac Codex Agent.
- Physical on-watch appearance remains a separate visual acceptance check after flashing.
