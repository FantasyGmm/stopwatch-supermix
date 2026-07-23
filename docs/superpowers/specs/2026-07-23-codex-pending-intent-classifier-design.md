# Codex Pending Intent Classifier

## Problem

The Mac agent currently classifies a completed Codex turn from only the final
paragraph of `task_complete.last_agent_message`. A short phrase list and
question-mark check miss valid handoffs such as:

- a rendered A/B/C design choice;
- a completed build that still requires device verification;
- a written specification waiting for approval;
- a blocked task waiting for authorization, information, or hardware.

Codex app-server reports these turns as ordinary `completed`/`idle`; it does not
provide a universal `awaiting_user` state.

## Decision

Use a local, deterministic, layered classifier. Structured rollout evidence has
priority, text structure is a fallback, and legacy phrases remain a final
compatibility fallback. Do not call an online or local LLM.

This favors recall: an ambiguous unresolved handoff may become Pending, because
missing a required user action is more costly than an occasional extra alert.

## Evidence Sources

### 1. Structured turn evidence

The tracker reads `response_item` records in addition to `event_msg` records.
For each active turn it persists:

- the latest `update_plan` statuses;
- unresolved `request_user_input` calls;
- the Codex turn ID.

At `task_complete`, either of these is sufficient for Pending:

- a `request_user_input` call has no matching output;
- the latest plan still contains `in_progress` or `pending` items.

`task_started` replaces stale evidence for that thread. Completed turns remove
their transient evidence. Persisting it in the ledger makes classification
survive a menu-agent restart during a Codex turn.

### 2. General text structure

After removing trailing Codex directives and memory-citation blocks, inspect the
whole final response rather than only its final sentence:

- any unresolved direct question in the final response;
- a homogeneous option group with at least two labels such as A/B/C or 1/2/3;
- an explicit required user action or blocking handoff.

Historical or quoted questions followed by a clear completion remain Completed.
Legacy Chinese and English waiting phrases stay as a low-priority fallback.

### 3. Explainable result

Classification returns both outcome and reason:

- `structured_unfinished_plan`
- `structured_input_request`
- `text_question`
- `text_choice`
- `text_required_action`
- `completion_only`

The reason is stored in the local ledger and logged. It is not sent to the
watch, so the BLE payload and firmware remain unchanged.

## State Transitions

- `task_started` -> Running and initializes turn evidence.
- `task_complete` + waiting evidence -> Pending, unnotified.
- `task_complete` without waiting evidence -> Completed.
- later `user_message` -> resolves earlier Pending in that conversation.
- the new `task_started` for that message remains Running.

## Migration

Bump the ledger schema from version 2 to version 3 and add an empty
`turn_signals` object. Existing alerts keep their state. Historical completed
turns are not globally replayed, avoiding duplicate old alerts. The currently
reported task can be repaired only when it is still awaiting input; if the user
has already replied, its new Running state remains authoritative.

## Validation

Add tests for:

- the real A/B/C design response with unfinished plan evidence;
- an unfinished plan surviving tracker restart;
- a later fully completed plan overriding earlier unfinished plan evidence;
- unresolved and resolved `request_user_input` calls;
- option groups without plan events;
- quoted/historical questions that must remain Completed;
- existing Pending resolution and notification behavior.

Run the focused tracker tests, all Mac-agent tests, install the signed menu app,
and verify the installed source hash and live ledger state.
