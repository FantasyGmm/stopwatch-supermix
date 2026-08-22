# Codex Dual Status and Task Stream — Design Specification

**Status:** Approved for implementation
**Approved browser reference:** `task-browser-non-card-options.html`, option A, in the 2026-08-22 brainstorming companion
**Target:** M5Stack StopWatch, 466 × 466 px circular AMOLED, dark mode only

## Product outcome

The Codex home page replaces its abstract single status word with two compact,
human-readable status channels directly below the Codex mark: Mac connection
and voice. The task browser replaces the one-task focus page with a card-free
three-item text stream whose center item is the active focus.

This specification supersedes only the visual and browsing-density portions of
`2026-08-11-codex-watch-task-focus-deck-design.md`. Secure routing, task
ordering, detail content, voice behavior, entry/exit gestures, and lifecycle
rules remain unchanged.

## Codex home status

- Preserve the approved pure-black background and existing Codex mark position.
- Place two independent status groups below the mark without overlapping it:
  - connection group: a 6 px semantic dot plus a short Chinese label;
  - voice group: a 6 px semantic dot plus a short Chinese label.
- Connection states:
  - live: mint filled dot, `Mac 已连接`;
  - connecting/pairing/waiting: violet filled dot, `连接中`;
  - disconnected/unavailable: gray hollow dot, `未连接`;
  - transport or pairing error: vermilion filled dot, `连接异常`.
- Voice states:
  - ready: cyan filled dot, `语音可用`;
  - starting: violet filled dot, `语音连接中`;
  - active: cyan filled dot, `语音中`;
  - off/unavailable: gray hollow dot, `语音不可用`;
  - error: vermilion filled dot, `语音异常`.
- A connection error does not hide voice state, and a voice error does not hide
  connection state.

## Task browser

- Use no cards, rounded task containers, surface gradients, shadows, or tinted
  row backgrounds. The two 1 px row separators are the sole gradient use:
  each fades from transparent to `#27303A` across the outer 18%, stays solid
  for the center 64%, then fades back to transparent across the final 18%.
- Show up to three task rows simultaneously: previous, focused, next.
- The focused row sits in the center and uses the largest title type, full
  opacity, a short violet left rule, and the existing right-side position rail.
- Previous and next rows remain readable at reduced opacity. Missing neighbors
  at list boundaries are simply absent; the list never wraps.
- Every visible row shows project, title, semantic status dot and label, and the
  task time string. The focused title may use two lines; neighboring titles use
  a single ellipsized line.
- Chinese titles must use native raster sizes: `CodexTaskTitle21`, generated
  from Hiragino Sans GB W6 at 21 px, for the focused row and
  `font_puhui_14_1` at 14 px for neighboring rows. Do not scale smaller CJK
  bitmaps up. Focused Latin titles reuse the Codex model-value typeface,
  `MontserratSemiBold26`; neighboring Latin titles use Montserrat 16 px.
- Center the `任务` header on the 233 px screen axis. The focused row is
  373 × 107 px at `(47, 168)`, with 18 px left and 28 px right content insets.
- In the focused row, place the project at local `y = 3` in an 18 px text box
  and the task title at local `y = 23`, preserving a visible 2 px gap instead
  of allowing the two labels to overlap.
- A one-line focused title places metadata at local y=55; a two-line title
  places it at y=82. Status position follows the rendered line count instead
  of always reserving two lines.
- Keep the right-side position nodes as one compact group centered at `y = 215`:
  inactive nodes are `4 × 4` px, the focused capsule is `8 × 18` px, and the
  fixed edge-to-edge gap is 6 px. Never stretch a short task list to fill the
  full rail height. Inactive nodes are neutral `#394452` circles regardless of
  task state; only the focused capsule uses `#829DFF`.
- The task-browser palette follows the approved browser reference exactly:
  title `#F3F6F8`, project `#9DB2FF`, status/time `#8995A2`, count `#8C98A5`,
  bottom hint `#687482`, handle `#778491`, pending dot `#FF6248`, and running
  dot `#49C9ED`. These task-browser tokens do not replace the Codex home or
  task-detail semantic colors.
- Pending tasks remain ordered before running tasks. Crossing the boundary keeps
  the existing haptic tick.
- A vertical drag moves the text stream with the finger after axis lock. Release
  advances at most one task using the existing threshold. At a list boundary,
  the stream uses bounded resistance instead of exposing empty space.
- Tapping the focused row keeps the existing behavior: select its route and open
  the authenticated task detail.

## Pull handle correction

- A 48 × 4 px etched-gray handle at `(209, 12)` belongs to the task deck layer,
  not only the task-detail layer.
- During the bottom-center upward entry gesture, the handle is the first visible
  element as the task layer crosses the bottom edge.
- The handle remains visible when the deck is fully open and does not inherit
  disconnected-content opacity.
- The visible handle owns a centered 100 × 48 px invisible touch target. Only
  a downward drag that begins in this target may move and dismiss the whole
  deck. A drag that begins in task content only browses tasks and never closes
  the deck, including at the first-item boundary.
- Dismissal completes only when the handle drag is released at the physical
  bottom row (`y = 465`). Releasing even one pixel earlier returns the deck to
  its fully open position; ordinary swipe-distance thresholds never dismiss it.
- The existing detail handle remains visible and unchanged.

## Task-detail voice continuity

- A right-button hold in task detail continues to target the selected task.
- If that exact route changes from Running to Completed while its detail is
  still visible, recording may start and the final submission opens the next
  turn on the same thread. The Agent revalidates the route and thread again at
  submission time; missing, collided, or changed routes still fail closed.

## Fixed geometry

- viewport: `466 × 466`;
- deck handle: `(209, 12, 48, 4)`;
- title: centered at `y = 49`; position count at the upper-right safe chord;
- previous row: `x = 79`, `y = 84`, `w = 308`, `h = 70`;
- focused row: `x = 47`, `y = 168`, `w = 349`, `h = 101`;
- next row: `x = 70`, `y = 286`, `w = 326`, `h = 70`;
- separators: `y = 164` and `y = 278`, fading before the circular edge;
- hint: centered in the bottom safe chord at `y = 418`;
- minimum focused hit target: the entire `349 × 101` focused row.

All task text, status, time, handle, and hit targets must remain inside a circle
of radius 221 px centered at `(233, 233)`.

## State matrix

| State | Visible rows | Focus treatment | Interaction |
|---|---|---|---|
| no tasks | centered empty copy | none | no detail tap |
| first task | focus + next | center rule | content drag browses/resists; only handle drag closes deck |
| middle task | previous + focus + next | center rule | continuous vertical drag |
| last task | previous + focus | center rule | upward drag has bounded resistance |
| disconnected | cached rows at 60% opacity | handle stays full opacity | voice disabled |
| mixed pending/running | ordered text stream | semantic dots | boundary haptic preserved |

## Handle dismissal contract

- Only a vertical drag that begins inside the handle's `100 × 48` touch target
  may dismiss the task deck. Content drags remain browse-only.
- The deck follows the finger until its vertical offset reaches `326 px`, about
  70% of the `466 px` close travel.
- Reaching the threshold immediately latches and animates the deck closed. The
  implementation must not wait for release, and the same touch must be consumed
  until release so it cannot open another surface underneath.
- Releasing below `326 px` returns the deck to the open position. Once latched,
  releasing or moving back upward cannot reopen it.
- The handle owns this behavior at every focused task index; closing does not
  require the first task to be focused.

## Fidelity gate

Host tests must prove neighbor selection, boundary behavior, round-safe geometry,
dual-channel mappings, native CJK title scale, and deck-handle ownership. Reuse
the existing full-coverage CJK assets; the application capacity gate leaves no
room for another full-size task font. A successful build proves only compilation.
Visual completion requires flashing the device and comparing the
home status, first/middle/last task positions, disconnected state, upward entry
handle, and detail view against the approved browser reference.

## Self-review record

- **Spec coverage:** dual status, every connection/voice state, three-row task
  density, continuous drag, boundary resistance, selection, handle ownership,
  disconnected behavior, and physical verification are explicit.
- **Circular safety:** all fixed rectangles were checked against the 221 px safe
  radius; no critical content is placed in the edge crescents.
- **Typography check:** the photographed fuzzy title was traced to a 16 px CJK
  font scaled to roughly 27 px; the approved implementation uses native raster
  sizes and introduces no duplicate full CJK font asset.
- **Behavior preservation:** route selection, encrypted detail, PTT routing,
  pending/running ordering, haptics, and page ownership are unchanged. The
  handle-only close now intentionally auto-latches at 70% travel, while task
  content remains browse-only and the triggering touch is consumed through
  release.
- **Ambiguity check:** the approved choice is option A. The later physical
  comparison locked the option-A palette, compact neutral position nodes, and
  separator edge fades explicitly; no remaining visual decision changes
  implementation behavior.
