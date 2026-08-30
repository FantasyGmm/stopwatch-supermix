# Codex Pinned Microphone — Direction D

**Status:** approved on 2026-08-30

**Supersedes:** the pinned-microphone visual treatment in `2026-08-13-codex-pinned-microphone-design.md`

**Visual source of truth:** browser prototype Direction D, locked by the measurements below.

## Intent

The pinned-microphone page should look like a precise audio instrument rather
than a generic switch. It keeps the established three-page navigation and
session behavior, while replacing the crude microphone drawing with a clean
geometric microphone and a restrained, real-input meter.

## Locked 466 × 466 layout

- Background: pure black `#000000`; no card, panel, halo disc, or full-screen
  gradient.
- Title: `固定麦克风`, centered at y=48, `PinnedMicTitle24`, signal white.
- Connection row: 6 px status dot plus `已连接到 Mac`, visually centered at
  y=84. Existing connected/disconnected wording remains authoritative.
- Interaction target: one transparent 232 × 194 target centered at x=117,
  y=122. It covers the meter, microphone, and surrounding whitespace.
- Microphone artwork: one exact 96 × 138 A8 mask at x=185, y=145,
  generated from the approved Direction D SVG path. The mask contains the
  46 × 76 capsule, three grille strokes, yoke, stem, and base. Do not rebuild
  the artwork from separate LVGL rectangle/arc primitives; their stroke and
  arc rasterization visibly changes the approved silhouette on the AMOLED.
- External meter: four 6 px-wide segments on each side, mirrored about x=233.
  Left centers are x=123, 137, 151, 165; right centers are x=343, 329, 315,
  301. Segment heights are derived from the four newest real PCM level
  samples and remain vertically centered on y=214.
- Prompt: centered at y=333, `PinnedMicState22`.
- Existing bottom page indicator geometry remains unchanged.

All visible content stays within the circular safe area. Nothing approaches
the extreme left/right edge below y=122 or above y=306.

## State matrix

| State | Icon and meter | Prompt | Motion |
|---|---|---|---|
| Ready | etched gray microphone; meter hidden | `轻触开启`, white | none |
| Starting | beacon violet microphone; meter hidden | `正在启动音频…`, violet | no fake level motion |
| Active | running cyan | `收音中 · 轻触关闭`, quota mint | mirrored real PCM meter only |
| Stopping | etched gray microphone; meter hidden | `正在关闭…`, etched gray | none |
| Unavailable | etched gray microphone; meter hidden | existing unavailable/disconnected wording | none |
| Error | alert vermilion microphone; meter hidden | `语音异常 · 轻触重试`, vermilion | none |

The meter must not animate from a timer or a generated waveform. It is visible
only while Active and reads the existing bounded 15-sample history from
`StopWatchAudioBackend`. In every inactive state all eight bars are hidden;
there is no quiet/static triangle beside the microphone.

## Behavior and lifecycle

- Tapping anywhere in the central target preserves the current pinned-mode
  controller contract.
- Page navigation, display-off survival, Home, app close, disconnect, epoch
  changes, and voice-error cleanup remain unchanged.
- The page reads audio levels only; it does not own capture, start a task, or
  allocate an unbounded buffer.
- Level rendering is bounded to eight small LVGL objects and four recent
  samples per UI pass.
- No internal jumping vertical bars are permitted.

## Fidelity acceptance

Source and host tests can lock the exact A8 asset geometry, state mapping,
meter visibility, and level math, but do not establish visual completion.
Final acceptance requires the flashed AMOLED
to be checked in at least Ready and Active states for centering, clipping,
stroke weight, color, legibility, and real input response.

## Self-review

- The approved D hierarchy and geometry are explicit rather than inferred from
  the earlier alternatives.
- The exact SVG-derived microphone asset prevents primitive-rendering drift.
- Static capsule grille lines are distinguished from the rejected animated
  vertical bars, and the external meter is absent outside Active.
- Every controller state has a defined color, prompt, and motion policy.
- The dynamic effect has a real data source and bounded runtime cost.
- Physical AMOLED verification is retained as a required final gate.
