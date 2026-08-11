# Agent Panel Bottom Close Gesture Design

## Problem

The Agent focus rail correctly owns vertical scrolling, but commit `ebe217f`
over-corrected gesture isolation by allowing panel-close drags only from the
top header. The visible bottom close affordance therefore does nothing, making
the panel appear impossible to retract through the expected bottom-up gesture.

## Interaction

Gesture ownership is decided from the initial press position:

- a vertical gesture beginning inside the rail viewport remains an internal
  rail scroll and never moves the panel;
- an upward gesture beginning in the bottom action area follows the finger and
  closes the panel after the existing 48-pixel threshold;
- the existing top-header close gesture remains supported for compatibility;
- a short bottom drag follows the finger, then settles back open on release;
- horizontal gestures remain available to the row delete interaction.

The panel keeps the existing six-pixel dead zone, clamped Y range, 180 ms
ease-out settle animation, and interruption behavior.

## Architecture

`AgentPanelModel` remains the gesture-arbitration boundary. It derives the
panel-draggable regions from `agent_panel_ui` layout constants: the header
above `kPanelHeaderBottomY` and the footer at or below the rail viewport's
bottom edge. `AppCodexStatus` requires no new motion mechanism because its
existing `vertical_drag_offset()` path already provides follow motion and
settling.

The LVGL rail remains a clipped, vertically scrollable object with its current
snap behavior. No scroll position is copied into the panel model and no
gesture handoff is attempted after a drag begins.

## Verification

- Native model tests prove that rail-origin drags do not move or close the
  panel, while footer-origin drags follow and close it.
- A short footer drag proves the existing settle-back decision remains intact.
- Firmware contracts continue to prove the rail is vertically clipped and the
  panel uses the follow/settle animation path.
- Focused Agent tests and the unified host suite guard adjacent behavior.

## Self-review

The design restores the missing bottom close path without restoring the old
conflict where every rail scroll could close the sheet. It changes only the
gesture-start region; thresholds, animation, rendering, row actions, and
pairing behavior remain unchanged. There are no placeholders or unresolved
choices.
