# UI-Independent Watch Voice Transport

## Decision

Move production voice delivery out of the Codex page update loop. The watch
will use one real-time capture task and one independent BLE transmit task. The
Codex UI remains responsible for button dispatch and presentation, but it is
not allowed to advance, drain, retry, or finish a voice session.

This replaces the previous mitigation that increased the outgoing audio
buffer from 300 ms to 1000 ms. A larger buffer can absorb one measured stall,
but any longer UI stall still ends the session with queue-overflow error 5.
The new design removes that dependency instead of extending its timeout.

## Evidence and Root Cause

Physical failures consistently reached the watch-generated
`VoiceFailure::QueueOverflow` path with detail zero. Detail zero is emitted
when the capture producer fills the local outgoing queue; BLE disconnect,
notification congestion, Typeless cleanup, and button release use different
paths or details.

The capture task already runs independently, but the only consumer of its
encrypted queue is this code in `AppCodexStatus::onUpdate()`:

1. update the UI and task carousels;
2. process pairing and BLE status work;
3. call `VoiceInputController::poll()`;
4. drain notifications with `take_notification()` and `notify_voice()`;
5. report each result with `record_notification_result()`.

Consequently, any sufficiently long delay before step four lets real-time
capture outrun the UI consumer. Increasing queue duration changes only how
long the UI may stall before failure.

## Goals

- UI rendering, task alerts, Agent-panel work, and other page updates cannot
  delay production voice delivery.
- Preserve encrypted protocol ordering: `PTT_BEGIN`, contiguous `AUDIO`
  sequences, then exactly one `PTT_END` or `VOICE_ERROR`.
- Keep capture and BLE congestion isolated from each other with bounded
  memory and bounded shutdown.
- Preserve current button, Home, disconnect, Agent-switch, key-epoch, and
  default-input cleanup behavior.
- Make a stalled-UI regression deterministic in host tests.
- Record enough non-secret diagnostics to distinguish capture, local queue,
  BLE congestion, disconnect, and terminal-delivery failures.

## Non-Goals

- Hiding a genuine BLE disconnect or an indefinitely unavailable receiver.
- Buffering unbounded audio.
- Changing the encrypted voice wire format or the shared multi-Agent key
  design.
- Moving UI rendering or secure-pairing state machines to new tasks.
- Recording audio, encryption material, verification codes, or packet bodies
  in diagnostics.

## Architecture

### Capture task

The existing capture task continues to own the recorder, mono I2S reads,
resampling, ADPCM encoding, session sequence allocation, and notification
encryption. It produces one encrypted audio notification per 20 ms block and
pushes it into the bounded outgoing queue.

The task must never call LVGL or wait for the Codex page update loop. After it
stops because of button release, Home, disconnect, or capture failure, it
wakes the transmit task so that terminal handling can complete independently.

### Transmit task

`VoiceInputController` gains a dedicated transmit worker and a narrow
`VoiceNotificationTransport` interface. `CodexBleTransport` implements that
interface using the existing production voice characteristic.

The worker is the only consumer of the outgoing queue. It:

1. waits for a notification or session-state signal;
2. copies the current queue head under the controller mutex;
3. releases the mutex before entering NimBLE;
4. sends the notification and records the result;
5. removes accepted packets in order;
6. uses bounded, time-spaced retry for transient NimBLE congestion;
7. creates and sends the terminal notification after capture stops and queued
   audio has drained.

No UI callback participates in these steps. The capture and transmit workers
have explicit task priorities above ordinary page work while remaining below
system-critical radio work.

### UI task

The Codex page keeps these responsibilities:

- translate physical button edges into press, release, cancel, and Home;
- pass connection epoch, negotiated MTU, subscription, and READY control
  changes to the controller;
- render the controller's atomic/mutex-protected UI state;
- initiate bounded shutdown during page close or Agent switching.

It no longer calls `take_notification()`, `notify_voice()`,
`record_notification_result()`, or terminal finalization.

### Transport concurrency

Production voice has one transmit owner. Link readiness is published as a
small synchronized snapshot containing the active authorized handle, MTU,
subscription state, and connection epoch. A send uses one snapshot and treats
a concurrent disconnect as a normal `NotReady` result. Link revocation clears
the snapshot before Agent promotion or disconnect cleanup, preventing packets
from being sent to a newly selected Agent under the previous session.

No controller mutex is held while calling NimBLE. This avoids a radio call
blocking button release, disconnect, or capture shutdown.

## Queue and Congestion Policy

The queue remains bounded at 1000 ms while the new path is validated. It is a
safety boundary for a genuine BLE outage, not an allowance for UI latency.

- `Accepted`: remove the exact queue head and reset congestion state.
- `Congested`: retain the head and retry with a short task delay until the
  bounded congestion deadline expires.
- `NotReady` or invalid link: stop capture, emit the appropriate local error
  state when possible, and perform bounded cleanup.
- Queue full with an independently scheduled transmitter: classify as a real
  transport starvation/outage and include a nonzero diagnostic detail that
  identifies the failing boundary.

Retries are based on elapsed time rather than three immediate calls. This
prevents a temporary NimBLE memory shortage from being converted into a
permanent failure within one scheduler tick.

## Lifecycle and Error Handling

- Press: initialize session, enqueue `PTT_BEGIN`, start capture, wake TX.
- Release: request capture stop; capture closes; TX drains audio and sends
  `PTT_END`; only accepted terminal delivery returns the UI to Ready.
- Home or page close: request stop, wake both workers, perform bounded joins,
  clear queued data, and restore the previous Mac input through the existing
  protocol cleanup.
- BLE disconnect or Agent switch: revoke the voice link first, stop both
  workers, clear session identity and queue, then permit the connection epoch
  to advance.
- Shutdown timeout: force-delete only the affected worker after its existing
  cooperative stop and abort-delay grace periods; never wait indefinitely on
  the UI task.

## Diagnostics

Expose session-level counters without packet contents:

- maximum queued audio milliseconds;
- longest interval between accepted notifications;
- total transient congestion retries;
- final failure boundary and code;
- whether terminal delivery completed.

USB logs may report these values at session end or failure. Agent logs retain
their current high-level `recording`, `ready`, and redacted error messages.

## Testing

### Deterministic host tests

- Simulate capture for at least 60 seconds while never calling the UI poll
  method; an accepting fake transport must receive all packets in order and
  the controller must not overflow.
- Stall only the fake UI for five seconds while capture and TX continue;
  queue depth must remain bounded near steady state.
- Inject transient transport congestion and verify delayed retry, ordering,
  recovery, and one terminal notification.
- Inject sustained congestion, disconnect, Home, shutdown, and Agent epoch
  change; each path must stop both workers and leave no stale packet for a new
  connection.
- Verify `PTT_END` summary counts match accepted audio after delayed sends.
- Add a firmware contract assertion that `AppCodexStatus` does not drain the
  production voice queue.

### Physical acceptance

1. Perform at least ten 60-second Mandarin dictation sessions.
2. During sessions, trigger Codex task alerts, carousel changes, Agent-panel
   pull-down, and ordinary page rendering.
3. Confirm every intentional release produces `recording -> ready`, no error
   5, no disconnect, and restored input state.
4. Repeat after an Agent reconnect and after switching away and back.
5. Capture peak queue depth and longest accepted-send gap without recording
   audio or secrets.

## Acceptance Criteria

- A completely stalled Codex UI cannot stop or delay the independent voice
  capture/TX pipeline.
- Automated tests demonstrate at least 60 seconds of continuous ordered
  delivery without UI polling.
- All existing host validation remains green.
- The application firmware builds within existing partition and static-memory
  gates.
- Ten physical 60-second stress sessions complete normally.
- A genuine persistent BLE failure remains bounded, diagnosable, and safe.
