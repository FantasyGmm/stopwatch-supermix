# Global Edge Controls Design

## Goal

Make the existing Launcher brightness and volume gestures available throughout
the visible watch UI. A vertical drag beginning in the left half adjusts global
brightness; the same gesture in the right half adjusts global speaker volume.
The control must work in Launcher, Assistant, Codex, Clock, Badge, IMU, FFT,
Settings, and future Mooncake applications without copying the control into each
application.

The existing visual treatment and gesture thresholds remain unchanged. This
change also centers the circular progress marker on the thickness of both arc
tracks.

## Decision

Use one global edge-control service attached to LVGL's display top layer. The
service owns the existing shared gesture state machine and overlay, updates
before Mooncake application callbacks, applies values through HAL, and
arbitrates touch ownership once an edge gesture activates.

This is preferable to per-application overlays because new applications inherit
the behavior automatically and there is only one touch candidate, overlay, and
persistence path. Implementing the gesture in the touch driver was rejected
because it would couple UI policy and LVGL rendering to the hardware layer.

## Component Boundaries

### EdgeControlLogic

`EdgeControlLogic` remains a pure, host-testable state machine. It owns:

- the left-brightness and right-volume candidate;
- vertical activation and horizontal cancellation;
- value normalization and live-apply timing;
- release persistence and delayed hide timing.

It does not access LVGL, HAL, Mooncake, Brookesia, or application state.

### EdgeControlOverlay

`EdgeControlOverlay` remains responsible for drawing the arcs, icons, value
labels, and progress markers. It is created once on `lv_layer_top()` instead of
being created below Launcher or Codex page roots. The overlay stays floating,
transparent, and non-clickable when inactive, so ordinary touches continue to
reach the current application.

The overlay is initialized only while holding the LVGL lock and is destroyed
before the display runtime is torn down.

### GlobalEdgeControlService

A single `GlobalEdgeControlService` owns the overlay and translates the current
LVGL touch state into `EdgeControlLogic` calls. It is initialized after HAL has
created the display and touch input device. Its update runs before
`GetMooncake().update()`.

The service exposes read-only touch ownership and per-setting revision values.
These are used only by code that polls the touch device or maintains a separate
brightness or volume editor. Normal LVGL widgets require no application-specific
integration.

Launcher and Codex no longer own their own `EdgeControlOverlay` instances.

## Gesture and Touch Arbitration

The existing gesture contract is preserved:

- touches beginning in the top 30 pixels are excluded;
- the left half selects brightness and the right half selects volume;
- vertical movement beyond 24 pixels and greater than horizontal movement
  activates the candidate;
- horizontal movement beyond 24 pixels and greater than vertical movement
  cancels the candidate and remains available to the application;
- upward movement increases the value and downward movement decreases it;
- brightness is clamped to 10-100;
- volume is clamped to 0-100 and rounded to five-point steps;
- live values are applied at most once every 40 milliseconds;
- release persists the final value and leaves the overlay visible for 700
  milliseconds.

Before activation, the current application continues receiving the touch. At
activation, the global service becomes the exclusive owner until physical
release. It calls `lv_indev_wait_release()` followed by `lv_indev_reset()` under
the LVGL lock. This matches LVGL's own safe cancellation sequence: the current
widget loses the press, cannot emit a later click, and no other widget can claim
the still-held finger.

The few components that directly poll `GetHAL().lvTouchpad` must also honor the
global ownership state:

- Launcher clears its horizontal swipe candidate and suppresses application
  clicks while ownership is active;
- Watch Face clears its horizontal face-switch candidate;
- any future raw-touch consumer must use the same ownership API;
- the existing status-bar gesture remains independent because it starts inside
  the top exclusion zone, where edge controls cannot become candidates.

When a horizontal movement cancels an edge candidate, ownership is never taken
and the application's existing horizontal action proceeds normally.

## Global Brightness and Volume State

Live drag updates change hardware immediately without writing NVS. Release
applies the final value with persistence enabled. The existing HAL settings
namespace remains the single durable source, so values survive application
switches and reboot.

Each successful global change advances a brightness or volume revision. The
Settings brightness and volume workers observe these revisions and update their
local slider and label before handling local edits. An open Settings page can
therefore neither display a stale value nor overwrite a global gesture with its
old value when OK is pressed.

When Assistant is running, live volume changes still apply immediately through
the shared hardware backend. On release, the final volume and its corresponding
mute state are queued to Assistant runtime and applied to Brookesia outside the
UI update path. This keeps Brookesia's desired state consistent without making
the gesture wait on a synchronous service RPC. Volume zero maps to mute enabled;
any nonzero volume maps to mute disabled.

## Overlay Geometry Fix

LVGL defines `lv_draw_arc_dsc_t::radius` as the arc's outer radius. The current
progress marker uses that outer radius as its center path, so the marker is
shifted outward by half the eight-pixel track width.

Both brightness and volume markers will use:

`marker_radius = arc_outer_radius - arc_width / 2`

With the current dimensions, the center moves inward by four pixels and lies on
the centerline of the track. Arc angles, track bounds, marker size, colors, and
labels remain unchanged.

## Lifecycle and Failure Handling

- If the display, top layer, or touch input is unavailable, initialization or
  update is a safe no-op and applications continue normally.
- The service does not activate while effective display brightness is zero.
  Codex screen-off behavior therefore remains unchanged and a black screen does
  not react to touch adjustment.
- If the display turns off during an active gesture, the latest value is applied
  and persisted once, ownership is released, and the overlay is hidden.
- Application open, close, and screen replacement do not destroy the global
  overlay or interrupt an active adjustment.
- LVGL object access, input cancellation, and drawing invalidation occur only
  under the existing LVGL lock.
- A missing or stopped Assistant runtime does not block volume adjustment; only
  the optional Brookesia state synchronization is skipped or deferred.

## Scope

This change does not alter button navigation, the global Home chord, the
status-bar pull-down gesture, Launcher carousel thresholds, Watch Face switching
thresholds, Codex screen-off behavior, Assistant lifecycle, or the visible arc
style beyond centering the progress marker.

No application-level enable switch is added. The control is available on every
visible application screen by default.

## Verification

Automated verification will cover:

- the existing pure gesture contract and bounds;
- ownership beginning only after vertical activation and ending on release;
- horizontal cancellation without ownership;
- LVGL wait-and-reset cancellation contract;
- global initialization and update ordering before Mooncake applications;
- removal of Launcher and Codex-owned overlays;
- suppression/reset behavior in Launcher and Watch Face raw-touch consumers;
- independent brightness and volume revisions;
- Settings slider synchronization after an external global change;
- asynchronous Assistant volume and mute synchronization;
- the marker center radius for both brightness and volume arcs;
- all existing Python and host tests, clean ESP-IDF build, capacity gate, and
  whitespace check.

Device validation will open every retained application and verify left-side
brightness adjustment, right-side volume adjustment, absence of accidental
clicks or navigation, centered markers, persistence across application changes
and reboot, live Assistant speech volume changes, Settings slider consistency,
and unchanged black-screen behavior. Hardware-dependent validation remains a
separate gate from offline completion.
