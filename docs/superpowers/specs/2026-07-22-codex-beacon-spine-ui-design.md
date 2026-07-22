# Codex Beacon Spine UI Design

## Goal

Replace the current symmetric instrument-frame layout on the 466 × 466 Codex status page with the user-approved **A · Beacon Spine** direction while preserving every existing data source, state transition, control, and image asset.

## Fidelity Contract

The implementation follows the approved second-round mockup, not the previous Precision Console spec.

- Keep the existing Codex logo asset, scale, and compact centered brand block.
- Keep connection state and host centered below the brand.
- Reuse the original 96 × 104 pet frames, frame order, durations, and disabled antialiasing.
- Place the pet in a quiet outlined dock on the left.
- Use one 2 px violet-blue vertical spine between the pet and telemetry.
- Stack `MODEL` and `EFFORT` on the right, left-aligned on one shared axis.
- Give the model value enough width for `gpt-5.6-sol` before ellipsis.
- Lay out each quota row as `window → percentage → progress track`.
- Use a mint quota fill so quotas are visually distinct from connection state and alerts.
- Separate the alert channel with a restrained horizontal rule.
- Keep alert heading, project, detail, and battery centered.

No new information, interaction, animation, font, or raster asset is introduced.

## Exact Layout

Coordinates use the 466 × 466 screen's top-left origin.

- Logo: `x=168, y=22`, existing scale `170`.
- Brand text: `x=218, y=32`.
- Connection status: centered at `y=63`, width `180`.
- Host: centered at `y=84`, width `320`.
- Pet dock: `x=52, y=112, w=104, h=104`.
- Pet image: centered within the dock at `x=56, y=112`, native `96 × 104`.
- Beacon spine: `x=170, y=112, w=2, h=104`.
- Telemetry column: `x=190, w=224`.
- Model caption/value: `y=114 / 136`.
- Effort caption/value: `y=170 / 192`.
- Quota band: `x=55, w=356`.
- 5H row: `y=236`; 7D row: `y=276`.
- Quota columns: label `32`, percentage `70`, track begins at `x=180` and is `231 × 8`.
- Alert divider: `x=82, y=320, w=302`.
- Alert heading/project/detail: centered at `y=337 / 367 / 394`.
- Battery: centered at `y=435`.

## Visual Tokens

- Instrument black `#070A0F`: background.
- Panel graphite `#27303E`: pet dock, divider, and quota tracks.
- Beacon violet `#7C9DFF`: connection state, spine, and project.
- Signal white `#F2F6F8`: primary values and percentages.
- Etched gray `#788391`: captions, host, detail, and battery.
- Quota mint `#53D7A4`: quota fills.
- Alert vermilion `#FF6248`: pending and error states.
- Existing handled green remains unchanged for handled/charging semantics.

No gradients, shadows, glow, rounded cards, or decorative red model frame are allowed.

## Behavior Preservation

The redesign changes only widget construction and coordinates. BLE/Wi-Fi handling, JSON parsing, quota mapping, alert carousel ordering, edge controls, screen-off behavior, battery updates, and pet timer behavior remain untouched.

## Verification

- Native layout-contract test locks exact bounds, ordering, and the model-width requirement.
- Existing Codex model and carousel tests must still pass.
- Full ESP-IDF firmware build must pass.
- Source inspection must confirm the original logo and pet descriptors are still used.
- Hardware visual validation remains required to prove physical-panel fidelity; if a device is connected, flash the verified build and inspect it on-device.
