# Clock App Integration Design

## Goal

Replace the separate Alarm Clock, Watch Face, and Stopwatch launcher applications with one `Clock` application. Clock presents a selection page first, preserves all three feature sets, and uses the existing Alarm Clock launcher icon.

## Application Architecture

The launcher installs one `AppClock` in place of `AppAlarmClock`, `AppWatchFace`, and `AppStopWatch`. Its app name is `Clock` and its launcher image is `icon_clock`.

`AppClock` owns a four-state controller:

- `Selection`
- `Alarm`
- `WatchFace`
- `Stopwatch`

The three old `AppAbility` wrappers are removed. Their existing functional units remain reusable Clock modules: the alarm model and alarm views, `WatchFaceManager`, and `StopwatchView`. Entering a feature constructs its existing view. Returning to Selection destroys that view. Re-entering therefore has the same reset semantics as closing and reopening the former standalone application.

The alarm model belongs to `AppClock` for the complete application lifetime. `AppClock::onRunning()` updates it in every Clock state, and `AppClock::onSleeping()` continues updating it while another launcher application is active. Alarm triggering and confirmation retain their current blocking presentation and HAL alarm behavior.

## Selection Page

Clock always opens on Selection; it does not remember the feature used in the previous session. The selection page uses the approved vertical-button layout:

- black full-screen background;
- centered `CLOCK` heading;
- three large rounded text buttons labeled `Alarm`, `Watch Face`, and `Stopwatch`;
- no new bitmap assets.

Selecting a button enters the corresponding feature. The feature's existing visual layout and touch behavior remain unchanged.

## Button Routing

The existing two-button hold remains the global Home gesture in every Clock state and closes Clock directly to the launcher.

While a feature is active, holding the upper-left hardware button (`btnA`) for approximately 500 ms returns to Selection. This single-button hold does nothing on Selection. A simultaneous two-button hold has priority over the single-left-button hold, so Home cannot be intercepted by feature navigation.

Watch Face retains short-left and short-right navigation. Stopwatch retains immediate right-button start/pause behavior. Its left-button reset/lap action moves from press time to short-release time so the same button can distinguish a short action from the long hold used to return to Selection. A long-left hold must not also reset or record a lap.

When an alarm is actively presenting its confirmation overlay, its current blocking behavior remains authoritative: the user confirms on screen before other navigation resumes.

Transitions clear feature-specific UI objects and reset button latches before the next state begins. This prevents the hold that initiated a transition from becoming a short press or immediately triggering another state change.

## Assets And Registration

Keep `main/assets/images/icon_clock.c` and its `icon_clock` declaration. Remove the Watch Face and Stopwatch launcher image sources and declarations because no launcher entry consumes them after integration.

Remove the three old application registrations and register only `AppClock`. Update the shared application include header accordingly. Functional view, model, and watch-face image assets remain in the build.

## Failure Behavior

If the alarm model cannot initialize, Clock and its other features remain available. The Alarm selection control is visibly disabled and does not enter an invalid alarm state. The initialization failure is logged using the existing application logging conventions.

View creation and destruction remain inside the LVGL lock. A state transition completes cleanup before constructing the next view, so two feature roots never own the active screen simultaneously.

## Documentation And Testing

Update the README launcher list and feature summary to describe one Clock application with Alarm, Watch Face, and Stopwatch subfeatures.

Host-side contract tests verify:

- the launcher registers `AppClock` exactly once;
- `AppClock` uses `icon_clock`;
- the three old application wrappers are not registered;
- `icon_watch_face` and `icon_stopwatch` declarations and image sources are removed;
- the three selection labels and state transitions are present.

Button-routing tests cover short-left, long-left, short-right, and simultaneous-button Home priority. Stopwatch tests prove short-left still performs reset/lap on release and long-left performs navigation without a reset/lap side effect.

Run all relevant host tests, `git diff --check`, and an ESP-IDF 5.5.4 firmware build. After a successful build, flash the launcher to the currently connected StopWatch and verify:

1. The launcher contains one Clock entry using the Alarm Clock icon.
2. Clock opens on Selection with three vertical buttons.
3. Each feature retains its existing touch and display behavior.
4. Long-left returns from each feature to Selection.
5. Two-button hold returns directly to the launcher from Selection and every feature.
6. Watch Face short navigation and Stopwatch controls behave as specified.
7. An enabled alarm continues updating and can trigger while another Clock feature or another launcher application is active.

## Acceptance Criteria

- Alarm Clock, Watch Face, and Stopwatch no longer appear as separate launcher applications.
- Clock contains all three functional areas behind the approved selection page.
- Clock uses `icon_clock`; unused Watch Face and Stopwatch launcher icon sources are removed.
- Existing Home behavior is unchanged, and long-left provides feature-to-selection navigation without Stopwatch side effects.
- Alarm scheduling and triggering continue while Clock is running or sleeping.
- Tests pass, firmware builds, and the merged application is flashed and verified on the connected device.
