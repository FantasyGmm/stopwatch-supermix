# Codex and Trae Unified Status Pages Design

## Goal

Extend the existing StopWatch Codex status application with a second Trae status
page. Codex remains the default page. A horizontal swipe from right to left
opens Trae; the reverse swipe returns to Codex.

Both sources are collected by the existing macOS menu application and delivered
over its existing BLE or Wi-Fi connection. The feature must not install a Trae
extension, start a second Mac process, create a second watch connection, or
weaken the existing Codex status and alert behavior.

## Reviewed Decision

The best fit is one Mac client with two isolated collectors and one shared
transport.

- `CodexStateCollector` remains the Codex source.
- A new `TraeStateCollector` reads Trae's local state and incremental logs.
- A composite collector merges the two source snapshots for the menu runtime.
- The existing transport sends separate Codex and Trae status messages over the
  same connection.
- The watch routes each message to the matching page model.

This is preferable to a separate Trae client because two processes would
duplicate menu controls, Bluetooth permissions, retry state, logging, and
connection ownership. It is preferable to a Trae extension because a local
extension adds installation and upgrade coupling while still relying on private
Trae state for model, effort, and agent lifecycle.

The design review changed the initial proposal in three important ways:

1. Codex and Trae are not serialized into one large JSON document. Separate
   messages prevent Trae fields from displacing Codex alerts inside the
   2,048-byte reassembly limit.
2. Codex keeps its existing two-second transmission. Trae transmits when its
   normalized snapshot changes and at a six-second heartbeat, reducing BLE
   writes while staying below the watch's ten-second stale threshold.
3. Page swipes explicitly participate in the existing global edge-control
   arbitration. A vertical gesture can still own brightness or volume; a clear
   horizontal gesture remains available to page navigation.

## Scope

### Included

- Trae model and effort.
- Trae process and source freshness.
- Current Trae project.
- Trae task state, title, and one-line content summary.
- Multiple Trae tasks using the same priority and rotation semantics as Codex.
- Codex/Trae page dots and the battery below them on both pages.
- One unified menu application and one shared watch connection.
- Conservative missing, stale, malformed, and oversized-data behavior.

### Excluded

- Trae token quota or progress bars.
- A Trae extension or modification to the Trae application.
- Sending prompts, responses, source code, tool output, request headers, or raw
  Trae log lines to the watch.
- Renaming the existing application bundle, bundle identifier, BLE service,
  advertised device name, or settings namespace.
- Trae completion chimes.
- Button-based page switching.
- More than two status pages.

## User Experience

### Page Order and Navigation

Codex is page zero and Trae is page one.

- Opening the application always starts on Codex.
- A right-to-left swipe on Codex moves to Trae.
- A left-to-right swipe on Trae returns to Codex.
- Swiping past either end does nothing.
- The completed transition uses a 140 ms ease-out animation.
- A cancelled or mostly vertical movement leaves the current page unchanged.
- The existing two-button Home chord closes the application from either page.
- The current long left-button screen-off behavior remains unchanged.

The page indicators are:

- Codex: `● ○`
- Trae: `○ ●`

The indicator is centered at `y=416`. The battery remains centered at `y=438`,
below the indicator and inside the circular safe area. This ordering applies to
both pages.

### Swipe Contract

The page gesture uses the same release-based shape already proven by Launcher:

- a fast horizontal swipe is at least 36 px within 260 ms;
- a drag swipe is at least 100 px;
- horizontal distance must be greater than vertical distance;
- the direction is decided only on release;
- no transition begins while `GlobalEdgeControlService` owns the touch;
- a consumed global edge-control release clears the page candidate;
- screen-off state clears the page candidate and suppresses navigation.

Global edge controls already cancel their candidate after horizontal movement
exceeds 24 px and dominates vertical movement. Page navigation therefore waits
for that cancellation instead of competing with brightness or volume.

### Trae Page Layout

The approved direction is **C · Split Lower Panel**.

The Trae page keeps the current Codex visual language:

- pure black background;
- centered Trae brand and a small connection/freshness label;
- violet signal mark and vertical beacon spine on the upper left;
- `MODEL` and `EFFORT` telemetry on the upper right;
- etched divider;
- violet, white, gray, and state-color hierarchy;
- page indicator and battery at the bottom.

No font glyph is assumed for the Trae mark. The view draws a small outlined
rounded-square signal mark with LVGL objects, avoiding a new font or imported
proprietary asset.

Coordinates use the 466 × 466 screen:

- brand and source status reuse the current `kBrandY` and `kStatusY`;
- upper signal mark occupies the current pet region;
- model and effort reuse the existing telemetry coordinates;
- lower divider: `x=82, y=223, w=302`;
- lower content bounds: `x=69, y=246, w=328, h=118`;
- lower vertical divider: `x=210, y=246, w=1, h=98`;
- left status/project column: `x=69, w=126`;
- right task column: `x=228, w=169`;
- page indicator: centered at `y=416`;
- battery: centered at `y=438`.

The left column answers "where and in what state":

- localized task state;
- project name.

The right column answers "what is being done":

- task title;
- at most two visual lines of sanitized task content.

Long project names, titles, and summaries use ellipsis. Missing values render as
`--`; they are never guessed.

### Visible States

Trae task states use the existing watch vocabulary:

- `pending` → `等待指令`
- `running` → `正在处理`
- `completed` → `已完成`

When Trae is running with no tracked task, the lower state is `空闲`. When the
Trae process is not running, the source label is `TRAE OFFLINE` and the lower
content retains the last valid snapshot in dimmed colors. When the process is
running but local data is temporarily unavailable, the source label is
`TRAE WAIT`; the view does not convert that condition into a task state.

Pending tasks have priority. With no pending task, running tasks have priority.
With neither, the newest completed task remains visible. Tasks in the selected
state rotate every three seconds, matching the existing Codex carousel
principle.

## Mac Client Architecture

### Component Boundaries

#### `TraePaths`

Defines all filesystem dependencies:

- `~/Library/Application Support/Trae CN/logs`
- `~/Library/Application Support/Trae CN/User/workspaceStorage`
- `~/Library/Application Support/Trae CN/ModularData/ai-agent/sandbox`
- `~/.trae-cn/.stopwatch_tasks.json`
- `~/.trae-cn/.stopwatch_model_selection.json`

Paths are injectable in tests.

The opaque `ModularData/ai-agent/database.db` is not a dependency. Current
inspection shows it is not a normal SQLite database, so relying on it would be
both brittle and unnecessary.

#### `TraeTaskTracker`

Owns incremental Trae lifecycle tracking:

- baselines the newest active AI-agent log at EOF on first run;
- persists file identity, offset, normalized task records, and revision;
- resumes after menu-app restart;
- handles truncation and log rotation;
- reads only appended bytes;
- recognizes task start, progress/tool activity, waiting, completion, and
  cancellation events;
- correlates session/task identifiers without persisting raw log lines;
- stores only normalized state, project, title, summary, and timestamps.

It must never write raw prompts, responses, tool output, HTTP bodies, request
headers, device identifiers, or source code into its ledger or application log.

#### `TraeStateCollector`

Builds a compact source snapshot.

- Process liveness is obtained through an injectable probe for bundle identifier
  `cn.trae.app`.
- The current session model is read from Trae's workspace state relation maps,
  with actual running-session log metadata taking precedence.
- Effort/mode is read from the matching session relation state. Unknown modes
  remain `--`.
- Project is resolved from session-correlated sandbox metadata. A matching
  workspace's `workspace.json` is the fallback.
- Task state, title, and summary come from `TraeTaskTracker`.
- The last valid model/effort pair is persisted and reused only while Trae is
  running.

The collector does not call Trae network endpoints and does not modify Trae
state.

#### `CompositeStateCollector`

Owns the Codex and Trae collectors and returns one in-process dictionary.
Collection failures are isolated per source:

- a Codex failure preserves the last Codex snapshot and still refreshes Trae;
- a Trae failure preserves the last Trae snapshot and still refreshes Codex;
- the exception log identifies the source but contains no raw source data.

`AgentRuntime` continues to sample while watch connection is disabled.

#### Menu Snapshot and Presentation

`AgentSnapshot` gains an immutable `TraeSnapshot`. The menu gains a `Trae
Status` section containing model, effort, task state, project, and task title.
The existing connection controls, device name, logs, and quit behavior remain
singletons.

The application remains `StopWatch Codex Agent` with bundle identifier
`com.stopwatch.codex-agent`. Keeping the identity avoids a second Bluetooth
permission prompt and preserves existing installation behavior.

## Transport and Payload Contract

The BLE service, characteristic, framing header, message types, 20-byte safe
write size, burst yielding, and 2,048-byte reassembly cap remain unchanged.

### Codex Packet

The current Codex status JSON remains backward-compatible. It may add
`"source":"codex"`, but its existing `codex_*` fields and alert trimming rules
do not change.

### Trae Packet

Trae uses the same status message type with an explicit source:

```json
{
  "type": "status",
  "source": "trae",
  "trae_online": true,
  "trae_model": "openrouter-3o",
  "trae_effort": "high",
  "trae_revision": 12,
  "trae_tasks": [
    {
      "project": "webshot",
      "title": "Trae status tracking",
      "detail": "Keep model and effort and add page switching",
      "time": "00:49",
      "state": "running"
    }
  ]
}
```

The watch packet limits are:

- four Trae tasks;
- project: 32 Unicode code points;
- title: 48 Unicode code points;
- detail: 72 Unicode code points;
- final serialized Trae message: at most 1,024 bytes.

If the byte budget is exceeded, tasks are removed from the end first, then the
detail is shortened. Model, effort, online state, revision, and the first task's
state are retained. UTF-8 strings are cut only at code-point boundaries.

Codex sends every two seconds as it does today. Trae sends immediately after
connection, whenever the normalized Trae fingerprint changes, and otherwise
every six seconds as a heartbeat. Writes are awaited serially, so frames from
the two messages cannot interleave.

Time synchronization remains one event after connection and before either
status source.

## Watch Architecture

### Message Routing

The app checks the top-level `source` before parsing:

- missing or `codex` routes to the existing Codex parser for compatibility;
- `trae` routes to a new Trae parser;
- any other source is rejected without altering either snapshot.

Codex and Trae maintain independent last-received timestamps. A valid Trae
packet cannot refresh Codex freshness, and vice versa.

### Page Composition

`AppCodexStatus` owns a clipped 466 × 466 viewport and a 932 × 466 horizontal
page strip:

- Codex root at `x=0`;
- Trae root at `x=466`;
- strip position `0` or `-466`.

Both roots are created once on open and destroyed once on close. Page switches
move the strip; they do not rebuild widgets or reset the Codex alert carousel,
pet animation, or pending glow.

A new `TraeStatusPage` owns only Trae LVGL widgets and rendering. A pure
`StatusPageSwipeLogic` owns press/move/release state and direction decisions.
The application coordinates these units with transport, screen-off behavior,
and global touch ownership.

The physical pending glow continues to follow Codex pending state even while the
Trae page is visible.

## Error Handling and Compatibility

- Existing firmware continues to accept old Codex-only clients.
- A new client connected to old firmware still sends the unchanged Codex packet;
  the old firmware rejects or ignores the separate Trae packet without losing
  Codex updates.
- Malformed Trae JSON does not clear the last valid Trae snapshot.
- A Trae state database lock or schema mismatch yields `TRAE WAIT` and last-good
  values, not an invented model or task.
- Log rotation, truncation, oversized lines, partial trailing lines, and invalid
  UTF-8 are recoverable.
- Trae app exit produces `trae_online=false` promptly; idle operation does not
  become offline merely because no log line was appended.
- BLE disconnect/reconnect sends time sync, Codex, and Trae in that order.
- Horizontal page navigation never consumes a vertical brightness, volume, or
  status-bar gesture.
- No new NVS settings are required. The current page is intentionally not
  persisted and always resets to Codex on open.

## Verification

### Automated

- Trae state parsing from sanitized local-state fixtures.
- Model/effort actual-session precedence and last-good fallback.
- Incremental log cursor, first-run baseline, restart recovery, truncation, and
  rotation.
- Task state transitions and pending/running/completed priority.
- Raw-line and sensitive-field redaction.
- Composite collector failure isolation.
- Immutable menu snapshot and presentation for both sources.
- Separate Codex/Trae messages, byte caps, change-driven sending, heartbeat, and
  reconnect order.
- Trae JSON parser validation and independent freshness.
- Trae carousel behavior.
- Swipe thresholds, direction, endpoint clamping, and global touch cancellation.
- Exact C-layout geometry, page-dot order, and battery placement.
- Existing Python suite, all host C++ tests, clean unified firmware build,
  capacity gates, and `git diff --check`.

### Manual Mac Validation

- Launch the existing menu app with Trae closed, idle, running a task, waiting
  for input, and completed.
- Verify model, effort, project, task title, and sanitized summary against Trae.
- Verify the Mac menu keeps updating with watch connection disabled.
- Restart the menu app during a task and confirm cursor recovery without replay.
- Rotate/restart Trae and confirm the adapter follows the new log.
- Confirm application logs contain no raw prompt, response, tool output, request
  header, device identifier, or source code.

### Manual Watch Validation

- Open Codex and confirm the current page remains visually and behaviorally
  correct.
- Swipe right-to-left to Trae and left-to-right back to Codex.
- Confirm `● ○` and `○ ●`, with battery below the dots on both pages.
- Verify vertical left/right gestures still change brightness/volume without
  switching pages.
- Verify diagonal and short swipes do not switch pages.
- Verify Trae state changes, long-string ellipsis, task rotation, idle, wait,
  offline, reconnect, and stale behavior.
- Verify Codex pending glow and Home/screen-off controls remain unchanged on
  either page.

Build success, automated tests, Mac runtime validation, and physical watch
validation are separate evidence gates.
