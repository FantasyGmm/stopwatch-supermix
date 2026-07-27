# Global Low-Battery Alert Design

## Goal

Add one low-battery alert that works on every application and page. When the
battery enters the low state, the watch plays a short warning sound and shows a
temporary text notification without requiring individual applications to
implement battery checks.

The low state begins below 30 percent. One low-battery episode produces one
alert. Connecting external power or recovering to at least 35 percent rearms
the next episode.

## Confirmed Product Behavior

- The alert applies globally to Launcher, Assistant, Codex, Clock, Settings,
  and every other current or future application.
- Two consecutive one-second samples below 30 percent are required before an
  episode starts. This filters the uninitialized startup value and brief voltage
  noise.
- An episode alerts once. It does not repeat on a timer, application switch, or
  screen wake.
- External power insertion suppresses the alert even if the reported battery
  remains below 30 percent. It also rearms the next battery-powered episode.
- A battery level of 35 percent or higher rearms the next episode. Values from
  30 through 34 percent retain the current armed or alerted state, preventing
  repeated alerts near the threshold.
- Screen-off does not wake or brighten the display.
- The warning sound must still play while the screen is off.
- If another subsystem owns or is actively using playback, the warning sound is
  queued and plays after playback becomes available. It must not interrupt
  Assistant speech, an alarm, or another active sound.
- The text notification is queued while the screen is off and appears on the
  next screen wake. If external power is connected before that wake, the queued
  text is cancelled.
- A muted speaker remains muted. The alert is considered handled without
  forcing a nonzero volume.

## Chosen Architecture

Use a dedicated `GlobalLowBatteryAlertService` initialized and updated by the
main loop. The service owns a pure `LowBatteryAlertLogic` state machine, a
top-layer LVGL notification, and the pending sound lifecycle.

This is preferable to adding the behavior to `GlobalEdgeControlService` because
edge controls are disabled on some Settings pages and have a different
responsibility. It is preferable to per-application checks because that would
duplicate policy and leave future applications unprotected.

The service does not create a FreeRTOS polling task. It advances from the
existing main loop and only evaluates battery state once per second, matching
the PMIC sampling cadence.

## Battery Policy

`LowBatteryAlertLogic` is dependency-free and host-testable. It receives:

- monotonic time;
- battery percentage;
- the latest successful PMIC battery-sample revision;
- whether external power is present;
- whether the physical display output is visible;
- whether the notification text is currently visible.

It emits one-shot commands rather than touching HAL or LVGL:

- queue warning sound;
- show notification text with the sampled percentage;
- hide notification text;
- cancel pending alert work.

The state machine has these logical phases:

- `Armed`: waiting for two consecutive valid samples below 30 percent;
- `LowPending`: the episode started and sound/text delivery may still be
  pending;
- `LowAlerted`: the episode has delivered or queued all required effects and
  cannot alert again until rearmed.

HAL exposes a thread-safe read-only `batterySampleRevision()` counter. The PMIC
task increments it only after a successful battery ADC read has updated the
filtered level. The service counts a low observation only when the revision
changes, so one ADC sample cannot satisfy both confirmation slots. Revision zero
is invalid. If the device genuinely starts below 30 percent, it alerts after the
second real sample.

External power uses `GetHAL().isBatteryCharging(false)`. The non-strict form is
intentional: a plugged-in, fully charged device is externally powered even when
the charging pin is inactive.

## Sound Delivery

The warning is a short, distinct two-tone descending melody generated through
the existing common audio helper. No encoded audio asset is added.

Before starting the tone, the service checks both playback ownership and actual
HAL playback activity:

1. If `ResourceSupervisor` reports another playback owner, keep the sound
   pending.
2. If the HAL is still playing audio that predates resource ownership, keep the
   sound pending.
3. Otherwise acquire playback as `RuntimeOwner::System`, start the tone, and
   retain ownership until HAL reports playback idle.
4. Release System playback immediately after the tone completes.

HAL therefore exposes a thread-safe read-only `isAudioPlaying()` query backed by
the existing audio codec state. The query does not stop, replace, or enqueue
audio.

This ordering guarantees that a low-battery event detected during Assistant
speech or while the display is off is not lost. The visual and sound deliveries
are independent: visible text may appear before a busy audio path becomes
available.

If external power is connected while sound is still pending, cancel the pending
sound. An already-started sub-second warning is allowed to finish so the audio
backend is not interrupted again.

## Global Notification UI

The notification is created on `lv_layer_top()` so it survives application and
page changes. It is presentation-only and does not accept touch input.

The visible form is a compact centered banner inside the round display safe
area:

- position: horizontally centered, above the screen midpoint;
- primary text: `电量低  28%` using the bundled PuHui CJK font;
- secondary text: `请及时充电` using the same font;
- percentage emphasis: existing Montserrat font at a larger size where useful;
- color: warm warning orange/red with high-contrast white text;
- duration: four seconds of actual visible-screen time.

The banner uses LVGL primitives and existing fonts only. It adds no bitmap or
audio assets. Its dimensions are fixed and constrained to the circular safe
area so the longest text does not clip at the panel edge.

Screen-off time does not consume the four-second text duration. A notification
queued during screen-off begins its visible timer only after the physical
display becomes visible. The service needs a HAL-level read-only query for the
effective physical display brightness so this remains correct for both the
current manual screen-off flow and the planned global AMOLED protection flow.

If the display turns off while the banner is visible, hide it without clearing
the remaining requirement and show a fresh four-second notification on the next
wake unless external power has been connected.

## Main-Loop Integration

Initialization occurs after HAL/LVGL setup. The update order is:

1. runtime supervisor update;
2. global display-protection update when that service is present;
3. low-battery policy and LVGL overlay update under the LVGL lock;
4. global edge-control update under the same LVGL lock;
5. pending hardware/audio effects outside the LVGL lock;
6. Mooncake application update.

The low-battery service never calls audio playback while holding the LVGL lock.
It records a pending effect under the lock and applies it afterward. This keeps
audio and display driver work out of the UI critical section.

The service remains enabled on Settings pages even when global edge controls
are disabled. No application may hide or destroy the notification directly.

## Failure and Recovery

- A failed or unavailable external-power read is treated as battery operation,
  matching the conservative power policy elsewhere in the firmware.
- A failed LVGL initialization disables text presentation but leaves the sound
  policy active.
- A failed playback acquisition leaves the sound pending for a later update.
- Service reinitialization starts armed and requires fresh consecutive samples;
  it does not infer an alert from stale UI state.
- Unsigned monotonic-time subtraction is used so timing remains correct across
  millisecond counter wraparound.

## Verification

Host tests cover:

- one low sample does not alert;
- two consecutive samples below 30 percent trigger exactly once;
- 30-34 percent does not rearm or retrigger;
- 35 percent rearms;
- external power suppresses and rearms;
- startup zero cannot bypass confirmation;
- screen-off queues text but still requests sound;
- wake starts the four-second visible interval;
- external power cancels queued text and pending sound;
- sound remains pending while playback is occupied and is emitted once after
  release;
- display off during a visible banner defers a fresh full-duration display.

Firmware contract tests verify:

- initialization and update occur in the global main-loop path;
- the overlay uses `lv_layer_top()` and does not accept touch input;
- the service remains independent of Settings and edge-control enablement;
- sound application occurs outside the LVGL lock;
- the complete offline validation runner executes the new host test.

Final validation requires the complete offline suite, a clean unified firmware
build with capacity gates, and hardware checks on at least Launcher, Assistant,
Settings, and a screen-off state. Hardware acceptance must verify that the tone
does not interrupt Assistant output and that screen-off remains dark while the
tone plays.

## Out of Scope

- periodic low-battery reminders;
- vibration;
- waking the display for the alert;
- a user-configurable threshold or Settings toggle;
- forced sound while speaker volume is zero;
- shutdown or deep-sleep policy at critically low battery.
