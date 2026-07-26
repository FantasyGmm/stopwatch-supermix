# Power LED Blink Interval Design

## Goal

Allow the user to choose how many seconds elapse between Power LED flashes while preserving the existing Off, On, and Blink modes and the established Settings visual style.

## Interaction

- The Power LED page keeps the three exclusive mode rows: `Off`, `On`, and `Blink`.
- Selecting `Blink` reveals an interval label and slider below the mode rows.
- The label uses the form `Interval: 2s` and updates while the slider is dragged.
- The slider range is 1 through 10 seconds with a one-second step.
- Selecting `Off` or `On` hides the interval controls without discarding the selected interval.
- Dragging the slider immediately restarts the Blink preview using the new interval.
- The existing green `OK` button remains the only save action.
- Leaving without pressing `OK` restores both the original mode and original interval.

## Layout

- The three mode rows become slightly shorter and move upward to create a dedicated interval area.
- The interval label sits directly above the slider.
- The slider uses the existing Settings colors: dark gray track, green indicator, and white knob.
- The green `OK` button retains its current size, position, typography, and colors.
- When hidden, the interval controls do not intercept touch input.

## Firmware Model

- Add a persisted blink interval in seconds with a default of 2 seconds.
- Store it in the existing `system` NVS namespace under `pwr_led_interval_s`.
- Clamp loaded and requested values to the supported 1-10 second range.
- Keep the LED pulse width fixed at 160 milliseconds; only the time between pulse starts changes.
- The HAL remains responsible for blink scheduling. The Settings worker only previews, commits, or restores the HAL configuration.
- Existing installations without the new NVS key continue to blink every 2 seconds.

## Save And Restore

- Entering the page captures the original mode and interval.
- Mode or interval changes are previewed without writing NVS.
- Pressing `OK` writes both values and exits.
- Destroying the worker before confirmation restores both captured values.

## Validation

- Contract tests cover the new HAL API, NVS key, clamping, default value, slider range, conditional visibility, preview, commit, and restore behavior.
- The complete Python regression suite must pass.
- The ESP-IDF firmware build and partition capacity check must pass.
- Hardware verification must confirm 1-second, 2-second, and 10-second intervals, persistence after reboot, and unchanged Off and On behavior.

## Non-Goals

- Adjusting LED peak brightness or the 160-millisecond pulse width.
- Sub-second intervals or free-form numeric input.
- Changing other Settings pages.
