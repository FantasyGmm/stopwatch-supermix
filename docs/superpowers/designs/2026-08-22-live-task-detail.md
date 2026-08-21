# Live Task Detail Design

## Goal

Make the Codex task detail panel visibly draggable and keep the selected task's
detail current while its Codex turn is still running.

## Approved UI

Use option A from the browser comparison: a short, centered horizontal handle
that reuses the Agent panel's existing size, color, opacity, and rounded shape.
The handle belongs to the task-detail panel root, so it follows the panel during
the upward drag and remains visible when the panel settles open.

For a running task, replace `Agent 最后一条完整回复` with `Agent 实时进展` and
show the existing running cyan as a small status dot. Pending and completed
tasks retain the completed-reply label without the live dot.

## Live Data Flow

The Codex rollout already emits complete `event_msg/agent_message` records
during a turn. The task alert reducer will accept those records only for the
currently active turn and copy the latest non-empty message into that running
alert's `last_reply`. This updates the alert revision through the existing
state snapshot.

The existing Agent publisher, route registry, encrypted task-detail framing,
fingerprint cache, nonce handling, and watch-side detail store remain the only
transport path. No new BLE UUID or watch-to-Agent focus protocol is added.
Each active route is sent only when its bounded text fingerprint changes; the
watch renders the detail for the currently selected route.

Running updates contain only the latest complete Agent message, rather than an
ever-growing transcript. The normal Agent status loop coalesces writes, so
intermediate partial text is not sent. The final `task_complete` record still
replaces the running preview with the full final reply under the existing
12-KiB detail bound.

## Safety and Failure Behavior

- Ignore malformed, empty, non-agent, stale-turn, and pre-threshold records.
- Do not expose reasoning, tool payloads, user messages, or partial JSONL
  records.
- Preserve the existing encrypted/authenticated task-detail channel and
  at-most-once publisher behavior.
- If the Agent disconnects, the watch continues to show its last verified
  cached detail and connection state.
- A live revision replaces the body at the top; the latest message is a new
  bounded unit rather than an appended transcript, so stale scroll positions
  are intentionally cleared.

## Verification

- Reducer tests prove a running `agent_message` updates only the matching turn
  and increments revision, while malformed/stale records do not.
- Agent tests prove changed live text emits new encrypted detail chunks and an
  unchanged message does not resend.
- Firmware host/contract tests prove the Agent-matching handle exists, follows
  the detail panel, and live/completed labels render in the correct states.
- Browser comparison is the visual source of truth; after flashing, the
  physical AMOLED must be checked for handle position, round-screen clipping,
  label legibility, and the live dot.
