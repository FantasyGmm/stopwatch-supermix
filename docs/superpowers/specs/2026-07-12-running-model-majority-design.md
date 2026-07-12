# Running Model Majority Design

## Goal

Display the model and reasoning effort that best represent the conversations
currently being processed, rather than copying the newest global Codex log
entry from one conversation.

## Selection Rule

Each conversation whose latest StopWatch state is Running contributes one vote
for its actual `(model, reasoning_effort)` pair. The pair is read by joining the
Running conversation ID to the `threads` row in `~/.codex/state_5.sqlite`.

The displayed pair is selected as follows:

1. Count complete, non-empty `(model, effort)` pairs across Running
   conversations.
2. Select the pair with the largest count.
3. When multiple pairs have the same count, select the pair used by the most
   recently started Running conversation among those tied pairs.
4. Never calculate model and effort independently, because that could produce
   a combination no Running conversation actually uses.

Rows missing either model or effort do not vote. A database read failure does
not replace a previously valid result.

## Persistence And Fallback

Whenever a valid winner changes, the Mac agent atomically persists the selected
model and effort to a small local JSON cache under `~/.codex`.

- When no conversation is Running, continue displaying the persisted winner.
- When the state database is temporarily unavailable, continue displaying the
  persisted winner.
- After restarting the menu agent, restore the persisted winner before the
  first status refresh.
- On a first installation with no persisted winner and no valid Running pair,
  use the existing live-log/config result as a one-time display fallback.

The cache contains only model and effort identifiers. It contains no prompt,
response, project, or conversation content.

## Components

The selection logic belongs in `CodexStateCollector`, after the alert tracker
has produced the latest per-conversation snapshot. `CodexPaths` gains testable
paths for the Codex state database and the selection cache. A pure helper ranks
candidate pairs using count first and latest Running timestamp second.

The existing `codex_model` and `codex_effort` output fields remain unchanged.
The Mac menu and watch firmware therefore require no protocol or rendering
changes.

## Refresh Behavior

The collector already refreshes every two seconds. Each refresh uses the same
Running snapshot sent to the menu and watch, queries only those thread IDs, and
updates the persisted selection only when the winner changes. Changes normally
reach the watch within one collector interval.

## Verification

- Three votes for `gpt-5.6-sol/xhigh` beat one vote for
  `gpt-5.6-sol/medium`.
- A one-to-one tie selects the pair from the most recently started Running
  conversation.
- Model and effort remain a real pair from one or more Running conversations.
- Completed and Pending conversations do not vote.
- Missing model/effort rows do not vote.
- No Running conversations retain the previous persisted result.
- Restart restores the previous result from disk.
- Missing or corrupt cache and database files fall back without breaking status
  collection.
- Existing quota, alert, BLE payload, and firmware behavior remain unchanged.
