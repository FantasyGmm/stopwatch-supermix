# Codex Agent Full-Info Ledger Design

## Summary

Replace the Codex pull-down Agent selector's focus rail with the approved
Option A, **Full-info Ledger**. Every authorized Agent remains selectable on
one 466 × 466 page and exposes the complete watch-side identity summary:
display name, truthful connection state, four-hex short identity, and last
connected time.

This redesign changes the default selection presentation and its management
entry point. Pairing, switching, voice cleanup, delete confirmation, reset
hold, credential rotation, BLE ownership, and close gestures remain
authoritative and unchanged.

## Source Of Truth

- Target: 466 × 466 circular AMOLED at 1× LVGL coordinates.
- Locked 8-Agent reference:
  `docs/superpowers/designs/2026-08-15-agent-selector-full-info-ledger/reference-8-agents.png`.
- This document and the locked PNG are authoritative. Earlier Focus Rail and
  multi-option browser pages are explicitly non-authoritative.
- The PNG is a browser-rendered reference. LVGL font antialiasing may differ,
  but geometry, hierarchy, colors, content, and states may not drift.

## Goals

- Show all 1–8 stored Agents on one page with no scrolling or empty slots.
- Give every row at least 44 px of touch height.
- Show the complete stored name without appending identity text to it.
- Always show a separate four-hex short ID so duplicate names remain clear.
- Show `Connected`, `Switch request`, `Switching...`, or `Offline` truthfully.
- Show `Now`, a bounded relative time, or `Unknown` without inventing data.
- Keep registry order stable when connection state changes.
- Move destructive actions to a separate management view so the selector stays
  visually quiet.

## Non-Goals

- Do not add device model, IP address, OS version, battery, signal strength, or
  any other field that is not stored on the watch.
- Do not change the registry capacity, persisted record format, pairing wire
  protocol, keys, epochs, or authorized-Agent lifecycle.
- Do not add rename, manual reorder, or bulk-select behavior.
- Do not redesign the pairing, delete-confirmation, reset-confirmation, empty,
  or transient-error surfaces in this change.

## Visual Contract

### Tokens

- Background: AMOLED black `#000000`.
- Primary text: `#F2F6F8`.
- Secondary text: `#788391`.
- Selection marker: `#49C9EE`.
- Connected: `#53D7A4`.
- Switch request: `#FFB454`.
- Offline: `#667180`.
- Drag handle and collapsed pull hint: secondary text at 72% opacity.
- Dividers: `#26313D` at 62% opacity.
- No row borders, rounded cards, or shadows.

ASCII Agent names use bundled Montserrat 16 or 18 depending on available row
height. Metadata uses Montserrat 14, the nearest bundled size to the 12 px
browser reference. Non-ASCII names continue using the bundled PuHui 14 font;
adding a larger CJK font is out of scope and is the only accepted type-size
deviation. The `AGENTS` title uses a dedicated Montserrat Bold 16 subset with
2 px letter spacing so it does not silently fall back to the medium-weight
built-in font.

### Fixed Regions

- Drag handle: `x=209, y=12, w=48, h=4`.
- Title `AGENTS`: centered at `y=28`, Montserrat 16 bold.
- `MANAGE`: centered below the title. Its combined header touch target spans
  `x=153, y=20, w=160, h=54`, ends exactly at the ledger boundary, and
  contains the visible Montserrat 14 label at `y=51`.
- Ledger zone: `y=74..426`, exactly 352 px high.
- Header-owned close gesture region ends at `y=74`; it never steals the first
  ledger row's touch area.
- Close hint: `x=193, y=438, w=80, h=4`.
- When collapsed, the panel rests at `y=-452`, leaving only a 14 px black
  safe strip. The same hint interpolates to local `y=456`, rendering at
  screen `y=4`; it does not cover the Codex mark, which begins at `y=22`.
  The gesture still owns the established top `0..70` px interaction region.

The exploratory mock placed `Manage` at the upper-right edge. Self-review
found that position clipped the circular safe area, so the locked design moves
it below the centered title. The title and Manage label share one nonoverlapping
header touch target; it never overlaps the first Agent row. No other concept
change is permitted.

### Dynamic Row Geometry

For `count` in `1..8`:

```text
row_height = clamp(floor(352 / count), 44, 66)
used_height = row_height × count
first_y = 74 + floor((352 - used_height) / 2)
row_y(index) = first_y + index × row_height
```

Each row is horizontally centered. Its width is derived from its vertical
center against a 220 px circular safe radius, minus 18 px of inset. It must
remain within the visible circle and may never be widened to a fixed minimum
that clips the top or bottom rows.

Each row uses two lines:

- Line 1: 8 px state dot, then the full display name.
- Line 2: textual state, `ID XXXX`, then right-aligned relative time.

The connected row adds a 3 px cyan marker and a symmetric horizontal cyan
wash: transparent at both edges, `#49C9EE` at 9.5% opacity in the center.
Requesting uses a transparent 2 px amber ring, a 13% outer halo, and amber
state text. Offline uses a 1 px outline dot and gray text. State styling never
changes row position.

## Data Contract

`AgentPanelRow` exposes:

- the existing fingerprint;
- the unmodified stored display name and byte count;
- a null-terminated four-hex uppercase short ID derived from fingerprint bytes
  0 and 1;
- the stored `last_connected_at` Unix timestamp in milliseconds;
- the derived row state.

Existing records whose `last_connected_at` predates `1577836800000`
(2020-01-01T00:00:00Z) are treated as migrated legacy data and display
`Unknown` until the Agent is selected or connects again. Connected always
displays `Now`; a pending switch displays `Awaiting`. Offline timestamps later
than the current clock display `Unknown`. Valid relative values use whole
minutes below 60 minutes and whole hours below 24 hours. The next 24 hours show
`Yesterday`, days 2–6 show the local weekday, days 7–29 show whole days, and
older values show `MM/DD`; they never show a negative duration. These rules
match the locked PNG's `Yesterday`, weekday, and multi-day examples.

New selection writes must use the synchronized system Unix clock, not boot
milliseconds. If the system clock is not yet valid, the previous stored value
is preserved rather than replaced with an untruthful timestamp.

## Interaction Contract

- Tapping any ledger row uses the existing `SwitchAgent` path for that exact
  fingerprint.
- Repeated taps are ignored while `_agent_switch_pending` is true.
- The already-connected row remains safe to tap and produces no duplicate
  transport side effect.
- `MANAGE` opens the existing focus-rail management view. That view preserves
  swipe-left Delete, reset, confirmations, and 1.2 s reset hold.
- The management view has a centered `DONE`/back action in the same header
  safe region. Closing the panel or destroying the app resets the next open to
  the ledger.
- Pull-down open, bottom close, header close, Home, wake consumption, and
  global edge-control ownership remain unchanged.

## State Matrix

| State | Ledger behavior |
| --- | --- |
| 0 Agents | Existing empty state; `MANAGE` hidden |
| 1 Agent | One 66 px row centered in the ledger zone |
| 5 Agents | Five 66 px rows centered with no scrolling |
| 8 Agents | Eight 44 px rows filling `y=74..426` |
| Connected | Mint dot, cyan marker/wash, `Connected`, `Now` |
| Switch request | Amber mark/text, `Switch request`, `Awaiting` |
| Switching | Cyan mark/text, `Switching...`, taps ignored |
| Offline | Gray outline/text and stored relative time or `Unknown` |
| Duplicate names | Names remain identical; separate IDs disambiguate |
| Long name | Use the stored name unchanged; only LVGL's final line bounds may ellipsize |
| Pairing request | Existing dedicated approval view replaces the ledger |
| Delete/reset confirm | Existing dedicated confirmation view replaces management |
| Notice/error | Existing 2 s notice; truthful row state remains unchanged |

## Verification Contract

- Host tests cover row propagation, short IDs, legacy timestamp handling,
  relative-time boundaries, and exact 1/5/8 geometry.
- The existing Agent panel, registry, pairing, and voice-related host tests
  remain green.
- A clean or incremental firmware build proves the LVGL implementation
  compiles; it does not prove visual fidelity.
- After flashing, capture the physical 8-Agent selector, 1-Agent selector,
  empty state, management state, request state, and switching state.
- Compare the physical 8-Agent state against the locked PNG region by region:
  header, first/last safe widths, row heights, type hierarchy, status colors,
  close hint, and absence of clipping. The change is not visually complete
  until this physical gate is recorded.
