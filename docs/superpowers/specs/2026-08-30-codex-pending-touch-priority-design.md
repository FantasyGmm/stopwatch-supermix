# Codex Pending Touch Priority Design

## Goal

Keep the original approved Pending red-halo implementation unchanged while
making horizontal page swipes responsive when LVGL rendering is blocked.

## Root Cause and Evidence

The halo is rendered and touch was sampled inside the same LVGL task. With the
original halo active, a redraw can hold `lv_timer_handler` for roughly
240--255 ms. Short swipes therefore commonly reached the application as only
Pressed then Released, with no movement point.

A hardware A/B disabled only the halo. Touch gaps returned to roughly
34--55 ms and the user completed ten of ten swipes. A separate hardware probe
then sampled CST820 from a priority-2 task every 10 ms while leaving the real
halo and priority-1 LVGL task active. It captured 15--23 samples and 9--14
movement points per gesture even when LVGL observed only Pressed and Released.
This proves that independent sampling preserves the missing trajectory.

## Rejected Experiments

The following experiments did not solve the root cause and are not part of the
production implementation:

- cached full-screen A8 halo mask;
- twelve perimeter A8 halo tiles;
- reduced or segmented halo invalidation;
- pausing and resuming the halo animation on touch;
- binding the halo object to LVGL input events;
- temporary touch/render timing logs and prototype sampler code.

The production tree restores the original single-overlay halo renderer,
geometry, color, opacity endpoints, phase quantization, invalidation regions,
and 1.6-second animation cycle from `HEAD`.

## Production Behavior

- A dedicated priority-2 FreeRTOS task is the sole runtime CST820 reader.
- It samples every 33 ms while idle, matching the existing LVGL input period,
  and every 10 ms only from detected contact through release.
- A fixed 32-sample queue stores timestamped touch states without allocation.
- The LVGL callback consumes one logical state per read cycle. A completed
  backlog is delivered as first Pressed, newest movement, then Released.
- Original hardware timestamps are used for Codex gesture duration checks, so
  delayed LVGL delivery cannot invalidate a physically fast swipe.
- Movement is coalesced under pressure and terminal release is preserved, so
  an overflowing queue cannot leave LVGL logically pressed.
- Arming screen-off suppression clears the queue. A generation check also
  rejects an I2C read that started before the clear and completed afterward.
- Swipe thresholds, gesture ownership, wake consumption, halo rendering, and
  all other application behavior remain unchanged.

## Resource Cost

`TouchSampleQueue` occupies 552 bytes on the host ABI and the sampler uses a
4 KiB task stack. Idle I2C cadence is not increased; the 10 ms cadence is paid
only during active physical contact. The queue is bounded and performs no
dynamic allocation.

## Verification

- Queue host tests cover stalled swipes, ongoing holds, deduplication,
  saturation, release preservation, reset, timestamp wrap, and adaptive
  sampling periods.
- The touch-gate integration test passes a buffered swipe through the real
  wake gate, edge-control logic, and page-swipe recognizer.
- Existing Pending-glow tests and firmware contracts prove the original halo
  implementation remains intact.
- Firmware must compile with ESP-IDF 5.5.4 and pass the capacity gate.
- Flash only the application partition; preserve NVS, pairing, storage, and
  assets.
- Physical acceptance requires the original flashing halo to remain visually
  unchanged and repeated alternating swipes to succeed on the AMOLED.
