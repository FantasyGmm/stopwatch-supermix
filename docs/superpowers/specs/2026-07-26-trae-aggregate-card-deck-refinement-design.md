# TRAE Aggregate Status Page Refinement Design

## Status

Approved visual and interaction direction: **C · aggregate card deck**.

This document refines the existing Codex/Trae two-page design. It replaces the
Trae page's signal rings and split lower panel with an aggregate active-task
view, but does not change the existing Codex page or the shared transport
architecture.

The product name is written as `TRAE` in the interface and in new code-facing
names. Earlier references to `Tree` or `TARE` refer to the same product and are
not separate states or entities.

## Goal

Make the watch answer three questions at a glance:

1. Is the Mac still connected to the watch?
2. Is the TRAE process alive?
3. Across every TRAE project, which tasks need confirmation and which are
   still running?

The page must preserve the visual rhythm of the Codex page while supporting
multiple simultaneous tasks. Completed tasks are deliberately absent so that
the small screen contains only actionable work.

## Scope

### Included

- The existing right-to-left swipe from Codex to TRAE.
- A native, optically aligned TRAE product icon beside the title.
- Separate Bluetooth-link and TRAE-process health states.
- Pending and running counts aggregated across all projects.
- A compact overlapping card deck for active tasks.
- Pending-first automatic rotation and manual browsing of all active tasks.
- The model and effort for the task currently represented by the front card.
- A small full-body TRAE robot animation derived from the approved source
  artwork.
- Empty, disconnected, unavailable, stale, and truncated-data behavior.
- Existing page dots and battery placement.

### Excluded

- Token usage, quota percentage, or a progress bar.
- Completed-task counts, cards, or history.
- Starting, stopping, or controlling TRAE from the watch.
- Approving a task from the watch.
- Showing prompts, source code, raw logs, or long task output.
- Replacing the existing Codex page, BLE service, or Mac application identity.

## Screen Structure

The page remains a 466 x 466 circular canvas with the same typography, safe
area, spacing rhythm, and black background as Codex.

From top to bottom:

1. TRAE product icon and uppercase `TRAE` title.
2. One compact health line: `BT <state> · TRAE <state>`.
3. Full-body animated TRAE robot on the left; `MODEL` and `EFFORT` on the
   right.
4. Divider.
5. Aggregate counts for `待确认` and `运行中`.
6. Overlapping active-task card deck.
7. Existing two-page indicator.
8. Existing battery indicator below the page dots.

The robot and telemetry form one visual group. The robot must be close enough
to `MODEL` and `EFFORT` to read as the same section, without colliding with the
title or card deck. It must occupy approximately the same proportion of the
screen as the Codex robot, not dominate the page.

## Header and Product Icon

The product icon is a native image resource derived from the approved TRAE
robot head. It is not drawn by stretching a screenshot and is not an overlay on
top of unrelated artwork.

The header follows the Codex geometry:

- icon slot starts at approximately `x=168` and renders at approximately
  32 x 32;
- title starts at approximately `x=218`, `y=32`;
- the nominal horizontal gap is 18 px;
- title text is exactly `TRAE`.

Alignment is judged by visible pixels, not merely by the image object's outer
box. Transparent padding must be trimmed or compensated so that the icon's
visible center is within 1 px of the title's visible vertical center. The
approved prototype measured a 0.5 px visible-center difference.

The header icon is static. Animation belongs only to the larger robot in the
content area.

## Connection and Process Health

Bluetooth-link health and TRAE-process health are independent facts. They are
shown as two inline items so they do not displace the telemetry section.

| Bluetooth link | Process observation | Display |
| --- | --- | --- |
| Connected | Alive | `BT LIVE · TRAE LIVE` |
| Connected | Not alive | `BT LIVE · TRAE OFF` |
| Connected | Probe unavailable | `BT LIVE · TRAE --` |
| Disconnected | Not observable | `BT OFF · TRAE --` |

Color treatment:

- Bluetooth `LIVE` and its dot use cyan.
- TRAE `LIVE` and its dot use green.
- `OFF` and `--` use muted gray.
- Color is supportive only; the text always carries the state.

When Bluetooth is disconnected, the watch must not claim that TRAE is off.
`TRAE --` means the remote process cannot currently be observed.

## Task Aggregation

### Included Tasks

The Mac client normalizes tasks from every observed TRAE project. The watch
receives only active tasks:

- `pending`: the task is waiting for user confirmation or instruction;
- `running`: the task is actively working.

Completed, cancelled, and failed tasks do not contribute to the aggregate
counts and do not appear in the deck. A task that becomes terminal disappears
on the next accepted snapshot.

### Aggregate Counts

The summary always shows both counts:

- `待确认 N`
- `运行中 N`

Counts describe the complete normalized active set on the Mac, even when the
wire payload has room for only a subset of detail cards. Counts are therefore
separate payload fields rather than inferred from the transmitted array.

### Stable Ordering

The detail-card order is deterministic:

1. all pending tasks;
2. all running tasks;
3. within a state, newest meaningful activity first;
4. ties break by stable task identifier.

This avoids cards jumping unpredictably between snapshots.

## Card Deck Interaction

The deck shows one readable front card and restrained portions of cards behind
it. Stack depth indicates that more active tasks exist; it is not proportional
to the total task count after the visual depth limit is reached.

The front card contains:

- localized state label;
- project name;
- task title;
- one short sanitized detail line when space permits;
- elapsed or latest-activity time when available.

Project, title, and detail are ellipsized. Missing data renders as `--`; it is
never guessed.

### Automatic Rotation

Automatic rotation always stays in the highest-priority non-empty queue:

- if pending tasks exist, rotate only pending cards;
- otherwise, rotate running cards;
- if the selected queue contains one card, do not animate a fake rotation.

The default interval is three seconds, matching the existing carousel rhythm.
Running details never automatically replace a pending card while confirmation
is needed.

### Manual Browsing

Tapping the card deck advances through all transmitted active cards in stable
deck order. This makes running work discoverable even while pending work has
automatic priority.

After five seconds without another tap, the deck returns to the current
highest-priority automatic queue and resumes rotation. A tap must not trigger a
page swipe; a recognized horizontal swipe must not advance the deck.

When a new snapshot removes the displayed task, the deck selects the first card
of the highest-priority queue. When the displayed task still exists, its stable
identifier is retained to avoid an unnecessary jump.

## Model and Effort

`MODEL` and `EFFORT` remain visible because they are meaningful even without a
token percentage.

- When the front task has task-scoped model and effort, show those values.
- Otherwise, fall back to the snapshot-level TRAE model and effort.
- If neither source is known, show `--`.
- When manual or automatic rotation changes the front task, telemetry changes
  with it.

The payload and task model therefore allow optional per-task `model` and
`effort` fields while retaining the existing snapshot-level fields for
compatibility.

## Robot Resource and Animation

The content robot is based directly on the approved pixel robot source:

`codex-clipboard-7040d413-96b5-4d77-b9d9-36a830462a59.png`

Production assets are converted to native image resources at exact target
dimensions. Scaling uses nearest-neighbor sampling or a pre-scaled source so
edges remain crisp and proportions do not distort.

Two authored frames are sufficient:

- idle: original full-body robot pose and diamond eyes;
- action: left eye becomes `>` and the left arm lifts, while the remaining
  silhouette stays faithful to the source.

The animation briefly switches to the action frame and back. It may run on a
low-frequency idle cadence and when task state changes, but it must not flash
continuously. Pending state may use the existing soft-glow treatment behind the
robot; the glow is not baked into the image asset.

Animation pauses while the TRAE page is not visible and when the screen is
off. The static title icon never animates.

## Data Contract

The Trae status packet retains the existing source-specific status message and
adds aggregate and per-task identity fields. A representative normalized
payload is:

```json
{
  "type": "status",
  "source": "trae",
  "trae_online": true,
  "trae_model": "Doubao",
  "trae_effort": "High",
  "trae_revision": 24,
  "trae_pending_count": 2,
  "trae_running_count": 3,
  "trae_tasks_truncated": false,
  "trae_tasks": [
    {
      "id": "stable-task-id",
      "project": "StopWatch",
      "title": "Review firmware status page",
      "detail": "Waiting for confirmation",
      "time": "00:49",
      "state": "pending",
      "model": "Doubao",
      "effort": "High"
    }
  ]
}
```

Contract rules:

- `trae_online` is the Mac-side process probe result, not Bluetooth state.
- Bluetooth state remains local watch transport state.
- aggregate counts cover the full active set before detail trimming;
- detail cards are trimmed only when required by the fixed payload budget;
- pending detail cards are retained before running detail cards;
- each transmitted task has a stable opaque identifier;
- raw prompt text, source code, headers, tool output, and logs are excluded;
- unknown optional values are empty or omitted and render as `--`;
- older payloads without count fields fall back to counts derived from their
  task array, so the refinement remains backward-compatible.

The current firmware capacity of four task details remains the initial hard
limit unless measured packet size and memory tests justify increasing it. The
counts still report every active task. When trimming occurs, the UI may show a
compact `+N` overflow mark, but must not synthesize missing cards.

## Component Responsibilities

### Mac Client

- `TraeTaskTracker` maintains normalized task lifecycle and stable identifiers
  across projects.
- `TraeStateCollector` probes process liveness, gathers task-scoped telemetry
  where available, and computes full active counts before trimming.
- `AgentRuntime` continues sending changed snapshots plus heartbeat messages
  through the existing shared connection.
- The menu snapshot may expose the same two counts for diagnosis, but this
  refinement does not add task-control actions.

### Firmware Model

- `TraeStatusSnapshot` gains full pending/running counts and optional task
  identity/model/effort fields.
- Parsing remains bounded and rejects malformed state values without
  invalidating the last good snapshot.
- The model distinguishes remote process liveness from transport freshness.

### Carousel Controller

- Owns deterministic pending-first ordering.
- Separates automatic queue rotation from manual all-card browsing.
- Retains the current task by stable identifier across revisions.
- Exposes current card and aggregate counts without embedding LVGL behavior.

### View

- Owns rendering, optical alignment, native image frames, soft glow, deck
  offsets, ellipsis, and localized labels.
- Reads Bluetooth state from the existing connection source and process state
  from the Trae snapshot.
- Does not infer lifecycle state from colors or animation.

## Freshness, Empty, and Error Behavior

### No Active Tasks

When both counts are zero, show `待确认 0`, `运行中 0`, and a single neutral
empty card reading `暂无进行中的任务`. Model and effort may retain current
snapshot-level values if the TRAE process is alive.

### Bluetooth Disconnected

Show `BT OFF · TRAE --`. Dim task data as last-known information; do not mutate
task counts or process state merely because transport was lost. After the
existing stale timeout, replace the task deck with `等待连接` while preserving
only deliberately labeled last-known telemetry if the current Codex behavior
already supports it.

### Connected but TRAE Off

Show `BT LIVE · TRAE OFF`, zero active counts, and an empty card reading
`TRAE 未运行`. Old tasks must not remain actionable.

### Connected but Probe Unknown

Show `BT LIVE · TRAE --` and a neutral `状态不可用` card. Do not reinterpret an
unavailable probe as an offline process.

### Malformed or Stale Payload

The firmware keeps the last valid bounded snapshot when a packet is malformed.
Transport freshness and process liveness remain separate: a stale task payload
does not prove that the process is off. Once the normal stale threshold is
crossed, the UI uses the appropriate unavailable presentation instead of
showing old cards as current.

## Navigation and Shared Footer

- Codex remains page zero and the startup page.
- A right-to-left swipe from Codex opens TRAE.
- A left-to-right swipe from TRAE returns to Codex.
- Existing horizontal/vertical gesture arbitration remains unchanged.
- Card taps are accepted only below the horizontal-swipe threshold.
- The page dots remain above the battery.
- On TRAE the second dot is active; on Codex the first dot is active.
- Battery rendering and placement are shared and unchanged.

## Verification Strategy

### Mac Unit Tests

- Aggregate counts include all active tasks across multiple projects.
- Completed, failed, and cancelled tasks are excluded.
- Pending details are retained before running details when trimming.
- Stable identifiers and deterministic ordering survive snapshot refreshes.
- Per-task model/effort and snapshot fallback are normalized correctly.
- Process alive, off, and unknown remain distinct.
- Sensitive raw content never enters the normalized payload.

### Firmware Unit and Contract Tests

- New and legacy payloads parse within fixed bounds.
- Full aggregate counts are independent from transmitted detail count.
- Automatic rotation stays within pending while pending exists.
- Without pending, automatic rotation covers running cards.
- Manual browsing reaches running cards while pending exists.
- Manual timeout returns to the priority queue.
- Current-card identity survives reorder and resets safely after removal.
- Terminal tasks disappear on revision update.
- Empty, disconnected, process-off, unknown, malformed, stale, and truncated
  states render the specified copy.

### Visual and Hardware Checks

- Compare the TRAE header against Codex at native 466 x 466 resolution.
- Confirm visible-pixel centers of icon and title differ by no more than 1 px.
- Confirm the robot matches the approved proportions and remains crisp.
- Confirm animation pauses off-page and on screen-off.
- Confirm the deck is legible with one, two, and four transmitted cards.
- Confirm counts remain correct when more than four active tasks exist.
- Confirm tap browsing does not interfere with horizontal page swipes.
- Confirm the footer order is page dots, then battery.
- Flash the built firmware and verify swipe, tap, animation, BLE transitions,
  process transitions, and multi-task rotation on the physical watch.

Build success, automated tests, and successful flashing are separate gates;
none alone proves the physical interaction or animation.

## Acceptance Criteria

The refinement is complete when:

1. Swiping right-to-left from Codex opens the approved TRAE C layout.
2. The header reads `TRAE`, and its icon is optically aligned like Codex.
3. `BT` and `TRAE` health independently reflect transport and process truth.
4. Pending and running counts aggregate every active task across projects.
5. Completed tasks are absent from counts and cards.
6. Pending tasks own automatic rotation; manual tapping can still reach every
   transmitted running task.
7. The front card controls task-scoped model and effort with a safe fallback.
8. The approved full-body robot animates without distortion or excessive size.
9. Page dots remain above the battery.
10. Unit, contract, build, and physical-watch checks pass and are reported as
    distinct evidence.
