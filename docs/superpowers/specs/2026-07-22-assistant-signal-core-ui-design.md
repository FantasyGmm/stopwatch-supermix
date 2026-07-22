# Assistant Signal Core UI Design

**Date:** 2026-07-22
**Status:** Approved visual direction, pending implementation plan
**Branch:** `codex/assistant-ui-signal-core`

## Objective

Replace the current text-only Assistant screen with the approved A2 "Signal Core" interface. The new screen must make voice state obvious at a glance, show the current recognized or spoken sentence without a redundant "live subtitle" label, and remain safe inside the StopWatch round display.

This is a presentation and event-routing change. It must not change Assistant activation, wake-word gating, network ownership, audio ownership, graceful shutdown, update installation behavior, or the two-button home gesture.

## Approved Direction

The interface uses one full-screen black surface with three fixed visual zones:

1. a compact status row for connectivity, update metadata, and battery;
2. a nine-bar waveform as the primary voice-state signal;
3. a title and at most two lines of current conversation text.

The words "实时字幕" are not displayed. Recognized user text and Assistant text appear directly below the divider.

The approved primary states are:

- waiting for the local wake word;
- listening;
- thinking;
- speaking.

Connecting, activation, failure, closing, and close-timeout states use the same structure rather than falling back to the old plain-text screen.

## Display Geometry

The LVGL root remains a fixed `466 x 466` non-scrollable panel centered on the physical display.

All critical content stays inside these conservative bounds:

- top status row: `y = 30..55`, horizontal inset at least `52 px`;
- phase label: centered near `y = 68`;
- waveform: centered within `y = 88..220`;
- primary title: centered near `y = 228`;
- divider: `x = 60..406`, near `y = 276`;
- conversation text: `x = 48..418`, `y = 294..354`;
- hardware hint: centered near `y = 408`, at least `42 px` above the panel edge.

No text, scrolling ticker, or control may be placed against the circular edge. Dynamic text is limited to two centered lines. Overflow uses LVGL ellipsis or clipping at the view boundary; it never scrolls horizontally.

## Visual System

### Color

- background: `#05070A`;
- primary text: `#F7FBFF`;
- secondary text: `#C7D1DD`;
- inactive text and bars: `#637185` / `#293641`;
- connected and listening: cyan `#2BC8FF`;
- speaking: teal/green `#3BD6C7` / `#61EDAA`;
- thinking: indigo `#697CFF`;
- warning/update: amber `#F1C85A`;
- failure: coral red `#FF6673`.

Colors are semantic and state-specific. The screen does not use gradients, image backgrounds, glow bitmaps, or decorative cards.

### Typography

Use only fonts already included in the firmware:

- Montserrat for numbers, versions, and English fallback labels;
- `font_puhui_14_1` for Chinese status and conversation content.

No new full CJK font is added. Fixed headings may use English when the existing Chinese font cannot provide the required emphasis without increasing Flash usage. Letter spacing remains zero.

### Waveform

The waveform consists of nine rounded LVGL rectangles. It is rendered from native objects, not a GIF, bitmap sequence, FFT, or new audio-analysis pipeline.

`AssistantView::update(now_ms)` advances a small deterministic frame table:

- waiting: static, low gray bars;
- listening: cyan/teal bars, approximately `100-120 ms` per frame;
- thinking: compact indigo inward/outward pulse, approximately `150-180 ms` per frame;
- speaking: teal/green bars, approximately `100-120 ms` per frame;
- connecting/activating/closing: slow low-amplitude pulse;
- failed/timeout: static low red bars.

The app loop drives animation. The view does not create a task, FreeRTOS timer, LVGL timer, or audio callback, which keeps shutdown ownership unchanged.

## State Presentation

### Connecting

- status: `正在连接`;
- waveform: low cyan-gray pulse;
- title: `连接网络`;
- content: `正在连接 Wi-Fi...`;
- Wi-Fi indicator: gray;
- footer: `双键长按返回`.

### Activating

- status: `正在激活`;
- waveform: low cyan pulse;
- title: `小智助手`;
- content: normal activation message or activation code;
- Wi-Fi indicator: cyan.

### Waiting For Wake

- entered when the runtime publishes `Running` after the wake-gate transition;
- status: `等待唤醒`;
- waveform: static gray;
- title: `你好，小智`;
- content: `说“你好小智”开始对话`;
- no Assistant audio channel is implied by this visual state.

### Listening

- entered only from the real `ListeningStatusChanged(true)` callback;
- status: `正在聆听`;
- waveform: active cyan/teal;
- title: `我在听`;
- content: the latest user recognition text when available, otherwise `请说`.

### Thinking

- entered when recognized user text is received;
- status: `正在思考`;
- waveform: compact indigo pulse;
- title: `请稍候`;
- content: the latest user text.

### Speaking

- entered from Assistant text or `SpeakingStatusChanged(true)`;
- status: `正在播报`;
- waveform: active teal/green;
- title: `小智助手`;
- content: the latest Assistant text, without a subtitle label.

When `SpeakingStatusChanged(false)` is received, the screen returns to Waiting For Wake and clears stale conversation text. A late inactive-listening event must not overwrite Thinking or Speaking.

### Failure

- status: `暂不可用`;
- waveform: static red;
- title: `助手不可用`;
- content: the concrete runtime error;
- footer: `右键重试  双键返回` only when retry is available.

### Closing

- status: `正在关闭`;
- waveform: collapsing gray pulse;
- title: `正在返回`;
- content: `等待助手释放资源...`;
- footer hidden;
- conversation events remain ignored after close is requested.

### Close Timeout

- status: `关闭超时`;
- waveform: static red;
- title: `资源仍受保护`;
- content: the runtime timeout detail;
- footer: `右键再次停止`.

## Status Row

The top row is informational and non-interactive.

- Left: a small status dot and `在线` after Wi-Fi setup reaches activation; gray `连接中` before that.
- Center: compact amber `vX.Y.Z 可用` only when the existing metadata-only update notice is present. It does not install firmware and does not become a button.
- Right: a compact battery outline with the integer percentage inside. Charging may use the existing lightning treatment.

Battery is refreshed at most once per second from `GetHAL().getBatteryLevel()`. The view must not poll hardware directly; `AppAssistant` reads telemetry outside the LVGL lock and applies it inside one short lock.

## Event Routing

The existing runtime callbacks already receive real listening and speaking changes but currently use them only for trace records. The implementation forwards those changes to the app mailbox so the UI does not infer microphone or speaker state from elapsed time.

Required presentation rules:

1. `Running` maps to Waiting For Wake, not Listening.
2. `ListeningStatusChanged(true)` maps to Listening.
3. User text maps to Thinking and replaces the conversation line.
4. Assistant text maps to Speaking and replaces the conversation line.
5. `SpeakingStatusChanged(false)` maps back to Waiting For Wake.
6. Inactive listening events do not demote Thinking or Speaking.
7. Closing, failure, and timeout states override conversation events.
8. Events from stale sessions continue to be discarded by the existing mailbox/session boundary.

The event extension must remain typed and bounded. It must not add a new queue, worker, callback allocation loop, or direct LVGL call from the runtime callback thread.

## Component Boundaries

### `AssistantView`

Owns LVGL objects, fixed layout, colors, bar frame application, text clipping, and destruction. It exposes state, text, battery, connectivity, update-version, and animation-update methods. It does not access Wi-Fi, battery hardware, runtime services, or audio samples.

### `AppAssistant`

Owns presentation state reduction, periodic battery sampling, runtime event mapping, retry behavior, and LVGL locking. It keeps the existing two-button home path and graceful close controller unchanged.

### `AssistantRuntime`

Continues to own Assistant lifecycle and service events. Its only UI-related addition is forwarding listening/speaking status through the mailbox after the existing session/source validation.

## Resource Budget

- no new bitmap or GIF assets;
- no new full font;
- no new task or timer;
- no audio FFT or amplitude-copy path;
- approximately nine waveform objects plus the status/text/battery objects;
- deterministic frame tables stored as small constants;
- no unbounded text history and no transcript list.

The firmware build must remain below the existing application capacity gate. The implementation report records the before/after binary delta.

## Error And Lifecycle Safety

- View teardown remains synchronous under the LVGL lock.
- `update()` becomes a no-op after `destroy()`.
- Runtime callbacks only publish mailbox events and never retain LVGL pointers.
- Closing still suppresses all conversation events.
- A failed start keeps the actionable failure screen visible after the subsequent `Stopped` event.
- A close timeout keeps resources protected and preserves the existing retry path.
- The update notice remains metadata-only and survives visual state changes.

## Validation

### Host And Contract Tests

- `Running` renders Waiting For Wake.
- Real listening and speaking callbacks are forwarded through the mailbox.
- inactive listening cannot overwrite Thinking/Speaking.
- speaking completion returns to Waiting For Wake.
- update metadata remains non-interactive and visible across state changes.
- long Chinese and ASCII messages remain inside the fixed text region.
- close-request event suppression and retry behavior remain unchanged.
- view animation does not allocate a timer or task.
- no new image/font asset is introduced.

### Build Validation

- full existing Python test suite;
- relevant C++ host tests;
- ESP-IDF unified firmware build;
- application and asset capacity gates;
- before/after binary size comparison.

### Hardware Validation

After the user explicitly enters download mode and the candidate is flashed:

- opening Assistant shows Connecting without clipped top or bottom text;
- Running shows Waiting For Wake and remains silent;
- saying the wake word changes to Listening;
- recognized text changes to Thinking and remains legible;
- Assistant text and audio change to Speaking;
- speaking completion returns to Waiting For Wake;
- update, Wi-Fi, and battery indicators fit inside the round screen;
- long-pressing both buttons closes Assistant and returns to Launcher without offset, freeze, or stale UI;
- retry, close-timeout, and repeated open/close behavior remain functional.

## Out Of Scope

- remote firmware installation or OTA partition changes;
- changes to wake-word behavior or server protocol;
- conversation history, scrolling transcripts, or touch scrolling;
- Assistant-specific Wi-Fi configuration;
- Bluetooth audio routing;
- a new pull-down control center;
- redesigning Launcher or other applications.
