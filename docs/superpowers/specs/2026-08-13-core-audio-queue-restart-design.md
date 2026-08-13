# Core Audio Queue Restart Design

## Problem

The prepared Core Audio output queue works for the first voice session but
fails during the second. `AudioQueueStop(queue, true)` resets the queue and
removes its scheduled buffers. The current restart path calls only
`AudioQueueStart`, so no callback consumes the native audio ring. After the
ring fills, a write accepts zero frames and the fail-closed voice service ends
the session.

## Design

Keep the prepared queue and its allocated buffers. Track whether those buffers
are currently scheduled. Opening the queue allocates and schedules all three
buffers. A synchronous stop marks them unscheduled. Before any later start,
clear and schedule those same buffers again, then start the queue.

If scheduling any buffer fails, dispose the queue immediately so a later
attempt must perform a clean prepare instead of reusing a partially scheduled
queue. Closing also resets the schedule state. The Python route lifecycle,
default-input lease, authenticated session protocol, and 300 ms watch threshold
remain unchanged.

## Verification

- A platform-neutral native lifecycle test proves that stop makes buffer
  scheduling necessary before restart.
- Existing native audio-ring and Mac Agent tests remain green.
- A real BlackHole exercise runs multiple prepare/start/write/stop cycles and
  verifies that every write accepts all frames.
- The rebuilt Agent is installed and relaunched; no watch flash is required.

## Self-review

The change addresses the observed failure at its source and preserves the
measured prewarming benefit. It does not switch to `AudioQueuePause`, retain
old audio, rebuild the queue per session, or weaken short-write failure
handling. There are no placeholders or unresolved choices.
