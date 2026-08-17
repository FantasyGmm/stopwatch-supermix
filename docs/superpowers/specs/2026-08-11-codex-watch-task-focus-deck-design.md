# Codex Watch Task Focus Deck — Design Specification

**Status:** Approved for implementation
**Authoritative concept:** original “E · 焦点轮播” in `/Users/luke/.codex/visualizations/2026/07/29/019fae12-604c-7c80-a90a-3af247283570/watch-task-voice-flow.html`
**Target:** M5Stack StopWatch, 466 × 466 px round display, dark mode only

## Product outcome

The Codex watch app gains a bottom-up task focus deck. It lets the user identify an ongoing Codex task by project and task name, open the task’s last complete Agent reply as unmodified original text, then hold the right hardware button to dictate the next instruction directly to that task.

Task selection is persistent and immediate. A pending task receives a new turn. A running task receives a steer operation against the exact active turn that was displayed; it must never silently fall back to starting a new turn.

## Authoritative interaction

### Entry and exit

- A vertical swipe beginning in the bottom-center safe zone opens the task focus deck from the Codex page.
- The gesture is ignored while the top Agent panel, global edge controls, screen protection, or the Trae page owns touch.
- A downward swipe from the first deck item closes the deck and returns to the Codex status page.
- The existing horizontal Codex/Trae paging remains unchanged. Once a gesture is classified as vertical deck navigation, horizontal paging is cancelled for that gesture.

### Focus deck

- Exactly one task occupies the 466 × 466 view.
- The project name is centered near the top in violet.
- The task name is the dominant element, centered, up to three lines, with no numeric prefix and no detail preview.
- A compact semantic status treatment sits below the title:
  - pending: warm vermilion solid pill, “等待你的指令”;
  - running: cyan outline pill, “正在执行”.
- The right inner edge contains the distinctive rotary task track from the approved E concept:
  - pending tasks use diamonds;
  - running tasks use circles;
  - the focused task uses a short capsule;
  - the track carries no numbers.
- Up/down swipes or a vertical drag over the rotary track move one task at a time. The order is all pending tasks, then all running tasks. The list does not wrap.
- Crossing the pending/running boundary produces one short haptic tick.
- Browsing alone does not change the persistent voice target.
- Tapping the focused task immediately makes it the persistent target and opens task detail.

### Task detail

- The project, task title, and status remain centered at the top.
- The body displays the latest complete, stable Agent reply verbatim. It does not display streaming partial output and does not summarize or rewrite the reply.
- The reply scrolls vertically. UTF-8 content is preserved at character boundaries.
- If the Agent has not completed a reply yet, the body shows “暂无完整回复”.
- A footer identifies the selected routing behavior:
  - pending: “按住右键 · 发送下一步”;
  - running: “按住右键 · 引导当前任务”.
- A downward swipe from the top of the detail returns to the deck without clearing the persistent selection.

### Voice submission

- Pressing and holding the right hardware button on detail starts the existing Typeless audio path.
- The authenticated PTT begin frame includes the selected opaque route ID, binding the whole recording to the selected task at press time.
- Releasing the button ends capture, waits for the newly completed Typeless transcript, and submits the transcript through Codex App Server.
- Desktop composer visibility is not a requirement. The transcript is not logged.
- Pending routes call `turn/start`.
- Running routes call `turn/steer` with the expected active turn ID.
- Missing selection, missing transcript, stale route, changed running turn, or App Server rejection fails closed and surfaces an error; none may send to the currently focused desktop composer as a fallback.

## Data and transport contract

### Opaque routes

The Mac Agent derives a stable 64-bit route ID from the Codex thread ID and exposes only the route ID to the watch. The Agent keeps the authoritative mapping:

`route ID → thread ID, state, expected turn ID, detail revision`

Route collisions invalidate both colliding routes. Raw thread IDs and turn IDs are never sent to the watch.

### Task list

The existing status JSON keeps at most eight pending/running tasks. Each entry contains:

- `route`: 16 lowercase hexadecimal characters;
- `project`: at most 32 Unicode characters;
- `title`: at most 48 Unicode characters;
- `state`: `pending` or `running`.

Pending precedes running. Completed tasks are not shown in the focus deck.

### Original reply synchronization

Original replies are sent as separate `task_detail` status messages so a long reply cannot evict task-list entries from the 2048-byte BLE message budget. Each message contains route ID, monotonically increasing detail revision, chunk index/count, and a UTF-8-safe text chunk. The watch reassembles chunks only when all chunks for one revision arrive.

The desktop retains and transmits up to 12 KiB per last reply. Content within that bound is byte-for-byte original UTF-8. If a reply exceeds the hardware bound, the watch receives the leading 12 KiB at a Unicode boundary plus an explicit truncation marker; this is truncation, never summarization.

The watch keeps detail text only for current task routes and discards data for routes removed by a newer task-list revision.

### Voice protocol compatibility

Production voice framing advances one protocol version because the authenticated PTT begin body grows from the codec tuple to the codec tuple plus the 64-bit route ID. Control, audio, end-summary, replay protection, key epoch validation, and encryption domains remain unchanged.

## Persistent selection rules

- Tapping a task stores its route ID as the selection.
- Status refresh preserves the selection while that route remains pending or running.
- If the selected route disappears, selection falls back to the first pending task, otherwise the first running task, otherwise no selection.
- Browsing position follows the selected route after refresh when possible; it otherwise remains on the nearest valid index.
- Recording keeps the route captured at PTT begin even if a status refresh changes the visible item before release.

## Visual contract

### Tokens

| Token | Value | Use |
|---|---:|---|
| Instrument black | `#000000` | full-screen background |
| Graphite | `#27303E` | track, dividers, inactive geometry |
| Beacon violet | `#7C9DFF` | project label and focus accent |
| Running cyan | `#49C9EE` | running status and nodes |
| Signal white | `#F2F6F8` | task title and original reply |
| Etched gray | `#788391` | hints and inactive nodes |
| Pending vermilion | `#FF6248` | pending status and nodes |

No gradients, drop shadows, glass effects, bouncy motion, decorative cards, or oversized generic rounded containers are allowed.

### Geometry and type

Values are inferred from the approved HTML prototype and are fixed for the first implementation pass:

- content safe width: 336 px, centered 18 px left of physical center to reserve the rotary track;
- project label top: 72 px; 18 px semibold Latin or 16 px CJK fallback;
- task title center: y 188–258 px; 32 px semibold Latin or 26 px CJK fallback; maximum three lines;
- status pill center: y 305 px; height 30 px; horizontal radius 15 px;
- detail body: x 54 px, y 145 px, width 338 px, height 222 px; 16 px CJK-capable font, 1.45 line-height equivalent;
- rotary track: center approximately (421, 234), radius 132 px, visible inner arc only; nodes are positioned on the right arc;
- all tappable task/detail regions meet a 44 × 44 px minimum target.

### State matrix

| State | Deck | Detail | Voice action |
|---|---|---|---|
| no tasks | centered “暂无进行中的任务”; empty track | unavailable | disabled |
| one pending | vermilion capsule node | original reply or empty copy | `turn/start` |
| many pending | diamond nodes, no wrap | selected pending detail | `turn/start` |
| one running | cyan capsule node | last stable reply | exact `turn/steer` |
| pending + running | pending ordered first; boundary haptic | semantic footer | state-specific |
| long project/title | ellipsis project; title wraps to 3 lines | header ellipsizes | unchanged |
| long reply | scrollable chunked original text | explicit truncation marker only past 12 KiB | unchanged |
| stale route/turn | selection retained visually until refresh | error state after submit | fail closed |
| disconnected | cached deck may remain; muted connection hint | cached detail | disabled |
| recording | selected task fixed; track inactive | recording indicator replaces footer hint | bound route |

## Preview and fidelity verification

- The approved HTML’s E state is the source reference; other variants in the visualization directory are non-authoritative.
- A host-renderable LVGL preview fixture with deterministic pending/running/long-text data is required. It must not initialize BLE, microphone, NVS, Wi-Fi, accessibility, or Typeless.
- Required captured states at 466 × 466: pending deck, running deck, mixed-list boundary, pending detail with long CJK reply, empty state, and recording state.
- The implementation is not visually complete until those captures have been compared region-by-region against the original E reference. Native LVGL font rasterization and the round-display crop are acceptable deviations; layout hierarchy, rotary identity, colors, and interaction states are not.

## Non-goals

- Showing live streaming Agent output.
- Editing the transcript on the watch.
- Displaying completed tasks in the deck.
- Mirroring or depending on the desktop Codex composer.
- Replacing the existing Agent selection panel or Codex/Trae horizontal paging.
- Adding release-build voice diagnostics or unrelated telemetry.

## Self-review record

- Spec coverage: entry, ordering, original reply, persistent selection, pending/start, running/steer, Typeless handoff, security binding, empty/error states, and visual verification are all explicit.
- Conflict check: bottom-center vertical gestures are isolated from top Agent panel and side edge controls; horizontal page swipe is preserved.
- Safety check: routing is authenticated at PTT begin and all stale/missing cases fail closed.
- Capacity check: the task list remains within the existing message budget; reply data is chunked and bounded independently.
- Ambiguity check: no product choice remains that changes behavior materially. The 12 KiB device bound is an implementation constraint and is represented honestly in the UI.
