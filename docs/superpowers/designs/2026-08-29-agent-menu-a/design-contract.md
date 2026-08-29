# Agent Menu A Visual Contract

## Source of truth

- Reference: `reference.png`
- Target: macOS status-item root menu, dark appearance
- Reference size: 430 x 372 px at 1x; implement as 430 pt wide with the
  content-driven reference height
- The approved browser prototype is authoritative. The system-selected row,
  font antialiasing, menu shadow, and submenu chrome may follow macOS.

## Geometry

- Menu surface: 430 pt wide, 10 pt corner radius, 7 pt vertical inset.
- Health block: 12 pt horizontal inset, 8 pt top inset, 10 pt bottom inset.
- Codex and Trae rows: 52 pt high.
- Device & Voice row: 40 pt high.
- Action rows: 27 pt high.
- Dividers: 1 pt line with 5 pt vertical margin.
- Standard row horizontal inset: 12 pt; status dot-to-content gap: 8 pt.
- Status dots: 7 x 7 pt circles.
- Version badge: 52 x 19 pt, 12 pt from the right edge; its vertical center
  aligns to the health title within 0.5 pt. The version text is centered in
  the pill (never right-aligned), with a 9.5 pt radius.

## Typography

- Family: SF Pro Text / macOS system text.
- Base/action text: 13 pt regular.
- Source and device titles: 13 pt semibold (weight 650 in the prototype).
- Health title: 14 pt semibold.
- Source secondary text: 11 pt regular.
- Right secondary task text: 10 pt regular.
- Version badge: 11 pt regular.
- Two-line rows use a compact 1.25 line-height and truncate the secondary line
  at the tail before displacing the right-side status.

## Color and material

- Menu surface: `rgba(33, 34, 39, 0.98)`.
- Primary text: `#edf1f7`.
- Secondary text: `#8c94a2`.
- Divider: `#2c3038`.
- Border: `rgba(255, 255, 255, 0.14)`.
- Connected/device dot: `#5bd69b`.
- Codex/pending dot and Pending text: `#ffb35a`.
- Trae dot: `#69a7ff`.
- Version badge: foreground `#b5d1ff`, background `#25344a`.
- Hover/selected action fill: `#2864d7` with primary text retained.

## Content and ownership

- Health: connected dot, `StopWatch <connection state>`, version badge, then
  `<Agent name> · <voice readiness>`.
- Codex: orange dot; left side contains source name and
  model/effort/5H/7D; right side contains Pending on the first line and
  Running/Completed on the second line; chevron opens the existing Codex
  submenu.
- Trae: blue dot; left side contains source name and online/model/effort;
  right side contains project then state/task title; chevron opens the
  existing Trae submenu.
- Device & Voice: state-colored dot, title and Bluetooth/voice summary;
  chevron opens the existing device submenu.
- `更改桌面 GIF` remains a visible root action and opens the existing
  conversion, preview, upload, restore, and cancellation window.
- Existing reconnect, Agent settings, diagnostics, contextual repair, and quit
  actions remain reachable. No global task entry is introduced.

## State coverage

- Connected + voice ready: reference state.
- Disconnected/scanning: health and device dots use secondary or error color,
  with truthful status text.
- Codex counts: zero values remain visible; Pending uses orange only when
  nonzero.
- Trae offline/stale: secondary/error state replaces Online without changing
  layout.
- Contextual voice/accessibility/pairing repairs remain hidden until needed.
- Hover and activation are required for every actionable root row, including
  the custom pet action; informational text remains non-interactive.

## Verification

- Capture the installed native menu at 2x and normalize it to 1x before image
  comparison with `reference.png`.
- Compare by region: health, Codex, Trae, device, actions, and menu surface.
- Automated tests protect data ownership, row geometry, typography, color,
  submenu routing, and action reachability; the installed screenshot remains
  the visual acceptance gate.
