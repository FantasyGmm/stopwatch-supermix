# Codex Global Button Validation

This checklist covers the upper-right button contract shared by the StopWatch
Codex app and the Mac Agent.

Inside the watch Codex app, an upper-right single click submits after the
300 ms double-click window. A fast double click steers the running turn, or
sends once when idle. Holding beyond 90 ms still controls Typeless.

The meaning applies on every page and in every conversation state in the watch
Codex app. A+B Home and screen wake have priority. The authenticated input
notification is independent of voice readiness, so Typeless, BlackHole, and
audio availability do not gate click actions.

## Desktop safety contract

- The Agent requires the existing foreground bundle to be
  `com.openai.codex`; it does not activate Codex or target a background app.
- The focused element must match one of the 16 English and Simplified Chinese
  composer descriptions extracted from the current Codex desktop resources,
  plus the real `AXTextArea`, `ProseMirror`, `ChromeAXNodeId`,
  `_RichTextInput_`, and `_ComposerLayoutBody_` Accessibility anchors.
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
./mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -v
./scripts/run_unified_offline_validation.sh --contract
./scripts/run_unified_offline_validation.sh --host-tests-only
./mac-codex-agent/.venv/bin/python -m unittest \
  mac-codex-agent/tests/test_input_action_protocol.py \
  mac-codex-agent/tests/test_codex_submit.py -v
./scripts/build_unified_candidate.sh --clean
git diff --check
```

### Current automated evidence (2026-08-12)

- Full Mac Agent discovery: 803 tests passed; one read-only live Accessibility
  probe skipped because Codex was not foreground.
- Focused input protocol and desktop submission: 34 tests passed; the same live
  probe was the only skip.
- Offline contract: all 50 stages were present and correctly numbered, with 54
  exact host compile/run pairs.
- Real host execution: all 50 stages passed with
  `IDF_PATH=/Users/luke/esp/esp-idf`.
- Firmware clean build: configuration stopped before application-source
  compilation because the local dependency set does not provide the required
  `CODEC_UAC_SUPPORT` option introduced by `espressif/usb_host_uac`. This is an
  existing build-environment/dependency blocker, not a successful firmware
  build.
- `git diff --check`: passed.

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
5. Typeless unavailable: click actions remain usable, while holding beyond
   90 ms reports voice unavailable.
6. A second press that becomes a hold starts push-to-talk and cancels the
   pending click.
7. Screen wake and A+B Home never submit; Launcher and all non-Codex watch apps
   retain their existing button behavior.
