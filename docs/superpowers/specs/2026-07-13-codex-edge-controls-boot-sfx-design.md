# Codex Edge Controls and Pending Sound Design

## Goal

Give the Codex monitor the same left/right edge controls as Launcher and replace
the Pending alert tone with the complete startup sound. Brightness and volume
changes must remain global and persistent across applications and reboots.

## Shared Edge Control Component

Extract Launcher's edge-control gesture, state, drawing, and HAL application
logic into a shared `EdgeControlOverlay` under `main/apps/common/`. Launcher and
Codex instantiate the same component rather than maintaining copied behavior.

The component owns:

- touch candidate, active side, start point, and current value;
- activation, live-apply, release, and delayed-hide timing;
- the transparent full-screen LVGL overlay and arc drawing callback;
- normalization and persistence through the existing HAL APIs;
- an optional interaction callback for host views that must disable competing
  scrolling while a control is active.

The component does not own application navigation, the Launcher carousel, the
Codex status view, or button handling.

## Gesture Contract

- A touch starting below the top 30 pixels becomes a control candidate.
- The left half selects brightness; the right half selects volume.
- Vertical movement greater than 24 pixels and greater than horizontal movement
  activates the candidate.
- Horizontal movement greater than 24 pixels and greater than vertical movement
  cancels the candidate.
- Upward movement increases the value and downward movement decreases it.
- The 210-pixel drag range maps brightness across 10-100 and volume across
  0-100.
- Active values are applied at most once every 40 milliseconds without an NVS
  write. Releasing applies once with persistence enabled.
- The visual overlay remains for 700 milliseconds after release.

The visuals remain identical to Launcher: blue left brightness arc with sun
icon and percentage, cyan right volume arc with speaker icon and percentage.

## Launcher Integration

`LauncherView` replaces its private control fields and methods with one shared
component. It provides an interaction callback that removes the panel's
scrollable flag and hides left/right page indicators while a control is active,
then restores them on release.

Launcher retains its own horizontal swipe detection and app carousel. A
horizontal gesture that cancels the edge-control candidate remains available to
the Launcher swipe handler. Existing startup, hold-scroll, icon visibility, and
click-suppression behavior remain unchanged.

## Codex Integration

`AppCodexStatus` creates the shared overlay as part of its view lifecycle and
updates it while the page is running under the existing LVGL lock. It destroys
the overlay before the Codex root panel is deleted.

The overlay is floating and transparent when inactive, so it does not alter the
Codex dashboard layout. Touch control does not interfere with the two-button
Home chord, left-button screen-off gesture, BLE updates, or alert carousel.

Both applications call the same existing global APIs:

- `setBackLightBrightness(value, true)` on release;
- `setSpeakerVolume(value, true)` on release.

The HAL immediately applies values and persists them in the system settings
namespace. Launcher, Clock, Codex, Assistant, and later applications therefore
observe the same brightness and volume.

## Pending Alert Sound

The existing Mac Agent continues sending `{"type":"chime","agent":"codex"}`
only for newly unnotified Pending work. No BLE protocol change is needed.

The firmware replaces the generated 660 Hz tone in the Chime branch with
`GetHAL().playBootSfx()`. This reuses the embedded `boot_sfx.bin` and its current
asynchronous playback path. One reconnect backlog still produces one sound;
Running and Completed remain silent.

The startup sound uses the current global speaker volume. A zero volume setting
therefore leaves the alert silent without changing alert acknowledgement.

## Failure and Lifecycle Handling

- A missing touch device makes the overlay update a no-op.
- Closing either application during an active gesture persists the latest
  applied value and restores any host interaction state before destroying LVGL
  objects.
- Reopening an application reads current values from HAL instead of retaining a
  stale component-local value.
- Audio playback remains asynchronous; a new asynchronous audio request may
  interrupt an in-progress sound according to the existing codec behavior.

## Verification

- Pure host tests cover side selection, vertical activation, horizontal
  cancellation, upward/downward mapping, bounds, release persistence, and hide
  timing.
- Source-contract tests require Launcher and Codex to instantiate the shared
  component and reject the old private Launcher implementation.
- A firmware contract test requires the Chime branch to call `playBootSfx()` and
  no longer contain the 660 Hz Pending tone.
- The full Python suite, host C++ tests, and ESP-IDF build must pass.
- Device validation covers left brightness and right volume control in both
  Launcher and Codex, persistence after leaving the page and rebooting, Pending
  full startup sound, and silence for Running/Completed.
- Flash only `ota_0` and verify the written image by readback SHA-256 without
  erasing NVS, resources, or the Assistant partition.
