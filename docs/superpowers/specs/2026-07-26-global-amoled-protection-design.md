# Global AMOLED Protection Design

## Goal

Protect the watch's CO5300 AMOLED panel from long-lived static content without
stopping applications or background services. Every visible application except
Codex follows one consistent inactivity policy:

- after 15 seconds without user or approved application activity, temporarily
  cap display brightness at 20 percent;
- after 60 seconds without activity, set effective display brightness to zero;
- restore the user's previous brightness on wake without changing the saved
  brightness setting.

Codex remains outside this automatic policy because it already owns a
button-driven screen-off and wake flow. This change must not alter that flow.

## Product Decisions

The inactivity timer is reset by physical user input, not by ordinary UI
animation. A changing watch face, IMU graph, FFT visualization, launcher
animation, or other periodic redraw therefore does not keep the panel awake.

Three application states explicitly keep the display awake:

- Assistant has an active voice interaction, including capture/listening,
  waiting for the current response, or speech playback;
- Stopwatch is in its Running state;
- an alarm is actively ringing.

When the final keep-awake state ends, the service starts a fresh 15/60-second
inactivity period. It does not inherit time accumulated before the active
operation.

Assistant being merely open is not a keep-awake condition. When it is waiting
without voice activity, it follows the normal inactivity policy. Visual
animation alone is not Assistant activity.

Stopwatch remains a keep-awake condition for as long as its model is Running,
including when the Clock UI leaves the Stopwatch subpage or returns to Launcher.
Paused and Stopped do not keep the display awake. Destroying the Clock
application clears its reason defensively.

An alarm that starts while the display is dimmed or off immediately restores the
display and holds it awake until the alarm is dismissed.

## Chosen Architecture

Use one global display-protection service with a pure policy state machine and
named, idempotent keep-awake reasons. Applications publish only the semantic
state that matters; they do not implement their own timers or brightness
transitions.

This is preferable to copying timeout code into each application because new
applications inherit the safe default automatically, timing remains consistent,
and temporary brightness has one owner. Implementing the entire policy in HAL
was rejected because HAL cannot determine whether Assistant, Stopwatch, or an
alarm is semantically active. Inferring activity from screen redraws was also
rejected because animated but unattended screens would never turn off.

## Component Boundaries

### DisplayProtectionLogic

`DisplayProtectionLogic` is a pure, host-testable state machine with three
states:

- `Active`
- `Dimmed`
- `ScreenOff`

It accepts the current monotonic time and events for user activity, automatic
policy enablement, and keep-awake reason changes. It emits transition commands
instead of touching HAL, LVGL, Mooncake, or application state.

The timing contract is:

- initialization, application-policy re-enable, user activity, and release of
  the final keep-awake reason set the inactivity origin to `now`;
- at 15,000 milliseconds of inactivity, `Active` becomes `Dimmed`;
- at 60,000 milliseconds of inactivity, `Active` or `Dimmed` becomes
  `ScreenOff`;
- user activity in `Dimmed` restores `Active` and remains available to the
  application;
- user activity in `ScreenOff` restores `Active` but marks that physical input
  for consumption;
- acquiring any keep-awake reason immediately restores `Active` and suppresses
  both timeout transitions until all reasons are released;
- disabling automatic protection restores `Active`, clears automatic timing,
  and leaves brightness ownership to the excluded application;
- elapsed-time comparisons use wrap-safe unsigned subtraction.

The logic owns no dynamic allocation and does not use a periodic FreeRTOS task.
It advances from the existing main update loop.

### GlobalDisplayProtectionService

`GlobalDisplayProtectionService` owns `DisplayProtectionLogic`, the pre-dim
brightness to restore, and the fixed keep-awake reason set. It updates before
global edge controls and before `GetMooncake().update()` so an off-screen wake
gesture can be intercepted before the current application acts on it.

The service exposes narrow, idempotent APIs:

- enable or disable automatic protection for the current application policy;
- set a named keep-awake reason active or inactive;
- report semantic Assistant activity;
- query the current display-protection state for diagnostics and tests.

Named reasons are a fixed enum rather than an unbounded reference count. Calling
`setKeepAwake(reason, true)` repeatedly cannot leak a hold, and one subsystem
cannot accidentally release another subsystem's reason.

The initial and restored brightness is the user's current effective brightness,
clamped to the existing 10-100 user range. Temporary dimming uses
`min(user_brightness, 20)`. A user already at 10 or 20 is never brightened by
the dim transition. Screen off uses brightness zero. Neither temporary value is
written to NVS.

When the user changes brightness through Settings or the global edge control,
the new user value becomes the next restore value. Protection-originated writes
are tagged internally so they cannot overwrite the restore value or count as
user activity.

### Application Integration

Codex disables automatic protection on open and re-enables it on close. Its
existing two-second left-button screen-off behavior, wake handling, Pending glow
gating, and brightness restore value remain authoritative while Codex is open.
The global service must not interpret a zero brightness created by Codex as its
own `ScreenOff` state.

Assistant publishes activity from actual conversation events and runtime
presentation state. Listening/capture, the outstanding response interval, and
speech playback hold `AssistantInteraction`. Returning to the idle/waiting
state releases it. Generic view animation does not call the activity API.
Starting, stopping, failure, or a lost callback must converge to a released
reason once no conversation is active. Listening and speaking are authoritative
level states. Outside those states, an outstanding response must show real
conversation progress; 60 seconds without a listening, speaking, transcript,
response, or presentation event clears a stale Assistant hold so a failed
session cannot keep the AMOLED lit forever.

Clock publishes `StopwatchRunning` from the Stopwatch model whenever the model
enters or leaves Running. The reason is independent of the currently selected
Clock subfunction. Alarm presentation acquires `AlarmRinging` before making the
ringing UI/audio visible and releases it after dismissal and audio shutdown.

Launcher, Watch Face, Badge, IMU, FFT, Settings, and non-running Clock states
need no application-specific code. They receive the default automatic policy.

## Input Detection and Wake Consumption

Any physical touch or button press while `Active` or `Dimmed` is user activity.
In `Dimmed`, brightness restores immediately and the same input continues to the
current application.

The first physical input while `ScreenOff` is wake-only:

- for touch, restore brightness, call LVGL's wait-release/reset sequence, and
  keep the touch suppressed until physical release;
- for buttons, restore brightness and ask HAL's generic button-input gate to
  feed released states to the public button objects until both physical buttons
  have been released.

HAL continues sampling the raw GPIOs while that gate is active, so the wake
button cannot later emerge as a click, hold, navigation event, Stopwatch action,
or Home chord. The gate is a generic input-suppression primitive; it contains no
display-policy or application knowledge.

Touch and raw-button activity are observed before application update. The
service does not rely on LVGL object events or `KeyManager`, because several
applications read touch/button state directly.

## Application Switching and Lifecycle

Opening or returning to a normal protected application starts a fresh active
period. Switching to Codex disables only the automatic policy; switching away
re-enables it and restarts timing.

Keep-awake reasons survive view replacement when their underlying operation is
still active. They are cleared defensively when the owning runtime or model is
destroyed. A service restart restores safe defaults: automatic protection
enabled, no holds, active display, and a fresh inactivity origin.

No transition closes an application, suspends Mooncake, stops Stopwatch time,
disconnects BLE/Wi-Fi, or stops Assistant/alarm runtime work. This is display
protection, not system sleep.

## Interaction With Existing Global Controls

The display-protection service runs before `GlobalEdgeControlService`.

- While `Active` or `Dimmed`, a brightness gesture is ordinary user activity.
  Its final persisted value updates the protection restore value.
- While `ScreenOff`, the wake touch is consumed before it can become a
  brightness or volume gesture.
- When effective brightness is zero, the existing edge-control service remains
  suspended as it is today.
- Temporary dim/off transitions do not increment the global user-brightness
  revision and do not alter the Settings slider's persisted value.

## Failure Handling

- If the display or LVGL input device is unavailable, policy state still
  advances but display/input actions become safe no-ops and emit a bounded
  diagnostic message.
- If a keep-awake publisher disappears without releasing its reason, its owner
  lifecycle clears that fixed reason. There is no anonymous counter to leak.
- If brightness restoration encounters an invalid cached value, reload the
  existing persisted HAL brightness and clamp it to 10-100.
- If Codex closes while its own screen-off state is active, its existing close
  path restores brightness before automatic protection is re-enabled.
- Time jumps in RTC do not affect the policy because all intervals use HAL's
  monotonic millisecond clock.

## Diagnostics

State transitions log only the previous/new protection state, transition reason,
and active keep-awake reason mask. Repeated update loops do not log. No Assistant
transcript, audio data, BLE payload, or user content is recorded.

The existing debug command path should expose a read-only protection status for
automated hardware validation: state, effective brightness, restore brightness,
automatic-policy enabled flag, and named reason mask. Test-only time injection
belongs in the pure logic tests, not in production debug commands.

## Scope Exclusions

This change does not:

- change Codex's own screen-off gesture or Pending animation;
- add an always-on display mode or pixel shifting;
- enter ESP32 deep sleep or panel-controller sleep;
- change saved brightness defaults;
- treat ordinary animation as activity;
- change application navigation, button feedback, alarm scheduling, Stopwatch
  timing, Assistant audio behavior, or background connectivity;
- add a user-facing timeout settings page in this version.

## Verification

Host tests for `DisplayProtectionLogic` will prove:

- exact 15-second dim and 60-second screen-off boundaries;
- no early transition and direct Active-to-ScreenOff behavior after a delayed
  update;
- wrap-safe timing;
- activity reset in Active and Dimmed;
- wake-only consumption in ScreenOff;
- automatic-policy disable/re-enable behavior;
- immediate restore and timeout suppression for every named keep-awake reason;
- fresh timing after the final reason is released;
- idempotent repeated reason updates;
- brightness cap behavior for user values below, at, and above 20.

Firmware contract and focused integration tests will prove:

- service update ordering before edge controls and Mooncake;
- wake touch uses LVGL wait-release/reset;
- button wake suppression lasts until physical release;
- temporary brightness writes do not persist or replace user brightness;
- Codex disables the automatic policy without changing its current flow;
- Assistant holds only for real conversation activity;
- Stopwatch Running holds across Clock subpage changes, while Paused/Stopped do
  not;
- alarm ringing wakes and holds until dismissal;
- Watch Face and other visual animation do not publish activity;
- diagnostics contain state metadata but no user content.

The existing Python tests, native host tests, clean ESP-IDF build, firmware
capacity gate, and whitespace check remain required.

Hardware validation will cover every retained application:

- verify dim at 15 seconds and black display at 60 seconds;
- verify dimmed input both restores brightness and performs its intended action;
- verify the first off-screen touch and each button wake the display without an
  accidental application action;
- verify the next input works normally after release;
- verify Assistant remains awake through a continuous voice exchange but sleeps
  after the interaction becomes idle;
- verify a running Stopwatch does not dim or turn off, including after leaving
  its subpage and returning to Launcher, and that pausing restarts timing;
- verify an alarm wakes a dimmed/off display and remains visible while ringing;
- verify Codex's existing manual screen-off and Pending behavior are unchanged;
- verify restored brightness equals the user's pre-dim value and survives an
  application switch and reboot.

Offline success does not substitute for these physical display and input checks.
