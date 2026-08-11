# Codex Agent Focus Rail Design

## Summary

Replace the Codex application's dense pull-down Agent rows with the approved
Focus Rail direction. The panel remains the only watch surface for choosing an
Agent, but it presents one strong center target, two nearby rows, and two faded
context rows instead of eight equal cards and eight permanent delete buttons.

The redesign changes presentation and touch routing only. Existing registry,
pairing, credential rotation, voice cleanup, and BLE connection behavior remain
authoritative.

## Source Of Truth

- Target canvas: 466 × 466 px, circular AMOLED, 1× LVGL coordinates.
- Approved concept: browser option A, `Focus Rail · 聚焦列表`.
- This document is the authoritative contract for implementation. The earlier
  three-option browser page remains exploratory and is not an implementation
  reference.
- Existing Codex palette tokens remain authoritative where named below.

## Goals

- Make the currently selected Agent unmistakable at a glance.
- Preserve all existing switching, pairing, deletion, reset, and recovery
  behavior for up to eight stored Agents.
- Remove the visual noise caused by equal-weight cards and permanent red delete
  buttons.
- Keep every primary touch target at least 44 × 44 px.
- Prevent list scrolling from accidentally closing the pull-down panel.

## Non-Goals

- Do not change the registry format, maximum Agent count, pairing protocol,
  key rotation, BLE ownership, or Mac Agent behavior.
- Do not add watch-side rename or reordering controls.
- Do not alter the underlying Codex or TRAE status pages.
- Do not make destructive actions single-step.

## Visual Tokens

The panel uses the existing Codex visual language:

- Background: `#080B10`.
- Primary text: `kSignalWhite` / `#F2F6F8`.
- Secondary text: `kEtchedGray` / `#788391`.
- Selection accent: `kRunningCyan` / `#49C9EE`.
- Connected: `kQuotaMint` / `#53D7A4`.
- Requesting switch: `#FFB454`.
- Offline: `#667180` with an outlined status dot.
- Destructive: `kAlertVermilion` / `#FF6248`.
- Pairing primary action: `kBeaconViolet` / `#7C9DFF`.

All text uses bundled Montserrat fonts. The focused Agent name uses Montserrat
SemiBold 22. Adjacent names use Montserrat SemiBold 18. Outer context names use
Montserrat SemiBold 16. Status labels use Montserrat 12 or the nearest bundled
size; no runtime font loading is permitted. Pairing codes use Montserrat
SemiBold 36 with explicit `3 + 3` grouping.

## Default Panel Geometry

The full-screen overlay remains 466 × 466 and slides from hidden `y=-430` to
visible `y=0`.

- Drag handle: `x=209, y=12, w=48, h=4`, radius 2.
- Title `AGENTS`: centered at `y=40`, Montserrat 16 SemiBold.
- Stored count: centered at `y=62`, for example `5 PAIRED`, Montserrat 12.
- Rail viewport: `x=32, y=78, w=402, h=300`.
- Fixed focus band: `x=39, y=195, w=388, h=60`.
- Focus center: `y=225`.
- Row center spacing: 58 px.
- Visible row centers: `109, 167, 225, 283, 341`.
- Bottom reset target: `x=139, y=390, w=188, h=44`.
- Close hint: `x=193, y=438, w=80, h=4`.

The focus band is not a card. It is composed from two borderless gradient
halves fading from transparent at the outer edges to a low-opacity cyan wash at
the center. A `3 × 28` cyan marker at `x=42, y=211` identifies the focus row.
No row uses a shadow or permanent outline.

Five reusable row slots render the visible portion of the stored-Agent list.
The focused row uses the largest type and full opacity. Adjacent rows use 72%
opacity; outer rows use 38%. Rows outside the stored range are hidden. The list
supports 0–8 records, clamps at both ends, and snaps the nearest record to the
focus center after a vertical drag.

Each row reserves:

- 52 px minimum touch height.
- An 8 px status dot.
- A single-line Agent name with ellipsis.
- A right-aligned state label that is never compressed by the name.

Duplicate names keep the existing four-hex-digit identity suffix before
ellipsis is applied.

## State Semantics

Selection and connection are distinct:

- `Connected`: mint filled dot and `Connected` label.
- `Requesting switch`: amber filled dot and `Switch request` label. Tapping the
  row approves the existing authorized Agent switch through the current switch
  path.
- `Offline`: gray outlined dot and `Offline` label. Tapping still selects that
  Agent as the preferred target and waits for its authenticated reconnect. It
  is not disabled.
- `Switching`: cyan progress arc and `Switching...`; repeated row taps are ignored
  while the existing switch transaction is pending.
- Switch failure: the row returns to its truthful connection state and a
  concise `Couldn't switch` message appears above the reset target for 2 s.

When voice capture is active, the existing bounded voice cleanup finishes
before the active connection changes. The UI stays in `Switching...` until the
switch completes or fails.

## Gestures And Selection

- Pull down from the top 70 px of the Codex application and move at least 48 px
  to open the panel, preserving the existing entry gesture.
- Vertical movement beginning inside the rail scrolls the Agent list only. It
  never moves or closes the panel.
- Vertical movement beginning in the title region `y=0–78` moves the panel.
  An upward displacement of at least 48 px closes it.
- Tapping a visible row executes the existing switch action for that record.
  The row first animates into the focus position over 160 ms, then the switch
  transaction starts. The focused row may be tapped again without side effects
  when it is already the active Agent.
- Snapping completes in 160 ms with no looping or decorative motion.

## Deletion

A horizontal left drag of at least 48 px beginning on a row reveals a 76 px
`Delete` action on the right. The delete action uses vermilion text on the base
background rather than a permanent red card.

Tapping `Delete` opens a full flat confirmation state:

- Title: `Delete Agent?`, centered at `y=144`, Montserrat 22.
- Agent name: centered at `y=180`, Montserrat 18.
- Destructive action: `x=82, y=240, w=302, h=54`, `Delete`.
- Cancel action: `x=133, y=310, w=200, h=48`, `Cancel`.

Confirming calls the current remove-and-rotate flow. Removing the active Agent
disconnects it. Pairing credentials rotate exactly as they do today. Failure
returns to the rail and shows `Couldn't delete` without removing the row.

Any vertical scroll, focus change, panel close, or outside tap cancels a
revealed delete action.

## Reset All

`Reset all` remains visually separated from normal switching. Tapping it opens
a full flat confirmation state:

- Title: `Reset all Agents?`, centered at `y=142`, Montserrat 22.
- Explanation: centered in `x=82, y=178, w=302`, two lines.
- Hold target: `x=92, y=250, w=282, h=58`, `Hold to reset`.
- Cancel target: `x=153, y=326, w=160, h=48`.

The destructive action requires an uninterrupted 1.2 s hold. Progress fills
the target from left to right; releasing early cancels it. A successful reset
clears all stored Agents and pending requests, rotates credentials, disconnects
the active Agent, and returns to the empty state. Failure shows `Couldn't reset`
and preserves the existing registry.

## New Agent Pairing

A new unpaired Agent request is separate from `Requesting switch`. It replaces
the rail with a dedicated approval state:

- Eyebrow `PAIRING REQUEST`: centered at `y=58`, Montserrat 12, amber.
- Agent name: centered at `y=92`, Montserrat 22.
- Helper text: centered at `y=124`, Montserrat 14.
- Six-digit code: centered at `y=166`, Montserrat SemiBold 36, formatted
  `482 731`.
- `Allow + Switch`: `x=82, y=236, w=302, h=54`, violet fill.
- `Allow`: `x=82, y=302, w=302, h=50`, violet outline.
- `Reject`: `x=133, y=364, w=200, h=46`, vermilion text.

The current 60 s request deadline remains authoritative. The view shows a
seconds-remaining label. At expiry, all three actions become unavailable and a
single `Dismiss` action replaces them. Approval and rejection continue using
the existing pairing session and transport paths.

## Empty And Error States

With no stored Agents and no pairing request:

- Title remains `AGENTS`.
- Center text: `No paired Agents`, `y=194`, Montserrat 20.
- Helper: `Open StopWatch Codex Agent on a Mac to pair`, centered in a 300 px
  width at `y=230`, Montserrat 14.
- `Reset all` is hidden.

Transient errors appear as one centered message above the bottom action area,
use vermilion text, and clear after 2 s. Errors never replace truthful row
state or silently mutate the registry.

## Component Boundaries

- `AgentPanelModel` continues to own registry-derived rows, request and
  confirmation state, and high-level decisions.
- A new pure `AgentFocusRailModel` owns focused index, visible-slot mapping,
  snapping, header-only close gesture ownership, and one revealed delete row.
- A new `agent_panel_ui_layout.h` owns all geometry, palette aliases, and
  compile-time round-screen bounds.
- `AppCodexStatus` owns LVGL objects and translates focus-rail decisions into
  existing `AgentPanelDecision` actions. It does not duplicate registry or BLE
  policy.

## Verification Contract

- Native tests cover 0, 1, 5, and 8 Agent slot mapping, focus clamping, snap
  behavior, header-only close, delete reveal/cancel, and row-to-fingerprint
  selection.
- Firmware contract tests lock the exact layout constants, fonts, state copy,
  pairing actions, and absence of permanent per-row delete buttons.
- Existing Agent registry, pairing, voice cleanup, and secure transport tests
  remain green.
- A fresh ESP-IDF unified build must pass capacity gates.
- Final visual acceptance compares a 466 × 466 running capture with the locked
  Focus Rail reference. If an automated watch capture remains unavailable, the
  user performs the same-size physical-screen review and that limitation is
  reported explicitly.
