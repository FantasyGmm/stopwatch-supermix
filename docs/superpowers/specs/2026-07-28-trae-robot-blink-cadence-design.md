# Trae Robot Blink Cadence Design

## Goal

Make the Trae status-page robot animation easy to notice during normal viewing
without making the character appear restless or distracting.

## Approved Timing

- Reduce the idle frame duration from `2200 ms` to `1300 ms`.
- Increase the action/blink frame duration from `180 ms` to `320 ms`.
- Keep the existing two-frame timer sequence: idle, action, idle.
- Keep task-change animation triggers on the same `320 ms` action duration.

The resulting autonomous cycle is approximately `1620 ms`, with the action
frame visible for about 20 percent of the cycle instead of about 8 percent.

## Scope

Only the Trae robot cadence constants and their behavioral tests change. The
approved robot assets, size, position, task carousel, page swipe interaction,
Codex page animation, and inactive-page timer behavior remain unchanged.

The timer continues to pause while the Trae page is inactive. Re-entering the
page starts from the idle frame and waits one idle interval before the first
blink. A task change may trigger the action frame immediately, after which the
normal cadence resumes.

## Verification

- A host test must assert the approved `1300 ms` idle duration and `320 ms`
  action duration before production constants are changed.
- The Trae layout/timer host test must pass after implementation.
- Firmware contract tests must continue to pass.
- The unified firmware must build successfully and pass capacity gates.
- On-device visual acceptance remains separate: the blink should be noticeable
  during a short observation without appearing continuously animated.

