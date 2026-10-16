# Phase 1 validation record

## Current build and device acceptance (2026-10-16)

Codex menu compaction, temporary Host suspension, A/B page navigation and long-A
submission, optional standalone-Assistant exclusion, DFS and on-demand utility
sampling were compiled and deployed with explicit user approval.

- ESP-IDF 5.5.4 clean build and artifact export passed. App: 4,706,672 bytes;
  assets: 183 bytes; static DIRAM: 148,299 bytes. Original partition/capacity
  gates passed. Effective configuration enables PM, disables automatic DFS
  initialization and standalone Assistant/wake-word models; the display service
  configures an 80 MHz minimum and holds the 240 MHz lock while awake.
- Agent 1.0.14/build 15 compiled for Apple Silicon and Intel, passed static
  package/signature checks, and was atomically installed and restarted through
  the normal macOS app path. Existing pairing authenticated successfully.
- Firmware `2e2e9a2-dirty` was written via `/dev/cu.usbmodem31201`. All four
  image writes verified; independent reset showed the new version, TE GPIO38
  synchronization and Launcher startup without panic.
- The user confirmed connection, page navigation and screen-off wake are normal
  after opening Codex. The confirmation question also included Host being hidden.

No tests or runtime self-tests were run. Actual power draw, runtime heap/stack
high-water marks and voice/desktop input actions were not measured or exercised
in this update. Artifacts and deployment evidence are under
`../deliverables/firmware-on-demand-20261016/`,
`../deliverables/macos-agent-on-demand-20261016/` and the delivery validation logs.

Existing gesture and Xiaozhi contract fixtures still describe the retired
short-click submission and always-enabled wake-word defaults. They were neither
modified nor run for this revision.

## Earlier validation history

The user's latest instruction stopped further test additions and test runs.
The final native changes are compile checked; packaged binaries are not executed
for runtime self-tests or version smoke tests. Build commands do not install an
app or driver and do not flash a device. The user's existing installed Agent is
1.0.7; the user has since authorized updating and restarting
`/Applications/StopWatch Codex Agent.app` for BLE validation. Record
installation, restart, and BLE results only after observing each.

## Connection and page-fix revision

The 1.0.9 source revision repairs advertisement discovery, connection controls,
initial Agent naming, persistent logging and system-language menus. The firmware revision removes the
TRAE page, preserves the Usage/Host carousel positions, and simplifies the
environment display. See [the update guide](mac-agent-connection.md).
Existing navigation assertions were aligned with the removed page; no test
cases were added or executed. This revision's compiler/package output and
hardware acceptance state are recorded alongside the delivered artifacts, not
inherited from earlier passing checks below.

## Completed before tests were stopped

- Rust core: 76 passing cases across collector migration, notifications,
  pricing, expiry, GIF/cache handling, Python protocol vectors, secure pairing,
  task detail and bounded status framing. Raw output:
  `../logs/rust-core-tests.log`.
- Python baseline: 74 protocol cases and a separate 19-case pet/converter
  invocation passed. The separate invocation includes repeated package cases;
  these counts should not be added as unique coverage. Raw outputs:
  `../logs/protocol-oracles-python.log` and
  `../logs/protocol-oracles-pet-converter.log`.
- C++ host checks covered the Codex status model, existing page gestures and
  telemetry parser. The partial-usage display also compiled as an Xtensa object.
- Packaging shell fixtures, including atomic rollback and actual macOS lock
  exclusion, passed before the instruction to stop tests.

## Compilation and artifacts

Final native compilation uses `cargo check --locked --features native-runtime`.
The final release compiles both Apple Silicon and Intel targets, combines them
with `lipo`, and signs the app. With
`STOPWATCH_SKIP_RUNTIME_TESTS=1`, its validation uses static bundle fields,
architecture, signature and checksums only. `release.json` records the skipped
runtime self-test and the source fingerprint.

Firmware uses the official ESP-IDF v5.5.4 full release source at
`735507283d5b2f9fb363a1901172dbd9e847945d`, including clean recursive submodules.
The original partition table and dependency pins are retained. App/assets gates
remain `0x700000`/`0xA0000`. The exported metrics and manifest give the actual
size, hashes, source commit, static DIRAM and build configuration.

Build toolchain recorded for reproduction:

- Rust/Cargo 1.97.1
- Xcode 26.6 (17F113), macOS SDK 26.5
- ESP-IDF v5.5.4, Python 3.12.7, Xtensa GCC 14.2.0
- Offline Python oracle: Pillow 11.3.0 in a project-local virtual environment

## Limits

The environmental application uses a Chinese interface and generated font
subsets, separate sensor states, finite I²C timeouts and a HAL-owned sampler.
No tests were added or run for this extension. Firmware compilation and capacity
metrics are recorded in its exported artifact manifest. The user has confirmed
that the sensors and official accessories work on the current watch firmware.
The enlarged layout and new icon still require visual acceptance after an
authorized firmware update; sensor investigation is not part of this revision.

BLE pairing on hardware, visual layout, live AX actions, audio routing,
Typeless dictation, driver installation, pet transfer and runtime heap/PSRAM/
stack high-water marks require acceptance on the intended Mac and watch.
Compilation and static capacity checks do not establish those results.

Tests and compiler diagnostics used generated fixtures. No real Codex
configuration, authentication data or Keychain records were read or modified
for validation. The user has explicitly authorized this delivery's direct
firmware write; a complete 16 MiB read is optional recovery material, not a
prerequisite. Use the exact detected serial port and capacity-gated candidate,
preserve NVS, pairing credentials, and storage, and do not erase the entire Flash. After
writing, verify readback hashes for all written images and confirm a fresh boot
from serial output. Record those results only after observation;
`backup_watch_flash.sh` remains available for optional recovery.
