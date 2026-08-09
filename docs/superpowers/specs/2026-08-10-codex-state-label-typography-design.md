# Codex State Label Typography Design

## Status

Approved visual direction: **C · 22 px strong emphasis**.

The source of truth is `design/reference/codex-state-labels-c.png`, captured
from the approved `c-22px` watch mockup at a 466 x 466 viewport. This
specification records the production values represented by that mockup so
implementation and visual verification do not depend on browser font
inference.

## Goal

Make the Codex page's `PENDING` and `RUNNING` labels easier to read at a glance
on the watch without changing their meaning, colors, glow behavior, counts, or
the surrounding information architecture.

## Scope

### Included

- The English `PENDING` and `RUNNING` labels on the Codex page.
- A bundled Montserrat Bold font at 22 px.
- The label geometry needed to fit the larger glyphs without clipping.
- A four-pixel downward adjustment to the numeric counts so the approved
  label-to-count spacing is preserved.
- Contract tests for the font and layout values.
- A 466 x 466 design-reference image and a same-size implementation capture
  for visual comparison.

### Excluded

- The TRAE page's `待确认` and `运行中` labels.
- Count font size, weight, color, horizontal position, or content.
- State colors, glow timing, carousel behavior, task content, quota layout,
  page indicators, battery indicator, or BLE data.
- Any localization or wording change.

## Design Contract

The target canvas remains 466 x 466 pixels at 1x scale on a black background.

| Property | `PENDING` | `RUNNING` |
| --- | ---: | ---: |
| Text | `PENDING` | `RUNNING` |
| Font | Montserrat Bold 22 px | Montserrat Bold 22 px |
| Color | `#FF6248` | `#49C9EE` |
| X | 54 | 300 |
| Y | 272 | 272 |
| Width | 112 | 112 |
| Alignment | centered | centered |

The count labels remain Montserrat SemiBold 26 px and retain their current
colors and horizontal geometry:

- pending count: `x=78`, `width=64`;
- running count: `x=324`, `width=64`.

Both counts move from `y=294` to `y=298`, matching the approved C mockup and
preventing the larger 22 px state labels from crowding the numerals.

All other Codex layout constants remain unchanged. In particular, the alert
divider stays at `y=258`, task index at `y=284`, project at `y=307`, detail at
`y=359`, page indicators at `y=416`, and battery at `y=432`.

## Component and State Behavior

The existing label objects remain the implementation boundary. Both labels
continue to use centered fixed-width LVGL labels and remain registered with
`CodexPendingSoftGlow` alongside their counts.

The typography and geometry apply in every data state:

- pending active;
- running active;
- no alerts;
- disconnected or stale transport;
- screen-on glow phases and screen-off static rendering.

Only opacity and existing state colors may vary at runtime. Font, size,
position, width, alignment, and wording do not vary by state.

## Font Asset

Production uses a generated LVGL C font named `MontserratBold22`, sourced from
the same Montserrat Bold face and ASCII glyph subset as `MontserratBold16`.
It is declared through the shared assets header and compiled through the
existing asset CMake source list. No runtime font scaling is used.

## Verification

Automated contract tests must verify:

- `MontserratBold22` is declared, compiled, and reports a 22 px source size;
- both state labels use `MontserratBold22`;
- label positions are `x=54/300`, `y=272`, and `width=112`;
- both counts use `y=298` while retaining existing horizontal geometry;
- the TRAE page font and layout are unchanged;
- glow targets remain connected to both state labels and counts.

Visual verification must compare a 466 x 466 implementation screenshot against
the approved C reference at the same scale. Review order is geometry, label
clipping, type weight and size, label-to-count spacing, then unchanged colors
and surrounding content. Any remaining pixel difference must be documented;
there are no pre-approved visual deviations.
