# Codex Beacon Spine UI Design

## Goal

Replace the current symmetric instrument-frame layout on the 466 × 466 Codex status page with the user-approved **A · Beacon Spine** direction while preserving every existing data source, state transition, control, and image asset.

## Fidelity Contract

The implementation follows the approved second-round mockup, not the previous Precision Console spec.

- Keep the existing Codex logo asset, scale, and compact centered brand block.
- Keep the connection state centered below the brand and do not render the configured host name.
- Reuse the original 96 × 104 pet frames, frame order, durations, and disabled antialiasing.
- Render the pet at `110 × 120` without a surrounding frame, preserving its center and leaving an 11 px gap to the spine.
- Shift the pet, spine, and telemetry group 24 px right from the previous revision so its visible content reads as centered.
- Use one 2 px violet-blue vertical spine between the pet and telemetry.
- Stack `MODEL` and `EFFORT` on the right, left-aligned on one shared axis.
- Give the model value enough width for `gpt-5.6-sol` before ellipsis.
- Lay out each quota row as `window → progress track → percentage`, with percentages aligned against the right safe edge.
- Use a mint quota fill so quotas are visually distinct from connection state and alerts.
- Separate the alert channel with a restrained horizontal rule.
- Keep alert heading, project, detail, and battery centered.
- Reuse the existing `MontserratSemiBold26` asset for the English primary hierarchy instead of introducing another font file.

No new information, interaction, animation, font, or raster asset is introduced.

## Exact Layout

Coordinates use the 466 × 466 screen's top-left origin.

- Logo: `x=168, y=22`, existing scale `170`.
- Brand text: `x=218, y=32`.
- Connection status: centered at `y=63`, width `180`.
- Host name: not rendered; no placeholder or replacement text.
- Pet image: `x=73, y=88, w=110, h=120`; no dock, border, card, or backdrop.
- Beacon spine: `x=194, y=96, w=2, h=104`.
- Telemetry column: `x=214, w=200`; right edge remains fixed at `x=414`.
- Model caption/value: `y=98 / 120`.
- Effort caption/value: `y=154 / 176`.
- Quota band: `x=55, w=356`.
- 5H row: `y=220`; 7D row: `y=260`.
- Quota columns: label begins at `x=55`, track begins at `x=101` and is `248 × 8`, percentage begins at `x=351` with width `60` and right alignment.
- Alert divider: `x=82, y=304, w=302`.
- Alert heading/project/detail: centered at `y=315 / 346 / 382`.
- Battery: centered at `y=438`.

The pet, telemetry, quotas, divider, and alert group retain the approved 16 px upward shift after removing the host-name row. The battery is deliberately moved down to `y=438` in this typography refinement.

## Typography Hierarchy

- Brand, model value, effort value, quota percentage, alert heading, and ASCII project name use the existing `MontserratSemiBold26` font.
- Alert heading remains state-colored: vermilion for pending, beacon violet for running, and handled green for completed.
- ASCII project names such as `StopWatch` use `MontserratSemiBold26`. Project names containing non-ASCII characters fall back to the existing `font_puhui_14_1` so no glyph is lost.
- Alert detail remains `font_puhui_14_1` for full Chinese coverage, but changes from etched gray to secondary light gray `#B8C2CC`.
- Connection state, `MODEL`/`EFFORT` captions, and `5H`/`7D` labels keep their current fonts and sizes. This preserves contrast between labels and live values.
- The larger quota percentage uses `x=351, w=60`, keeping its right edge fixed at `x=411`.

## Visual Tokens

- Pure black `#000000`: background.
- Panel graphite `#27303E`: divider and quota tracks.
- Beacon violet `#7C9DFF`: connection state, spine, and project.
- Signal white `#F2F6F8`: primary values and percentages.
- Etched gray `#788391`: captions and battery.
- Secondary light gray `#B8C2CC`: alert detail.
- Quota mint `#53D7A4`: quota fills.
- Alert vermilion `#FF6248`: pending and error states.
- Existing handled green remains unchanged for handled/charging semantics.

No gradients, shadows, glow, rounded cards, or decorative red model frame are allowed.

## Behavior Preservation

The redesign changes only widget construction and coordinates. BLE/Wi-Fi handling, configured host storage, JSON parsing, quota mapping, alert carousel ordering, edge controls, screen-off behavior, battery updates, and pet timer behavior remain untouched. Host updates may still occur internally but have no visible label.

## Verification

- Native layout-contract test locks exact bounds, ordering, and the model-width requirement.
- The layout contract verifies that the enlarged pet stays clear of the spine and that quota percentages follow their progress tracks.
- The layout contract verifies the shifted group coordinates and preserves the telemetry right edge at `x=414`.
- The source contract verifies that no host-name label is constructed while connection status remains visible.
- The source contract verifies reuse of `MontserratSemiBold26`, ASCII/non-ASCII project-font fallback, the brighter detail color, and the lower battery coordinate.
- Existing Codex model and carousel tests must still pass.
- Full ESP-IDF firmware build must pass.
- Source inspection must confirm the original logo and pet descriptors are still used.
- Hardware visual validation remains required to prove physical-panel fidelity; if a device is connected, flash the verified build and inspect it on-device.
