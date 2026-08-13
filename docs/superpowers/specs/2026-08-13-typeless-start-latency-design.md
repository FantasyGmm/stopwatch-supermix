# Typeless Start Latency Design

## Goal

Reduce perceived Typeless activation latency by changing the right-button hold
threshold from 500 ms to 300 ms and removing repeated Core Audio construction
from the post-threshold path.

## Interaction

- A right-button release before 300 ms remains a short click.
- Holding through 300 ms starts voice exactly once.
- The existing 90 ms A+B Home arbitration remains independent and unchanged.
- A second click that becomes a 300 ms hold still cancels the pending submit
  before voice starts.

## Audio lifecycle

When authenticated voice becomes Ready, `VoiceAudioRoute.prepare()` finds
BlackHole and opens the Core Audio output queue without starting it or changing
the system default input. A voice session then calls `start()`, which starts the
prepared queue and temporarily leases BlackHole as the default input before
the Typeless shortcut is emitted.

Normal session cleanup stops and clears the queue and restores the previous
default input, but leaves the queue prepared for reuse. Disconnect, credential
replacement, and Agent shutdown call `close()` to restore any lease, stop the
queue, and dispose it. Failed prepare or start attempts aggregate cleanup
failures and leave the route safe to close again.

The 50 ms key-down duration remains synchronous. Typeless receives the key-down
event immediately; making key-up asynchronous would mostly advance menu state
while adding cross-thread key ownership and shutdown recovery risk.

## Safety

Preparation does not acquire the watch microphone, start Core Audio playback,
change the system default input, or trigger Typeless. The default input remains
leased only during an authenticated active voice session. Existing replay,
session-ordering, cleanup, and BLE authorization checks remain unchanged.

## Verification

- Firmware host tests cover 299 ms short-click and 300 ms voice boundaries.
- Audio route tests prove prepare is inert, two sessions reuse one open queue,
  normal stop restores input without closing, and final close disposes exactly
  once.
- Voice service tests prove prepare/close lifecycle and failure cleanup.
- Mac Agent tests, firmware host tests, and a production firmware build cover
  integration.
- A local Core Audio benchmark compares cold route start with prepared restart.

## Self-review

The design changes only the approved threshold and the already-isolated Mac
audio route lifecycle. It does not pre-capture audio, hold BlackHole as the
default input while idle, alter Fn semantics, or weaken authenticated session
boundaries. There are no unresolved choices or placeholders.
