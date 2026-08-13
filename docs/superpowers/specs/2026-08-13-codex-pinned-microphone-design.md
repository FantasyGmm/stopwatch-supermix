# Codex Pinned Microphone Design

## Status

Approved direction: **A · centered beacon**.

## Goal

Add a third page to the watch Codex app that lets the user start one continuous
watch-to-Mac microphone session without holding the upper-right button.

## Navigation

The Codex app page order from left to right is:

1. Pinned microphone;
2. Codex status;
3. TRAE status.

A right swipe from Codex reveals pinned microphone; a left swipe from Codex
reveals TRAE. Navigation does not wrap. The existing fast-swipe and long-drag
thresholds remain unchanged.
The page strip becomes 1398 pixels wide and the global indicator shows three
positions.

## Visual Contract

The page uses the approved 466 × 466 centered-beacon composition and the
existing Codex palette:

- black instrument background;
- centered `CODEX` and `固定麦克风` labels;
- `CODEX` at 14 px, `固定麦克风` at 24 px, and state at 22 px;
- a real 62 × 82 px microphone drawn from LVGL geometry rather than a font
  symbol; the microphone, status, divider, switch, and active page indicator
  are aligned on `x=233`;
- off state uses etched gray, `已关闭`, and a left-positioned switch knob;
- active state uses a violet/cyan beacon, mint `收音中`,
  `MAC · CONTINUOUS AUDIO`, and a right-positioned switch knob;
- unavailable/error states keep the same geometry and replace the status copy
  and color rather than rearranging the page.

The switch is a large centered touch target. State remains readable without
depending on animation. A short non-bouncing transition may accompany a state
change, but animation is not required for meaning.

## Voice Semantics

Pinned mode reuses the production encrypted voice transport, capture pipeline,
and existing Mac virtual-microphone route. It does not add a second GATT
service. A protocol-authenticated session-mode field distinguishes pinned
external microphone from push-to-talk.

Turning the switch on is equivalent to one sustained voice press:

1. require the authenticated production voice link to be ready;
2. start exactly one existing voice session;
3. continue capture and encrypted BLE audio delivery until stopped;
4. display `收音中` only when the voice controller reports recording;
5. on the Mac, start and feed the virtual microphone route without invoking
   the Typeless shortcut.

Turning the switch off requests the existing graceful voice end and returns to
the ready/off state after the terminal notification is handled.

The pinned latch is owned by the watch UI. The Mac reads the authenticated
session mode from Begin: push-to-talk arms the route and Typeless shortcut;
external microphone arms only the route. End and error clean up only resources
armed by that session.

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

- fixed microphone ← Codex → TRAE swipe boundaries and no wrap;
- pinned start emits one Begin and keeps producing audio without button hold;
- pinned stop emits one graceful End;
- duplicate start/stop is idempotent;
- button hold cannot start a second session while pinned;
- page navigation preserves the session;
- Home/app close, disconnect, epoch change, unavailable link, suppression, and
  error clear the latch and stop/cancel exactly once;
- the page strip, three indicators, exact type sizes, geometric microphone,
  approved labels, geometry, colors, and switch states are wired into firmware;
- PTT Begin still arms Typeless, while external-microphone Begin never calls
  the shortcut and remains available when Typeless is not running.

Acceptance requires a real watch-to-Mac trial: enable the switch, speak without
holding the button, verify continuous Mac input, navigate away and back, then
disable the switch and verify capture and audio-route cleanup stop.
