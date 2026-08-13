# Codex App Global Right-Button Submit Design

**Date:** 2026-08-11

## Goal

Give the upper-right hardware button a global meaning inside the StopWatch
Codex app. A short single click submits the current Codex desktop composer. A
fast double click steers the currently running Codex turn. The existing hold
gesture continues to control push-to-talk voice input.

This behavior applies on every page and in every conversation state inside the
watch Codex app. It does not apply in the launcher, clock, Assistant, or any
other watch application.

## Confirmed Interaction

- Hold the upper-right button for 500 ms to start push-to-talk. The separate
  90 ms chord guard remains reserved for A+B Home arbitration. Releasing the
  right button stops push-to-talk as it does today.
- Complete one short click and wait 300 ms without a second click to request a
  normal submit.
- Complete a second short click within the 300 ms window to request a forced
  steer. The double click produces one action and never first emits a single
  click.
- If a second press begins inside the double-click window but becomes a hold,
  the firmware cancels the pending single click and starts voice input. It must
  not submit while the user is beginning dictation.
- A wake press remains consumed. The A+B Home chord retains priority. Screen
  wake, Home, app close, and input suppression clear pending click state.
- The actions are available at any time in the watch Codex app. They are not
  armed by, or limited to, a preceding voice session.

## Responsibility Boundary

The watch owns physical gesture classification because it observes button
edges without BLE scheduling jitter. The Mac Agent owns desktop meaning because
it can inspect the foreground application and the current Codex composer.

### Watch

A focused gesture classifier extends the existing right-button edge gate. It
distinguishes hold, single click, and double click while preserving wake and
Home suppression. It emits only semantic `Submit` or `ForceSteer` actions after
the gesture is final.

Button actions travel over an authenticated, replay-resistant BLE action path
that is independent of Typeless readiness and the audio session lifecycle. The
messages carry a connection-scoped identity and monotonic action sequence so
the Agent can reject stale or duplicate actions. Losing the BLE link drops the
action instead of retrying it in a way that could submit twice.

### Mac Agent

A dedicated Codex submit controller consumes decoded button actions. Before
acting, it requires:

1. the foreground application bundle identifier to equal `com.openai.codex`;
2. a visible current-conversation composer to be available through macOS
   Accessibility; and
3. non-empty composer input.

For `Submit`, the controller invokes the current normal submit behavior. For
`ForceSteer`, it inspects the current submit semantics:

- idle composer: submit once normally;
- running turn whose normal action is already Steer: invoke that action;
- running turn whose normal action is Queue: invoke the alternate submit
  shortcut that performs Steer.

The production adapter accepts only the real Codex task-composer shape: an
editable `AXTextArea` with `ProseMirror`, a description from the current 16-ID
English/Simplified-Chinese allowlist, stable `ChromeAXNodeId` tokens, and the
`_RichTextInput_ -> _ComposerLayoutBody_` ancestor chain. A unique enabled
Send, Queue, or Steer button must be present inside the bounded local subtree.

Immediately before dispatch, the adapter revalidates the foreground PID,
composer and button tokens, text, and semantics. Normal Send, Queue, and Steer
invoke `AXPress` on that verified button. Only Queue-to-forced-Steer emits a
complete Command+Return down/up pair through `CGEventPostToPid` for the
verified Codex PID. It never sends an unqualified Return key to an unknown focus
target and never activates Codex.

Accessibility revalidation and event delivery cannot be atomic. A same-process
focus change inside Codex after the final check is a residual race; at-most-once
delivery and no retry prevent an ambiguous result from becoming a duplicate.

## Data Flow

```text
upper-right button edges
  -> watch gesture classifier
  -> Submit | ForceSteer
  -> authenticated BLE action notification
  -> Agent replay/deduplication guard
  -> Codex foreground and composer inspection
  -> normal submit | forced steer | safe ignore
```

The action subscription remains available whenever the Agent and watch have a
usable authenticated BLE connection. Typeless, BlackHole, and microphone
readiness only gate push-to-talk; they do not gate button submission.

## Failure Handling

- If Codex is not foreground, ignore the action and log the reason.
- If the composer is missing, empty, or semantically ambiguous, ignore the
  action and log the reason.
- If Accessibility permission is unavailable, expose the existing
  Accessibility-required Agent state and do not synthesize input.
- Reject malformed, unauthenticated, stale, replayed, and duplicate BLE action
  messages.
- Clear pending watch gesture state on disconnect, wake suppression, Home, and
  app close.
- Do not queue an action across reconnects and do not retry after an ambiguous
  desktop outcome; at-most-once submission is safer than duplicate submission.
- Treat the residual same-PID focus race between Accessibility revalidation and
  event delivery as bounded but unavoidable; `CGEventPostToPid` prevents
  cross-application delivery but not a later focus change within Codex.

## Testing

### Firmware

- A single click emits one `Submit` only after 300 ms.
- A double click emits one `ForceSteer` and no preceding `Submit`.
- A second press that reaches 500 ms cancels the pending click and starts
  push-to-talk.
- Wake, A+B Home, app close, disconnect, suppression, and 32-bit timer wrap do
  not leave a pending action.
- The 500 ms long-press voice threshold and cancellation behavior remain
  intact.

### Protocol and Agent

- Round-trip action encoding and decoding, authentication failure, replay
  rejection, stale connection identity, and duplicate sequence handling.
- Codex foreground and non-foreground cases.
- Idle Send, running Queue, and running Steer composer states.
- Empty composer, missing Accessibility permission, missing Accessibility
  element, and ambiguous semantics all fail closed.
- Button actions remain usable when Typeless or the audio route is unavailable.

### Regression and Hardware

- Run the existing voice, BLE, firmware contract, and Mac Agent test suites.
- Build the unified firmware candidate.
- On hardware, verify single click, double click, long-press voice, A+B Home,
  and screen-off wake behavior with Codex both foreground and background.

## Non-Goals

- System-wide StopWatch button behavior outside the Codex app.
- Activating or foregrounding Codex automatically.
- Submitting to a background Codex window.
- Persisting or retrying button actions across BLE disconnects.
- Changing the user's Codex follow-up queue preference.
- Adding a new watch UI for this feature.
