# Codex Status UI Redesign

## Goal

Redesign the 466 × 466 StopWatch Codex status page as a precise, professional instrument panel without adding, removing, or changing any information or behavior.

The selected direction is **Precision Console**. It improves hierarchy, alignment, and round-screen fit while preserving the existing monitor's data, animation, carousel, and transport states.

## Scope

This redesign changes only LVGL presentation in `AppCodexStatus::create_view()` and the small view helpers it uses.

It preserves:

- the Codex logo and title;
- connection status and host name;
- model and reasoning-effort values;
- the animated pixel pet;
- 5-hour and 7-day quota percentages and bars;
- alert heading, project, and detail;
- battery state;
- all existing data parsing, BLE/Wi-Fi transport, alert carousel, timing, chimes, and state transitions.

No new status, label, icon, interaction, animation, font, or image asset is introduced.

## Asset Reuse

The redesign must reuse the existing assets without modification:

- Codex logo: `logo_codex` from `assets/logo_codex.c`;
- pet frames: `codex_pet_idle_frames` from `assets/codex_pet_assets.cpp`;
- pet frame durations: `codex_pet_idle_durations_ms`;
- existing Montserrat and Puhui fonts already linked into the firmware.

The pet keeps its native frame dimensions, current frame sequence, current timing, and disabled antialiasing. The logo keeps its current LVGL scale of 170. Assets may move but must not be redrawn or transformed into replacements.

## Visual Direction

The page should feel like a compact precision instrument rather than a collection of generic cards. The visual signature is a symmetric model–pet–effort assembly in the upper half, followed by aligned quota telemetry and a clearly separated alert channel.

The design is flat and restrained:

- no gradients;
- no blur or glow;
- no drop shadows;
- no decorative grid;
- no large rounded cards;
- color is used for status semantics, not decoration.

## Layout

Coordinates use the top-left corner as `(0, 0)` and target the existing 466 × 466 panel.

### Brand and connectivity

- Logo: current asset near `x=168, y=22`, preserving the existing scale.
- `Codex`: immediately to the logo's right, vertically centered with it.
- Connection status: centered at `y=63`, maximum width 180 px.
- Host: centered at `y=84`, maximum width 320 px, single-line ellipsis for overflow.

The brand remains compact. Connection status is stronger than the host; the host is supporting information.

### Model, pet, and effort instrument

The main instrument occupies `x=43–423, y=108–212`.

- Model instrument: `x=54, y=112`, size `116 × 92`.
- Pet: centered on `x=233`, with the same native size and animation, visually centered near `y=158`.
- Effort instrument: `x=296, y=112`, size `116 × 92`.

The two existing arcs remain but become mirrored brackets around the pet. They use equal size, weight, and visual density. The model and effort values remain the only text inside their respective instruments; no `MODEL` or `EFFORT` captions are added.

A one-pixel, etched-gray horizontal calibration line connects the two instruments. It breaks across the full pet image width plus 8 px of clearance on each side and does not cross the image.

### Quotas

The quota rows occupy the screen's widest central band:

- 5H row: `x=49, y=225`, width 368 px;
- 7D row: `x=49, y=268`, width 368 px.

Each row uses the same three-column alignment:

1. fixed-width `5H` or `7D` label;
2. progress track;
3. right-aligned percentage.

Tracks are 8–10 px high with a maximum 2 px corner radius. Both rows share identical baselines and column widths so their percentages can be compared without eye movement.

### Alert channel

A one-pixel, etched-gray divider separates telemetry from alerts at `x=73, y=310`, with a width of 320 px and a 28 px center gap.

- Alert heading: centered at `y=324`, maximum width 310 px.
- Project: centered at `y=354`, maximum width 340 px, single-line ellipsis.
- Detail: centered at `y=382`, maximum width 320 px, single-line ellipsis.

The alert heading carries state color. The project is the strongest alert body text. Detail remains secondary.

### Battery

Battery remains centered in the bottom tip area near `y=435`, with its current text content and update behavior. It stays visually quiet except for existing charging and low-battery state colors.

## Color System

Use the following palette:

- instrument black `#070A0F`: page background;
- panel graphite `#121823`: arc tracks and quota tracks;
- calibration blue `#54C7EC`: Codex brand, healthy/live states, quota fill, and effort value;
- signal white `#F2F6F8`: model value, percentages, and primary text;
- etched gray `#788391`: host, divider, battery, and secondary detail;
- alert vermilion `#FF6248`: pending alerts, connection errors, and other states requiring attention;
- existing handled green: retain the current semantic color for all-handled and charging states.

Red must no longer be a permanent decorative color for the model arc. It is reserved for actionable or abnormal states. Offline, waiting, connecting, and live states continue to use their existing meanings, expressed through this reduced palette.

## Typography

Reuse existing firmware fonts only:

- `Codex`: Montserrat 24;
- connection status: Montserrat 14;
- host and battery: Montserrat 14;
- model: Montserrat 14, with existing ellipsis behavior;
- effort: Montserrat 18;
- quota labels: Montserrat 18 or the smallest existing size that preserves the current hierarchy;
- quota percentages: Montserrat 24;
- alert heading: Montserrat 18;
- project and detail: existing Puhui 14 font for Chinese compatibility.

Do not add font files or glyph ranges. Numeric alignment must be achieved through fixed widget widths and right alignment rather than a new monospace font.

## State Rendering

All current states remain functional. The redesign changes only their presentation:

- live/healthy: calibration blue;
- connecting or unavailable information: etched gray;
- pending/action required or connection error: alert vermilion;
- all handled/charging: existing green;
- primary values: signal white unless the value itself carries an existing state color.

Alert rotation, pending-first ordering, handled state, title truncation, and the distinction between `已完成` and `等待指令` are unchanged.

## Round-Screen Constraints

- Keep important text at least 42 px from the circular edge at its vertical position.
- Put the widest information in the screen's middle band.
- Keep the top brand and bottom battery narrow.
- Avoid rectangular containers whose corners appear clipped by the circular display.
- Verify the layout using the actual 466 × 466 framebuffer, not a rectangular desktop preview alone.

## Implementation Boundaries

The implementation may introduce small private LVGL presentation helpers for mirrored instruments, dividers, and aligned quota rows. It must not change the status model, protocol payload, alert carousel rules, timer behavior, transport code, or Mac agent.

Existing working-tree changes are outside this design and must be preserved. Implementation should be limited to the Codex status view and any directly required declaration changes.

## Verification

The completed UI must be checked in representative states:

1. startup with unknown values;
2. BLE live with ordinary model and effort strings;
3. Wi-Fi fallback with a long host name;
4. pending alert with a long project and Chinese detail;
5. all alerts handled;
6. no alerts;
7. low battery and charging;
8. long model name and maximum percentage widths.

Verification must confirm:

- no overlap or circular-edge clipping;
- all original elements remain visible;
- pet animation and logo render from the original assets;
- no change to carousel or status behavior;
- the firmware still builds within the existing asset and partition constraints.
