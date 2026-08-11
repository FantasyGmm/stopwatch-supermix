# Agent Panel Follow Motion Design

## Problem

The Agent panel currently jumps directly between `-430` and `0` after release. The same open/close path calls `render_agent_panel()`, which cleans and rebuilds the entire child tree. The abrupt position change plus unnecessary reconstruction produces a flashing transition with no direct manipulation feedback.

## Interaction

The panel follows a dominant vertical drag after a six-pixel dead zone. Movement is clamped between the hidden and visible positions. Releasing after the existing 48-pixel open or close threshold keeps the current model behavior and settles to the new endpoint; releasing below the threshold settles back to the prior endpoint.

Settling uses a 180 ms LVGL ease-out position animation. A new press cancels the current settle animation, captures the panel's current Y position, and continues from that exact visual position. There is no opacity, bounce, blur, or content choreography.

## Architecture

`AgentPanelModel` remains responsible for gesture intent and exposes a UI-independent vertical drag offset. `AppCodexStatus` owns pixel positions and LVGL animation:

- press: cancel the active panel animation and capture the current Y position;
- move: combine the captured Y position with the model offset and clamp it;
- release: let the model decide visibility, then animate from the current Y to the matching endpoint;
- lifecycle exit: delete the panel animation before deleting LVGL objects.

Opening and closing no longer call `render_agent_panel()`. That method remains responsible only for content changes such as registry, pairing, and confirmation updates. Automatic pairing visibility uses the same settle helper after its content is rebuilt.

Global edge controls remain disabled whenever the model says the panel is visible and are restored when it is hidden or the app closes.

## Failure Handling

Missing LVGL objects make position and animation helpers no-ops. Screen-off or suppressed input cancels gesture tracking and settles the panel to the model's current endpoint. Horizontal gestures and sub-dead-zone movement do not move the panel. Starting a new gesture safely replaces an in-flight animation.

## Verification

- Native model tests cover the dead zone, vertical dominance, upward and downward drag offsets, and unchanged 48-pixel decisions.
- Firmware source contracts cover animation duration, ease-out path, animation cancellation, lifecycle cleanup, and removal of content rendering from the open/close release path.
- Existing Agent panel, global edge-control, and unified host validation remain green.
- A full ESP-IDF build and on-device boot smoke verify the committed firmware.

## Self-review

The design changes only Agent panel motion, keeps the existing thresholds and gesture ownership, and does not introduce decorative effects or a new rendering subsystem. Content rebuilding and position animation have distinct responsibilities. There are no placeholders or unresolved behavior choices.
