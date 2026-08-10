# Turn-Aborted Running State Design

## Problem

The watch can show one more Running task than Codex. A rollout records
`task_started`, followed by `turn_aborted`, but `TaskAlertTracker` currently
accepts only `task_started`, `task_complete`, and `user_message`. The Running
alert therefore remains the latest state for that conversation indefinitely.

The live failure was deterministic: Codex reported three active tasks while
the tracker reported four. The extra conversation had two matching
`turn_aborted` events and no later `task_complete`.

## Considered Approaches

1. **Handle the lifecycle event and migrate stale Running alerts (selected).**
   Consume `turn_aborted` in the existing ordered event reducer. On upgrade,
   stream only rollouts whose latest alert is still Running and repair a
   matching historical abort. This uses the authoritative local event source
   and preserves exact turn ordering.
2. **Reconcile against Codex UI status.** This would couple the local agent to
   an app-only status API that is not present in the state database and may be
   unavailable while the app is closed.
3. **Expire Running alerts after a timeout.** This would hide genuinely long
   turns and merely replace the lifecycle bug with a timing heuristic.

## Event Reduction

New Running alerts persist their non-empty `turn_id`. When a `turn_aborted`
event arrives, the tracker requires the event turn ID to match the current
structured turn signal. It retires only the newest Running alert for that
conversation and turn, updating it to:

- `state = completed`
- `outcome = completed`
- `reason = turn_aborted`
- `progress_at` and `handled_at` equal to the abort timestamp
- `watch_notified = true`

The matching structured turn signal is then removed. An abort for an older
turn cannot clear a newer Running turn. Aborts never create Pending state and
never request a chime.

For legacy Running alerts without a stored `turn_id`, the event may use the
newest Running alert only while the active structured signal still matches the
abort turn ID. This keeps the live reducer compatible with existing ledgers.

## One-Time Repair

The ledger gains an independent abort-reducer version. New ledgers start at
the current version. When an older ledger is loaded, the tracker identifies
only the latest alert per conversation that is still Running. For each such
conversation it streams the rollout, finds the `task_started` event at the
alert timestamp, and looks for a later `turn_aborted` with the same turn ID.

A match applies the same completed transition as live reduction. No match
leaves a genuinely active turn unchanged. The version advances after the
bounded set of Running conversations is inspected, so normal refreshes do not
replay old files repeatedly.

## Failure Handling

Unreadable or incomplete rollouts leave the existing Running alert unchanged
and the conversation queued for retry. Malformed complete records do not
change the alert and are not retried automatically. Existing cursor positions
are never rewound, so historical alerts are not recreated.

## Tests

The integration suite must prove:

1. `task_started -> turn_aborted` produces zero Running and zero Pending.
2. The completed alert records `turn_aborted` and never requests a watch
   notification.
3. A late abort for an older turn cannot clear a newer Running turn.
4. Restarting after a processed abort is revision-idempotent.
5. An old ledger whose cursor is already past the abort repairs the stale
   Running alert once.
6. An incomplete rollout remains Running and is retried without corrupting the
   ledger.

## Scope

This change touches only Codex task lifecycle reduction, replay migration, and
their tests. It does not infer activity from timestamps, change Pending
classification, change BLE payload fields, or reconcile against the Codex UI.
