# Codex Global Button Validation

This checklist covers the upper-right button contract shared by the StopWatch
Codex app and the Mac Agent.

Inside the watch Codex app, an upper-right single click submits after the
300 ms double-click window. A fast double click steers the running turn, or
sends once when idle. Holding for 300 ms still controls Typeless.

The meaning applies on every page and in every conversation state in the watch
Codex app. A+B Home retains its 90 ms arbitration guard and, together with
screen wake, has priority. The authenticated input
notification is independent of voice readiness, so Typeless, BlackHole, and
audio availability do not gate click actions.

The **固定麦克风** pinned microphone page is one right swipe from Codex status.
Its switch keeps the existing encrypted voice session active without a held
button and feeds the Mac BlackHole route without starting Typeless. Page
navigation and display-off preserve it, while Home/close, disconnect, Agent
epoch change, or voice failure stop it.

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
- Only Queue to forced Steer uses a complete Command+Return key-down and
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

For the custom-pet package, installed-runtime smoke, and exact host-stage
contracts, also run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_install_script.py \
  mac-codex-agent/tests/test_unified_voice_validation.py -v
./scripts/run_unified_offline_validation.sh --contract
```

The installer smoke runs from the staged app's own executable with an empty
environment, `PYTHONNOUSERSITE=1`, and only bundle-local `PYTHONHOME` and
`PYTHONPATH`. It imports Pillow and all six pet modules, generates a temporary
two-frame GIF, converts it, and parses the resulting package. The source GIF
and temporary output are not published into the bundle.

## Custom-pet evidence boundaries

Record these gates separately. Passing an earlier row does not prove any later
row.

| Evidence layer | What proves it |
| --- | --- |
| Repository source | Focused Python/package/protocol tests plus exact runner contracts pass in the checked-out tree. |
| Packaged Agent bundle | A newly staged, self-contained app passes its bundle-local imports, generated two-frame GIF conversion, key-material scan, universal2 scan, and code-signature verification before publication. |
| Installed Agent bundle | The published app is revalidated and its six installed pet modules are compared with the intended source; repository or staging success alone is insufficient. |
| Running Agent process | The normally launched installed app emits a fresh `Agent started` after any expected Keychain approval; a process ID alone is insufficient. |
| Firmware build | A clean ESP-IDF 5.5.4 build and capacity gate pass; host C++ tests do not prove this. |
| Flash write | The exact bootloader, partition table, application, and assets offsets/hashes verify on the detected `/dev/cu.usbmodem*`; ordinary flashing must not write `storage` or create a firmware backup. |
| Fresh boot | Serial output from the newly written application proves storage mount, pet selection validation, and pet-service startup; flash-tool success alone is insufficient. |
| Authenticated BLE transport | The installed Agent discovers the optional service, reaches authenticated Ready, sends acknowledged bytes, and receives the authoritative committed generation/hash. |
| Physical AMOLED | Named photos or direct inspection compare built-in, custom, unavailable/error, navigation, and resume states region by region on the running round display. |

Task 10 intentionally validates only repository/packaging contracts and must
not install the Agent or flash the watch. Installation, normal-process restart,
authenticated upload, reboot, ordinary-flash persistence, interruption,
storage-reserve failure, Restore Default, and the physical AMOLED packet are
Task 11 release gates.

### Task 10 packaging evidence (2026-08-23)

- Focused installer and unified-contract suite: 30 tests passed in 37.303 s.
- Offline contract: all 68 stages were present and correctly numbered, with 74
  exact host compile/run pairs. Pet stages 39–43 identify the CXP1 parser,
  two-slot persistence, authenticated control/status protocol, authenticated
  transfer controller, and PSRAM animation runtime with their exact source
  lists.
- A temporary app published only beneath the Task 10 worktree passed staged and
  published validation: internal-symlink and key-material scans, universal2
  Mach-O checks, ad-hoc signing, and bundle-local runtime smoke. The isolated
  executable reported Pillow 11.3.0; imported `pet_package.py`,
  `pet_converter.py`, `pet_protocol.py`, `pet_upload.py`, `pet_cache.py`, and
  `pet_window.py`; converted a generated two-frame/200 ms GIF into a `144 ×
  156`, 796-byte CXP1 package; and reparsed both frames without repository
  paths.
- No installed Agent was replaced or launched. No firmware build, flash, boot,
  live BLE transfer, persistence test, or physical-display comparison was
  performed in Task 10.

## Custom-pet hardware acceptance

1. Open **Codex Pet...**, select the supplied six-frame GIF, and confirm the
   exact-package preview reports six frames, `1.08 s`, `144 × 156`, and no more
   than 256 KiB.
2. Confirm offline selection still previews, while an unsupported watch says
   `Firmware update required` and leaves Send/Restore disabled without
   interrupting the other BLE channels.
3. On an authenticated active Production connection, observe **Send to Watch**
   progress through Sending, Verifying, Applying, and Installed. Confirm only
   acknowledged bytes advance and the watch swaps at a frame boundary without
   a blank image.
4. Use **Cancel** during a replacement and confirm the old complete pet remains
   active. Trigger the 128 KiB shared-storage reserve gate and confirm the
   precise required/available error also preserves the old pet.
5. Reboot the watch and perform one ordinary firmware flash that excludes
   `storage`; confirm the authoritative hash and animation persist after each.
6. Use **Restore Default** and confirm the built-in robot is selected before
   slot cleanup. Confirm separately that a deliberately authorized full
   storage erase removes the custom pet; never use an erase as normal recovery.
7. Complete the named physical packet for the `144 × 156` pet box, aspect-fit
   transparency, pixel sharpness, cadence, circular clipping, telemetry and
   task text, page dots, battery, and unchanged buttons/swipes.

### Historical automated evidence (2026-08-14)

- Full Mac Agent discovery: 904 tests passed; one read-only live Accessibility
  probe was skipped because its foreground-app precondition was not present.
- Focused input protocol and desktop submission: 37 tests passed; the same live
  probe was the only skip.
- Offline contract: all 62 stages were present and correctly numbered, with 63
  exact host compile/run pairs.
- Real host execution: the prior 58-stage baseline passed with `IDF_PATH` pointing at a local
  ESP-IDF 5.5.4 checkout.
- Firmware clean build: a fresh tracked checkout bootstrapped its locked
  dependencies and produced the ESP32-S3 application and assets successfully.
  `CODEC_UAC_SUPPORT` is now defined by tracked project Kconfig with `default n`,
  so the unused optional `espressif/usb_host_uac` dependency remains disabled.
  The application used 6,361,264 bytes and retained 55.88% partition headroom;
  the flashed artifact SHA256 was
  `4001f5fba27eb73c5166a945bcf8c104c198379f46bc0efa898084c23d233a1e`.
- `git diff --check`: passed.

### Agent selector Option A evidence (2026-08-21)

- Focused Agent panel model, UI layout, presentation, firmware-contract, and
  unified-validation contract tests passed with `IDF_PATH` pointing at a local
  ESP-IDF 5.5.4 checkout.
- Incremental ESP-IDF 5.5.4 firmware build succeeded. The application image
  used `0x6523b0` bytes and retained `0x76dc50` bytes (54%) in the smallest
  application partition.
- The capacity-gated four-image candidate was flashed without erasing NVS.
  Independent `verify_flash` checks matched the bootloader, partition table,
  application, and assets digests. Serial validation loaded the application
  from `0x20000`, reached `[Launcher] on open`, and found no panic or watchdog
  reset.
- The user physically confirmed the collapsed Codex header and expanded Agent
  selector on the AMOLED: the pull hint no longer covers the Codex mark and the
  Agent page background is pure black.

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
   300 ms reports voice unavailable.
6. A second press that becomes a hold starts push-to-talk and cancels the
   pending click.
7. Screen wake and A+B Home never submit; Launcher and all non-Codex watch apps
   retain their existing button behavior.
