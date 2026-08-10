# Running Count Lifecycle Design

## Problem

The Codex alert snapshot currently limits every non-pending state to records created on the current local day. A task that remains running across midnight is therefore removed from the Running count even though its lifecycle has not ended.

## Design

Date filtering applies only to `completed`, because that count represents today's completed work. The active lifecycle states `pending` and `running` remain visible until a later lifecycle event replaces them for the same thread.

The ledger format, retention period, watch payload, and state transition reducer remain unchanged. The fix is a single snapshot predicate change.

## Correctness Checks

- A running task survives local midnight.
- A later abort or normal completion still replaces that running task.
- Pending continues to survive midnight.
- Completed continues to disappear from the next day's snapshot.
- Latest-per-thread selection and seven-day ledger pruning remain unchanged.

## Self-review

The design has no placeholders or new state semantics. It fixes the observed lifecycle/date coupling without expanding scope or adding special cases.
