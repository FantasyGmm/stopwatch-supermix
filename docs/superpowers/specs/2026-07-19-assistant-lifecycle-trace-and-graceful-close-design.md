# Assistant Lifecycle Trace and Graceful Close Design

**Date:** 2026-07-19  
**Status:** Approved under the user's standing authorization to make debug design decisions autonomously

## 1. Problem

Returning from Assistant with the two side buttons reaches the `Closing` view but can fail to return to Launcher. The existing USB command `debug app home` does not reproduce that path: it asks Launcher to close the active Mooncake application immediately, while the physical gesture first asks `AssistantRuntime` to stop and waits for a real `Stopped` boundary.

The physical path also plays the legacy button sound before `KeyManager` reports `GoHome`. That sound opens the HAL player directly even when Assistant owns the shared playback resource. It can therefore interfere with Brookesia's use of the same speaker and I2S backend during shutdown.

Current logs contain useful memory snapshots, but they do not consistently identify the session, event order, close trigger, state transition, submission result, callback, or resource ownership. A single reproduction cannot be reconstructed reliably.

## 2. Goals

1. Prevent button feedback from touching playback hardware while a supervised runtime owns playback.
2. Make physical and automated graceful-close requests enter one Assistant application path.
3. Emit a compact, ordered trace for every important Assistant lifecycle boundary.
4. Allow the host to dump the current in-memory trace and status without touching the UI.
5. Keep continuous audio packets and conversation content out of the trace.
6. Compile the debug-only trace buffer and commands out of production builds.

## 3. Non-goals

- Persisting traces to Flash or surviving power loss.
- Logging microphone frames, encoded audio packets, or full user/agent text.
- Changing Brookesia, XiaoZhi protocol behavior, or the two-second stop timeout.
- Replacing existing component logs; the trace provides a correlated lifecycle index around them.

## 4. Selected Approach

Use a fixed-capacity structured trace in RAM, enabled only by `CONFIG_STOPWATCH_DEBUG_COMMANDS`. Each record contains numeric fields rather than owned strings, so recording is bounded and allocation-free. Every record is also printed immediately with the stable `ASTRACE` prefix. A dump command reprints the retained records in sequence order.

This is preferred to plain additional logs because it is machine-readable and can detect missing lifecycle boundaries. It is preferred to Flash persistence because current debugging is attached over USB, and persistence would add write wear and recovery complexity without helping the reported live hang.

## 5. Trace Model

Each record contains:

- monotonic `sequence`;
- Assistant `session_id`;
- `timestamp_ms`;
- `component`: App, Runtime, Callback, Resource, Button, or Debug;
- `event`: Open, StartRequest, StateTransition, ResourceDecision, ActionSubmit, Callback, CloseRequest, StopTimeout, Release, Closed, or Error;
- current `AssistantRuntimeState`;
- `detail` and `result` numeric enums;
- optional compact diagnostic values for resource ownership, free heap, and task count at boundary events.

The buffer retains 64 records. On overflow it overwrites the oldest record and increments a dropped counter. Dump output starts with the retained range and dropped count. No trace record owns dynamic memory.

Stable output example:

```text
ASTRACE sid=3 seq=41 t=91234 comp=app event=close-request state=running detail=physical-home result=accepted
ASTRACE sid=3 seq=42 t=91235 comp=runtime event=state state=stopping detail=request-stop result=accepted
ASTRACE sid=3 seq=43 t=91302 comp=callback event=lifecycle state=stopping detail=stopped result=accepted
ASTRACE sid=3 seq=44 t=91308 comp=runtime event=release state=stopped detail=all-resources result=ok wifi=0 capture=0 playback=0 tasks=18
```

The immediate serial form and dump form use the same renderer. Conversation events record only event kind and payload length.

## 6. Instrumentation Boundaries

### Application

`AppAssistant` records open, close trigger, stop-request result, observed `Stopped`, and `onClose`. A private `requestGracefulClose(trigger)` method becomes the sole entry for physical and debug close requests. It sets `Closing`, records the trigger, and asks `RuntimeSupervisor` to stop exactly once.

### Runtime

`RuntimeSupervisor` records the start request/result and diagnostics at before-start, first-running, stopped, and stop-timeout boundaries. `AssistantRuntime` records accepted/rejected state transitions, resource acquisitions, Brookesia action submission/completion, lifecycle callbacks, timeout, and release completion. Callback records are safe, allocation-free writes; rendering stays on the main runtime thread.

### Button feedback

A pure policy determines whether button SFX is allowed. SFX is suppressed whenever `ResourceSupervisor::snapshot().playback_owner != RuntimeOwner::None`. Vibration and raw button state updates remain unchanged. A suppression trace includes the current playback owner. This preserves the gesture while preventing the legacy player from colliding with Assistant audio.

## 7. Debug Commands

Add these commands under `CONFIG_STOPWATCH_DEBUG_COMMANDS`:

- `debug assistant home`: enqueue a graceful close request for the active Assistant. It does not call `Mooncake::closeApp` directly.
- `debug assistant status`: print the current session, state, resource owners, binding/subscription counts, heap, and task count.
- `debug assistant trace`: dump retained `ASTRACE` records and dropped count.
- `debug assistant trace clear`: clear retained records and reset the dropped count without changing the active session.

`debug app home` remains available as an emergency force-close command and is explicitly logged as `force-home`, so test scripts cannot confuse it with graceful shutdown.

The Assistant debug target is registered only while `AppAssistant` is open. Commands issued at other times return `BUSY` deterministically.

## 8. Host Automation

`watch_debug.py` gains matching commands. The graceful-close validation performs:

1. clear trace;
2. open Assistant;
3. wait for `first-running` for the new session;
4. issue `debug assistant home`;
5. wait for `close-request`, `Stopping`, callback `Stopped`, release, and Launcher open;
6. request status and verify bindings, subscriptions, Wi-Fi leases, capture owner, and playback owner are zero/none;
7. repeat the full cycle once to detect leaked tasks or resources.

On failure the script automatically dumps the retained trace before exiting.

## 9. Error Handling

- A rejected close request is traced and leaves the `Closing` UI visible only while the runtime reports an active stop state.
- A stop timeout records state, submission status, owners, bindings, subscriptions, heap, and tasks before the existing timeout page is shown.
- Late callbacks include their source session. Stale-session callbacks are traced as rejected and never mutate the active session.
- Trace writes must never block, allocate, call LVGL, or invoke service APIs.

## 10. Tests

Implementation follows test-first order:

1. A pure button-feedback policy test fails until supervised playback suppresses SFX.
2. Trace buffer tests cover ordering, overflow, dropped count, clear, and stable enum rendering.
3. Parser tests cover all four Assistant commands and malformed input.
4. Assistant close-controller tests prove physical and debug triggers produce the same one-shot stop request.
5. Existing offline validation remains green.
6. A unified firmware build must pass capacity checks.
7. Hardware validation must pass two open/graceful-close cycles, show Launcher after each cycle, show no reset, and finish with no resource owners, bindings, or subscriptions.

## 11. Acceptance Criteria

- Two-button return no longer starts legacy HAL audio while Assistant owns playback.
- The physical gesture and `debug assistant home` share the same graceful-close code path.
- A single captured session can be reconstructed in order from `ASTRACE` records.
- A timeout or unexpected reset leaves enough live serial evidence to identify the last completed boundary; persistence across power loss remains out of scope.
- Two consecutive hardware cycles return to Launcher and pass the existing resource/leak gate.
- Production builds can disable all new trace storage, USB commands, and debug rendering through `CONFIG_STOPWATCH_DEBUG_COMMANDS=n`.
