# Codex Pinned Microphone Design

## Status

Approved direction: **A · centered beacon**.

## Goal

Add a third page to the watch Codex app that lets the user start one continuous
watch-to-Mac microphone session without holding the upper-right button.

## Navigation

The Codex app page order is:

1. Codex status;
2. Pinned microphone;
3. TRAE status.

A left swipe moves forward and a right swipe moves backward. Navigation does
not wrap. The existing fast-swipe and long-drag thresholds remain unchanged.
The page strip becomes 1398 pixels wide and the global indicator shows three
positions.

## Visual Contract

The page uses the approved 466 × 466 centered-beacon composition and the
existing Codex palette:

- black instrument background;
- centered `CODEX` and `固定麦克风` labels;
- microphone glyph, status, divider, switch, and active page indicator aligned
  on `x=233`;
- off state uses etched gray, `已关闭`, and a left-positioned switch knob;
- active state uses a violet/cyan beacon, mint `收音中`,
  `MAC · CONTINUOUS AUDIO`, and a right-positioned switch knob;
- unavailable/error states keep the same geometry and replace the status copy
  and color rather than rearranging the page.

The switch is a large centered touch target. State remains readable without
depending on animation. A short non-bouncing transition may accompany a state
change, but animation is not required for meaning.

## Voice Semantics

Pinned mode reuses the production encrypted voice session and audio pipeline.
It does not add a second audio protocol, GATT service, or Mac audio route.

Turning the switch on is equivalent to one sustained voice press:

1. require the authenticated production voice link to be ready;
2. start exactly one existing voice session;
3. continue capture and encrypted BLE audio delivery until stopped;
4. display `收音中` only when the voice controller reports recording.

Turning the switch off requests the existing graceful voice end and returns to
the ready/off state after the terminal notification is handled.

The pinned latch is owned by the watch UI, not the Mac Agent. The Mac continues
to process the existing Begin/Audio/End sequence and therefore needs no new
persistent-mode wire state.

## Interaction With the Upper-Right Button

While pinned mode is active, upper-right click actions keep their existing
submit/forced-steer semantics. A right-button hold must not start a second
voice session; the pinned session remains active until the page switch or a
safety boundary stops it.

## Safety and Lifecycle

Pinned capture stops and the latch clears on:

- leaving the Codex app or pressing Home;
- BLE disconnect, connection epoch change, credential change, or voice link
  becoming unusable;
- controller error;
- app shutdown.

Swiping among the three Codex pages does not stop pinned capture. Turning the
screen off does not stop it; the page explicitly represents a latched
continuous microphone, and returning to the page shows the current state.

The mode is intentionally not persisted across app close, reboot, Agent
switch, or disconnect. The user must explicitly enable it again.

## Failure Presentation

- If voice is not ready, tapping the switch does not latch it on; the page
  shows the controller's unavailable/error state.
- If startup fails, the switch remains off.
- If an active session fails or disconnects, capture is cancelled, the latch
  clears, and the page becomes unavailable/error.
- Repeated taps or repeated lifecycle notifications are idempotent and never
  create two sessions or two terminal notifications.

## Verification

Focused automated coverage must prove:

- three-page forward/backward swipe boundaries and no wrap;
- pinned start emits one Begin and keeps producing audio without button hold;
- pinned stop emits one graceful End;
- duplicate start/stop is idempotent;
- button hold cannot start a second session while pinned;
- page navigation preserves the session;
- Home/app close, disconnect, epoch change, unavailable link, suppression, and
  error clear the latch and stop/cancel exactly once;
- the page strip, third page, three indicators, approved labels, geometry,
  colors, and switch states are wired into the firmware.

Acceptance requires a real watch-to-Mac trial: enable the switch, speak without
holding the button, verify continuous Mac input, navigate away and back, then
disable the switch and verify capture and audio-route cleanup stop.
