# Agent Panel Gesture Ownership Design

## Problem

The Agent panel opens with a downward swipe but cannot close with an upward swipe. Global brightness and volume controls activate after 24 pixels of vertical movement, before the Agent panel's 48-pixel close threshold. The page then cancels the panel touch because the global control owns it.

## Design

The visible Agent panel exclusively owns vertical gestures. `render_agent_panel()` uses the existing `GlobalEdgeControlService::setEnabled()` interface to disable global brightness and volume gestures while the panel is visible and restore them when it is hidden. `onClose()` also restores global controls so an app lifecycle exit cannot leave them disabled.

No thresholds, gesture directions, or global edge-control behavior change outside the visible Agent panel. Pairing requests that make the panel visible receive the same ownership automatically through rendering.

## Verification

- A source contract proves that rendering synchronizes global edge-control enablement with panel visibility.
- A lifecycle contract proves that closing the Codex status app restores global controls.
- The native Agent panel model test continues to prove upward close and downward open gestures.
- Existing global edge-control and Agent panel tests remain green.

## Self-review

The design is bounded to one interaction conflict, reuses an existing lifecycle-safe API, and contains no new gesture state or thresholds. There are no placeholders or ambiguous ownership cases.
