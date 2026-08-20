# Fixed Microphone · Option A Design Contract

## Source of truth

- Authoritative reference: `reference.html`
- Target viewport: 466 × 466 px at device scale
- Target hardware: M5Stack StopWatch round AMOLED
- Older pinned-microphone mockups and the prior centered-beacon design are
  non-authoritative.

## Purpose

The page starts and stops the continuous watch-to-Mac external microphone.
The microphone itself is the only control. The page must acknowledge a tap
immediately even when recorder initialization is still running.

## Layout

- Background: instrument black `#020304`.
- Title: `固定麦克风`, centered at y=48, dedicated 24 px Chinese font, signal
  white `#F4F7FB`.
- Connection row: centered at y=84, 6 px state dot plus `已连接到 Mac` in 14 px
  Chinese text.
- Main control: centered 194 × 194 px circular touch target at x=136, y=126.
- Microphone: recognizable 74 × 118 px capsule, U-shaped pickup frame, stem,
  and base. The icon is geometric LVGL artwork, not a font glyph.
- Prompt: centered at y=333, 22 px Chinese text.
- Shared footer: preserve the same three 8 px page dots at y=416 and the same
  battery readout used by the Codex and TRAE pages. The pinned microphone page
  changes only the central content area; it does not define a custom footer.

## State matrix

| State | Microphone | Main control | Prompt | Motion |
| --- | --- | --- | --- | --- |
| Ready/off | Etched gray | Dark circular well | `轻触开启` | None |
| Starting | Violet outline | Violet-to-cyan progress ring | `正在启动音频…` | Ring rotates continuously |
| Active | Running cyan | Cyan edge and subtle glow | `轻触关闭` | Two cyan sound waves expand from the control |
| Unavailable | Etched gray | Disabled dark well | `等待 Mac 连接` | None |
| Error | Vermilion | Vermilion edge | `启动失败 · 轻触重试` | None |

Motion is feedback only. With reduced motion, Starting uses a static split-color
ring and Active uses two fixed sound-wave arcs.

## Interaction and lifecycle

- Tapping the 194 × 194 px microphone target is the only toggle action.
- A valid enable tap publishes Starting before recorder initialization begins.
- Recorder initialization runs off the LVGL/UI thread.
- Duplicate taps while Starting are ignored.
- Tapping Active requests the existing graceful stop.
- Home, app close, disconnect, credential/epoch change, unusable voice link,
  and controller error cancel a pending or active session exactly once.
- Swiping to another Codex page does not stop an active pinned microphone.

## Accepted deviations

- The browser reference uses CSS/SVG for visual review. Firmware recreates the
  same geometry with LVGL primitives.
- Active sound waves may use stepped opacity rather than browser alpha easing
  if LVGL animation cost is materially lower; positions, size, color, and
  cadence remain the same.
- Battery text in the browser reference is a representative charged state;
  firmware uses the shared live battery value, icon, and color policy.

## Verification

- Host tests cover immediate Starting publication, deferred recorder open,
  duplicate-start suppression, transition to Active, failure, and cancellation.
- Firmware compiles against the real ESP32-S3/LVGL toolchain.
- Physical acceptance requires photos of Ready, Starting, Active, Unavailable,
  and Error on the AMOLED at the same page position as `reference.html`.
