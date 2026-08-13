# Codex Right-Button Hold Threshold Design

## Problem

The Codex right-button edge gate currently starts push-to-talk after the
90 ms Home-chord guard. A normal deliberate click can last longer than 90 ms,
so it starts voice before release and can never become a submit action. A live
Mac Agent sample reproduced the symptom as a roughly 226 ms recording session.

## Decision

Keep the existing 90 ms chord guard for A+B Home arbitration, but give the
right button an independent 500 ms hold threshold for voice. A right-button
release before 500 ms is a short click and continues through the existing
300 ms single/double-click classifier. A right button that remains pressed at
500 ms starts voice exactly once; its eventual release ends voice.

The edge gate remains the only owner of physical timing. BLE input actions,
desktop Accessibility submission, authenticated replay protection, and the
single/double-click action protocol do not change.

## Priority and suppression

- A+B Home, wake consumption, and global input suppression retain priority.
- Pressing A at any point before voice starts suppresses the right-button
  candidate; it does not emit a click.
- Pressing A after voice starts cancels voice as before.
- A second click that remains pressed until 500 ms cancels the pending submit
  and starts voice; it cannot later emit `ForceSteer`.
- Unsigned elapsed-time arithmetic remains wrap-safe.

## Verification

Host tests must prove that 226 ms and 499 ms releases are short clicks, 500 ms
starts voice, a second press that crosses 500 ms cancels the pending click, and
all Home/wake/suppression paths remain unchanged. The full host suite, firmware
contracts, reproducible build, direct flash, boot validation, and one live
single-click Agent sample form the release evidence. Normal flashing does not
create a firmware backup.

