# Codex Global Button Validation

This checklist covers the upper-right button contract shared by the StopWatch
Codex app and the Mac Agent.

Inside the watch Codex app, an upper-right single click submits after the
300 ms double-click window. A fast double click steers the running turn, or
sends once when idle. Holding for 500 ms still controls Typeless.

The meaning applies on every page and in every conversation state in the watch
Codex app. A+B Home retains its 90 ms arbitration guard and, together with
screen wake, has priority. The authenticated input
notification is independent of voice readiness, so Typeless, BlackHole, and
audio availability do not gate click actions.

## Desktop safety contract

- The Agent requires the existing foreground bundle to be
  `com.openai.codex`; it does not activate Codex or target a background app.
- The focused element must match one of the English and Simplified Chinese
  descriptions from 16 composer resource IDs extracted from the current Codex
  desktop resources, plus the real `AXTextArea`, `ProseMirror`,
  `ChromeAXNodeId`, `_RichTextInput_`, and `_ComposerLayoutBody_`
  Accessibility anchors.
- The nearby Send, Queue, or Steer button must be unique, enabled, semantically
  recognized, and have its own stable Chromium node identifier. Empty text,
  an unknown label, missing permission, or ambiguous structure fails closed.
- Before dispatch, the Agent rechecks foreground PID, composer and button
  tokens, text, and semantic state. Normal Send, Queue, and Steer use verified
  button `AXPress`.
- Only Queue to forced Steer uses a complete Command+Shift+Return key-down and
  key-up pair through `CGEventPostToPid`, scoped to the revalidated Codex PID.
- macOS cannot make Accessibility revalidation and event delivery atomic. A
  same-process foreground race remains possible if focus changes inside Codex
  after the final check. Input actions use at-most-once delivery and are not
  retried after an ambiguous desktop result.

## Automated commands

Run these without physically pressing the watch button or invoking `AXPress`:

```bash
export IDF_PATH=/path/to/esp-idf
source "$IDF_PATH/export.sh"
./mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -v
./scripts/run_unified_offline_validation.sh --contract
IDF_PATH="$IDF_PATH" ./scripts/run_unified_offline_validation.sh \
  --host-tests-only
./mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_input_action_protocol.py \
  mac-codex-agent/tests/test_codex_submit.py -v
./scripts/build_unified_candidate.sh --clean
git diff --check
```

### Current automated evidence (2026-08-13)

- Full Mac Agent discovery: 897 tests passed; one read-only live Accessibility
  probe skipped because Codex was not foreground.
- Focused input protocol and desktop submission: 37 tests passed; the same live
  probe was the only skip.
- Offline contract: all 57 stages were present and correctly numbered, with 62
  exact host compile/run pairs.
- Real host execution: all 57 stages passed with `IDF_PATH` pointing at a local
  ESP-IDF 5.5.4 checkout.
- Firmware clean build: a fresh tracked checkout bootstrapped its locked
  dependencies and produced the ESP32-S3 application and assets successfully.
  `CODEC_UAC_SUPPORT` is now defined by tracked project Kconfig with `default n`,
  so the unused optional `espressif/usb_host_uac` dependency remains disabled.
  The application used 6,287,200 bytes and retained 56.39% partition headroom;
  the clean-checkout artifact SHA256 was
  `13488fc847f6538d7992408418032f3ccbca9ae58361439915136c184c299914`.
- `git diff --check`: passed.

Normal project flashing does not create or require a firmware backup. The
candidate flasher still requires the exact bootloader, partition table,
application, and assets artifacts and enforces partition and capacity limits.

No physical button and no live Accessibility action was invoked by automated
validation. The manual hardware cases below remain required.

The live Accessibility probe is read-only. It performs production recognition
assertions only when Accessibility is trusted, Codex is foreground, and the
focused element is one of the supported composers. Other normal desktop states
skip the live assertion; deterministic fixtures continue to verify fail-closed
behavior, the literal `ChromeAXNodeId`, and bridged NSArray traversal.

## Manual hardware acceptance

1. Idle Codex with text: one click sends once after about 300 ms.
2. Running Queue mode with text: one click queues; a fast double click steers
   once without first queueing.
3. Running Steer mode with text: a fast double click steers once.
4. Codex background, missing composer, empty composer, or unknown control:
   both click gestures are ignored.
5. Typeless unavailable: click actions remain usable, while holding for
   500 ms reports voice unavailable.
6. A second press that becomes a hold starts push-to-talk and cancels the
   pending click.
7. Screen wake and A+B Home never submit; Launcher and all non-Codex watch apps
   retain their existing button behavior.
