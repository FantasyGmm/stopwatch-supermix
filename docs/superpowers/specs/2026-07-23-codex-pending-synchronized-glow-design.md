# Codex Pending Synchronized Glow Design

## Goal

Replace the current visibly staged and slow Pending edge animation with a
smooth synchronized breath:

- all three red rings brighten and dim together;
- one complete dark-to-bright-to-dark cycle lasts 1.2 seconds;
- the animation remains exclusive to the visible Pending state;
- the display work is limited to the edge occupied by the rings instead of
  repeatedly invalidating three full-screen objects.

## Current Problem

`CodexPendingSoftGlow` currently owns three 444–464 px LVGL objects. Every
animation step changes `border_opa` on each object separately. LVGL invalidates
the full bounds of an object when a local style property changes, so one phase
causes three nearly full-screen invalidations.

The StopWatch display uses partial rendering with a 120-line buffer. Repainting
the full Codex page for every opacity step makes the three rings appear to
refresh progressively even though they receive the same numeric animation
phase. The existing 900 ms forward plus 900 ms playback duration also makes one
complete breath last 1.8 seconds.

## Chosen Design

### One transparent drawing overlay

`CodexPendingSoftGlow` will own one 466 × 466 transparent, non-interactive LVGL
object instead of three ring objects. Its draw event will render the same three
fixed concentric circles in one callback.

The overlay remains below the existing Codex content, preserving the current
layout and interaction behavior. No canvas or full-screen bitmap is allocated.

### One shared phase

One LVGL animation drives a single phase from 0 to 255 and back. Every ring uses
the same calculated opacity for a given phase, ranging from `LV_OPA_40` to
`LV_OPA_COVER`, so there is no phase offset, visibility threshold difference,
or sequential ring update. This preserves the existing outer ring's intensity
while bringing the other two rings into the same visible breath. The Pending
heading keeps its existing synchronized opacity pulse.

The forward duration is 600 ms and the playback duration is 600 ms, producing a
1.2-second complete cycle. The ease-in-out path remains in use so the reversal
does not look abrupt.

### Edge-only invalidation

Each phase change updates the stored phase and invalidates four narrow areas:
top, bottom, left, and right. The edge band must cover the full geometry of all
three rings, including their border widths, while avoiding the center of the
screen.

The drawing callback may be invoked once for each clipped edge area, but it
submits all three rings together using the same phase. It must not mutate three
separate LVGL object styles.

The edge-band size is a named, testable constant derived from the innermost
ring's inner boundary. A 20 px band covers the current ring geometry with a
small safety margin.

## State and Lifecycle

The existing state behavior remains unchanged:

- Pending and screen on: show and animate the overlay.
- Running, Completed, or no alerts: stop the animation and hide the overlay.
- Screen off: stop and hide it.
- Wake while still Pending: show and restart it.
- App close: delete the animation and overlay before destroying the parent.

Repeated synchronization with the same state remains idempotent.

## Performance Boundaries

- No full-screen ARGB canvas or pre-rendered glow image.
- No additional task or timer outside LVGL's animation system.
- No layout, BLE protocol, alert classification, carousel, or display-driver
  change.
- No global LVGL refresh-rate change; other applications must be unaffected.
- Animation updates invalidate only the four edge bands plus the small Pending
  heading area.

## Verification

### Automated

1. A host-side logic test verifies:
   - 600 ms forward and 600 ms playback durations;
   - all three rings receive the same opacity for each phase;
   - phase endpoints map to the intended minimum and maximum opacity;
   - the 20 px edge band covers every ring border.
2. The firmware contract test verifies:
   - one overlay replaces the array of three ring objects;
   - all rings are drawn from one draw callback;
   - animation steps do not call `lv_obj_set_style_border_opa` three times;
   - invalidation uses four bounded edge areas, not full-object invalidation;
   - Pending/screen-off lifecycle wiring remains intact.
3. Run focused tests, the full Mac Agent suite, host-side C++ tests, and the
   unified firmware build.

### Hardware

1. Flash the verified unified firmware to the connected StopWatch.
2. Open Codex and deliver a controlled Pending snapshot.
3. Observe at least five complete cycles.
4. Accept only if all three rings brighten and dim together, the motion is
   visually continuous, the page center no longer appears to repaint in
   sections, and leaving Pending stops the effect immediately.

Automated build success is not evidence of visual smoothness; the hardware
observation remains a required acceptance gate.
