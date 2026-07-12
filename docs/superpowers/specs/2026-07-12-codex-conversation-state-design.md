# Codex Conversation State Design

## Goal

Represent the latest actionable state of each Codex conversation instead of
treating every finished turn as an unhandled alert. The watch must prioritize
conversations waiting for the user, then show active work, and use a static
completed state only when neither category exists.

## Current Problem

The tracker currently turns every `task_complete` event into Pending. A normal
successful response therefore remains Pending until the user writes another
message, even when Codex did not ask for input. The tracker ignores the explicit
`task_started` event, so active work cannot be displayed as Running.

## Conversation State Machine

Each conversation exposes only its latest state for display, while the ledger
may retain historical records for recovery and auditing.

### Running

- `task_started` creates or replaces the conversation's current live record as
  Running.
- Codex emits `task_started` before the corresponding `user_message` in current
  rollouts. Processing that `user_message` acknowledges older Pending records
  but must not clear the new Running record.

### Pending

- There is one `task_complete` event type, not separate waiting and completed
  event types.
- The existing local classifier inspects `last_agent_message` from
  `task_complete`.
- A direct question, confirmation request, information request, or required
  user action classifies as waiting and transitions Running to Pending.
- Only a transition to Pending triggers a watch chime.

### Completed

- A `task_complete` response that does not require another user reply
  transitions Running to Completed.
- A later `user_message` in the same conversation acknowledges older Pending
  records by transitioning them to Completed.
- Completed records are not actionable and never participate in rotation.

Malformed or incomplete rollout lines retain the last valid state. If a
`task_complete` appears without a matching Running record, the tracker creates
the resulting Pending or Completed record so recovery after downtime remains
lossless.

## Display Priority

The synchronized snapshot contains counts and bounded records for Pending,
Running, and Completed. Records are ordered by priority and then newest first.

1. When Pending exists, select the newest Pending record and rotate through all
   available Pending records only. The heading is red `PENDING X/N`.
2. When no Pending exists but Running exists, select the newest Running record
   and rotate through all available Running records. The heading is blue
   `RUNNING X/N`.
3. When neither Pending nor Running exists, stop rotation and show green
   `COMPLETED` with the most recent completed conversation detail.
4. When no current-day conversation record exists, retain `NO ALERTS`.

Watch detail labels are:

- Pending: `等待指令 · <short conversation title>`
- Running: `正在处理 · <short conversation title>`
- Completed: `已完成 · <short conversation title>`

## Mac Agent

The menu's task section gains Running alongside Pending and Completed. It uses
the same latest-state-per-conversation snapshot as the watch, while the JSON
ledger remains the persistent source for restart recovery. Pending notification
IDs include Pending records only.

## Compatibility And Migration

Existing ledger rows with `state=handled` migrate to Completed. Existing
Pending rows retain Pending until a later user message acknowledges them. The
BLE payload adds `codex_alert_running` and accepts `state=running` and
`state=completed`; firmware and Mac agent are updated together before flashing.

## Verification

- `task_started -> user_message` remains Running.
- `task_started -> task_complete(waiting)` becomes Pending and chimes once.
- `task_started -> task_complete(completed)` becomes Completed without a chime.
- A user reply converts only older Pending records in that conversation to
  Completed and does not affect another conversation.
- Multiple Pending conversations rotate exclusively among Pending.
- With no Pending, multiple Running conversations rotate exclusively among
  Running.
- With neither Pending nor Running, the watch remains on green `COMPLETED`.
- Restart, partial JSONL lines, short-title synchronization, BLE size limits,
  and launcher-only flashing retain their existing guarantees.
