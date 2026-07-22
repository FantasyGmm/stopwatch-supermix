# Codex Pending Soft Glow Design

## Goal

Add the approved **A1 soft breathing glow** to the latest Codex status page when the displayed task state is `Pending`, without reverting or repositioning any UI delivered by commit `d8fdb8c`.

## Current UI Baseline

The effect is additive to the current 466 x 466 page. These existing contracts remain unchanged:

- pure-black `#000000` page background;
- hidden host-name row;
- Codex logo and connection state at their current positions;
- enlarged pet at `x=73, y=88` with scale `293`;
- beacon spine at `x=194, y=96`;
- telemetry, quota, alert, and battery geometry in `codex_status_ui_layout.h`;
- pet animation, alert carousel, BLE/Wi-Fi transport, edge controls, and screen-off behavior.

The previous no-glow visual rule remains in force for normal, running, completed, disconnected, and error states. This specification introduces one narrow exception for the `Pending` notification effect.

## Visual Effect

The physical panel edge receives a restrained red breathing halo:

- one crisp 2 px vermilion core ring nearest the visible edge;
- two progressively softer inner rings that simulate an 8-12 px glow without realtime blur;
- a 1.8 second ease-in/ease-out cycle;
- the core ring varies from approximately 45% to 100% opacity;
- inner rings use lower opacity and breathe in phase with the core;
- ring geometry remains fixed while only opacity changes, avoiding visible layout movement;
- the existing `PENDING n/n` title gently brightens in the same phase;
- no gradients, bitmap assets, or shader effects are added.

All glow objects sit above the black page background but below the existing content and `EdgeControlOverlay`. They cannot cover the pet, labels, progress bars, battery text, or physical-button affordances.

## State And Lifecycle

- Activate only when `CodexAlertCarousel::display_state()` is `CodexTaskState::Pending` and the screen is on.
- Stop and hide immediately when the display state becomes Running, Completed, or empty.
- Keep Pending priority unchanged; the effect follows the already-selected carousel display state rather than independently inspecting alert order.
- Pause and hide the effect when the Codex page enters screen-off mode so it creates no hidden redraw load.
- On wake, resume only if the current display state is still Pending.
- Stop the animation and release all LVGL objects before the Codex view is destroyed.
- Do not add repeated sound or vibration. Existing one-shot Pending notification behavior remains unchanged.

## Component Boundary

Implement the effect as a small Codex-specific LVGL component with this ownership contract:

- `init(parent)`: create the fixed concentric rings below content;
- `set_heading(label)`: retain a non-owning reference to the existing alert heading after that label is created;
- `set_active(active)`: start, stop, show, or hide the single opacity animation idempotently;
- `shutdown()`: remove animation callbacks and clear object references before parent deletion.

`AppCodexStatus` creates the ring component immediately after the black panel and before all existing content, then supplies the already-owned alert heading after label construction. It forwards view lifecycle, Pending display state, and screen-off state. The component does not parse alerts, own the heading label, own timers outside LVGL, or alter layout constants. Deactivation restores the heading to full opacity before hiding the rings.

## Performance Constraints

- Animate opacity only; do not resize, rotate, blur, or repaint bitmap assets.
- Use one LVGL animation callback to update all ring opacities and the Pending heading phase.
- Keep the ring count to three.
- Do not run the animation while hidden, screen-off, or outside the Codex app.

## Verification

- Add host-side tests for idempotent active/inactive transitions and Pending-only gating.
- Add a source/layout contract proving that the existing `d8fdb8c` coordinates and hidden-host behavior remain unchanged.
- Verify Running, Completed, empty, and screen-off states hide and stop the effect.
- Run the focused Codex model, carousel, UI layout, and firmware source-contract tests.
- Run the full unified firmware build and capacity gate.
- Flash the verified build and confirm on hardware that the halo is visible, smooth, clipped to the circular panel, and does not obscure edge controls or current UI content.
