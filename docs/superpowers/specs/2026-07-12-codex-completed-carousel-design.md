# Codex Completed Carousel Design

## Goal

Make the watch activity state distinguish actionable Codex progress from
historical handled progress. Handled records must not keep animating after the
user has dealt with them.

## Display Behavior

- When one or more alerts are pending, the activity heading remains red and
  reads `PENDING X/N`.
- Rotation includes pending alerts only. Handled alerts are skipped even when
  both states are present in the synchronized payload.
- When no alerts are pending, rotation stops. The most recent handled alert
  remains visible as a static project and outcome summary.
- The all-clear heading reads `COMPLETED` in green. The previous
  `ALL HANDLED · N` and `HANDLED X/N` headings are removed.
- With no alert records at all, the existing `NO ALERTS` empty state remains.

## Implementation Boundary

The change is confined to the firmware-side `CodexAlertCarousel`. The Mac
agent continues sending the same pending and handled records, counts, and
revision. No BLE protocol or persistent-ledger migration is required.

## Verification

- A mixed list rotates only through pending entries.
- An all-handled list remains on one entry and reports `COMPLETED`.
- The completed heading uses the existing handled-green color path.
- Pending heading text, order, and red color remain unchanged.
