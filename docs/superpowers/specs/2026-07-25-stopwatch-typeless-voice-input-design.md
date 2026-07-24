# StopWatch Typeless Voice Input Design

## Purpose

Turn the physically validated StopWatch-to-Mac BLE microphone path into a
push-to-talk input for Typeless. While the Codex watch app is open, pressing
and holding the upper-right hardware button starts Typeless and streams the
watch microphone to the Mac. Releasing the button stops Typeless and restores
the Mac's previous microphone selection.

The feature is part of the existing StopWatch Codex Agent. It must not require
the user to download, locate, or run a separate installer file.

## Confirmed Product Decisions

- Use BLE IMA-ADPCM at the validated 8 KiB/s operating profile.
- Keep the production voice characteristics unencrypted for this personal-use
  version. BLE encryption and bonding remain a later hardening task.
- Use BlackHole 2ch instead of developing a custom Core Audio driver.
- Integrate BlackHole setup into the Agent experience.
- Do not commit the BlackHole package or driver binary to Git.
- Keep the Agent manual-launch only. This feature does not add login startup or
  relaunch the Agent after the user quits it.
- Preserve the existing two-button hold gesture that returns to the launcher.

## Scope

### Watch Firmware

- Convert the existing bounded microphone probe into a production
  push-to-talk session controlled by the Codex app's right hardware button.
- Notify the Mac when a voice session begins and ends.
- Stream ordered 16 kHz mono IMA-ADPCM blocks while the button remains held.
- Show concise ready, recording, and error feedback in the existing Codex
  status area.
- Release microphone and BLE resources after release, disconnect, app close,
  home gesture, or capture failure.

### Mac Agent

- Receive voice notifications in the existing BLE session that carries Codex
  status and time synchronization.
- Decode ADPCM into 16 kHz mono PCM.
- Feed PCM continuously into BlackHole 2ch through Core Audio.
- Switch the system default input to BlackHole before Typeless starts
  recording, then restore the exact prior input after recording stops.
- Synthesize one Fn tap at push-to-talk start and one Fn tap at stop.
- Expose voice setup and current voice state in the existing menu-bar menu.
- Install BlackHole through a first-run action inside the Agent.

### Setup

- Detect whether BlackHole 2ch is installed and visible to Core Audio.
- Obtain explicit user action before the first privileged installation.
- Install the bundled local package with the macOS authorization UI.
- Detect when a restart is required and report that state instead of repeatedly
  attempting installation.

## Non-Goals

- Wi-Fi audio transport.
- Speech recognition inside the Agent.
- Modifying Typeless or its application data.
- Supporting arbitrary virtual audio drivers.
- Sending voice outside the local watch-to-Mac connection.
- Automatically starting the Agent at login.
- Eliminating the one-time macOS administrator authorization.
- Claiming encrypted or authenticated voice transport in this version.

## User Experience

### Initial Setup

The menu contains a non-selectable `Voice Input` status row. Before setup it
shows `Driver Required`, followed by a `Set Up Watch Voice Input...` command.
Selecting the command explains that one administrator authorization and a Mac
restart may be required. The Agent then invokes the system installer from its
own resources. The user never handles a package file.

After installation:

- `Voice Input: Restart Required` is shown until Core Audio exposes BlackHole;
- `Voice Input: Ready` is shown when BlackHole and input routing are available;
- setup commands disappear once the driver is ready;
- setup never enables login startup.

### Push To Talk

With the watch Codex app connected and voice input ready:

1. The user presses the upper-right hardware button.
2. The watch acquires the microphone and starts a new monotonic voice session.
3. The Mac primes BlackHole with silence, remembers the current default input,
   switches the default input to BlackHole, and emits one Fn tap.
4. The watch streams speech for as long as the button remains pressed.
5. On release, the watch flushes the final ADPCM block and sends the session-end
   event after all audio packets.
6. The Mac drains the jitter buffer, emits the second Fn tap, stops the
   BlackHole stream, and restores the remembered input device.

The watch status shows `VOICE` while recording. A disconnected or unavailable
Agent does not trigger the microphone or Fn and briefly shows `VOICE OFF`.

### Home Gesture And Screen-Off Interaction

The existing left-plus-right hold remains the home gesture. If the left button
is pressed during an active voice session, the watch cancels voice capture,
sends a session-end event when possible, and then allows the normal home
gesture to close the app.

When the Codex screen is off, the first right-button press only wakes the
screen. Push-to-talk becomes armed after both buttons are released. This
preserves the current wake guard and prevents hidden accidental recording.

## Architecture

### BLE Ownership

`BleStatusTransport` remains the sole Mac central connection. The standalone
probe client is not used by the installed Agent. The transport subscribes to
the voice notification characteristic immediately after connecting, then sends
status and time-sync data as it does today.

The watch advertises one service set and accepts one central. Status writes and
voice notifications share the same connection but retain independent framing
and queues. Status delivery must not block microphone capture, and voice
congestion must not corrupt status messages.

### Production Voice Protocol

The production protocol reuses the tested notification envelope and ADPCM
block format but replaces duration-based probe commands with button-driven
events.

| Event | Direction | Meaning |
| --- | --- | --- |
| `VOICE_READY` | Mac to watch | Driver, audio sink, and Fn event path are ready |
| `VOICE_UNAVAILABLE` | Mac to watch | Voice cannot start; includes a compact reason |
| `PTT_BEGIN` | Watch to Mac | New session after microphone acquisition |
| `AUDIO` | Watch to Mac | Ordered IMA-ADPCM data for that session |
| `PTT_END` | Watch to Mac | Final counters after all audio notifications |
| `VOICE_ERROR` | Either direction | Session cannot continue |

Each session has a nonzero 16-bit identifier and monotonic 32-bit sequence
numbers. The Mac ignores packets from stale sessions. `PTT_END` includes
captured sample, encoded block, overflow, and failure counters so cleanup and
loss remain observable outside the diagnostic probe.

### Watch Session State

The watch controller has the following states:

- `Unavailable`: Mac has not announced voice readiness.
- `Ready`: right-button push-to-talk may start.
- `Starting`: microphone ownership and recorder startup are in progress.
- `Streaming`: capture, resample, encode, and BLE notification are active.
- `Stopping`: the final block and summary are being sent.
- `Error`: the current press is rejected; release returns to `Ready` or
  `Unavailable`.

Only button edge transitions mutate the requested push-to-talk state. Holding
the button does not repeatedly start sessions. Every stop path is idempotent.

The producer uses the tested 8 KiB/s path, bounded packet queues, shared I2S
clock lifecycle, and off-stack capture buffer from the probe branch. No BLE API
is called directly from the audio capture callback.

### Mac Voice Pipeline

The Mac pipeline is split into independently testable components:

1. `VoiceNotificationReceiver` validates session and sequence ordering.
2. `AdpcmDecoder` converts each complete block to 16-bit mono PCM.
3. `PcmJitterBuffer` absorbs normal BLE arrival variation with bounded memory.
4. `BlackHoleSink` performs sample conversion to the device's Core Audio
   format and writes silence on underrun.
5. `DefaultInputLease` records, switches, and restores the system input device.
6. `FnShortcutController` emits complete Fn down/up taps and guarantees a
   matching stop tap after a successful start tap.
7. `TypelessVoiceSession` coordinates these components and owns cleanup.

The audio render callback never waits for BLE, logging, UI, or Python locks. It
consumes from a bounded PCM ring buffer. BLE callbacks enqueue complete
notifications into the Agent's asyncio loop.

`BlackHoleSink`, the PCM ring buffer, and the Core Audio device operations are
implemented in a small native bridge loaded inside the Agent process. Python
owns session orchestration and passes decoded PCM to the bridge through bounded
copy operations; Python is never called from the Core Audio render thread. The
bridge is built as part of the Agent and is not a second application or
background service.

### Timing And Pre-Roll

The BlackHole output stream is opened and primed before the start Fn tap. The
watch retains up to 300 ms of initial speech in a bounded pre-roll buffer while
the Mac changes the default input. Once the Mac session is active, pre-roll is
sent first, followed by live audio.

The target from right-button press to Fn tap is at most 250 ms. The target from
right-button release to the stop Fn tap is at most 350 ms, including final
audio drain. Actual timings must be measured on hardware; they are not inferred
from unit tests.

### Fn Event Generation

The Agent uses Quartz/PyObjC to emit a complete Fn key tap. Startup self-checks
verify Accessibility authorization before voice is reported ready. Missing
authorization produces `Accessibility Required` in the menu and opens the
standard macOS settings pane only after the user selects the corresponding
action.

The implementation does not activate, inspect, patch, or automate Typeless UI.
The physical acceptance test must prove that Typeless 2.1.0 responds to both
synthetic Fn taps.

### Input Device Switching

Typeless's current local data has no stable, documented input-device preference
that the Agent can configure. The Agent therefore changes the Core Audio system
default input immediately before the start Fn tap.

`DefaultInputLease` stores the device UID, not only its display name. It
restores that UID after the stop tap, on BLE disconnect, on Agent shutdown, and
after any partial setup failure. If the previous device disappears, the Agent
falls back to the built-in microphone and records the recovery in the log.

Hardware acceptance must prove that Typeless 2.1.0 opens the current default
input at recording start. If Typeless pins a device across recordings, this
design stops before modifying Typeless internals; a separate one-time Typeless
device-selection decision is required.

## BlackHole Provisioning

The local app build pins BlackHole 2ch `0.7.1`:

- source URL:
  `https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg`;
- SHA-256:
  `57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf`.

The build script downloads the package to a temporary cache, verifies the hash,
and places it under the generated app bundle's `Contents/Resources/Drivers/`.
The package is ignored by Git. A hash mismatch fails the build; there is no
latest-version fallback.

At runtime, setup verifies the package hash again and invokes
`/usr/sbin/installer` through the standard macOS authorization flow. The Agent
does not collect, pipe, log, or store the administrator password. After the
installer returns, it checks the package receipt and polls Core Audio for the
BlackHole device. If the device is not visible, the menu reports that a restart
is required.

This repository remains MIT and does not redistribute the BlackHole binary.
The generated private app bundle contains the GPL-3.0 package solely for the
owner's local use. The generated bundle must not be attached to a public GitHub
release without a separate license and distribution review.

## Security And Privacy Boundary

This personal-use version intentionally leaves the voice control and
notification characteristics unencrypted. Any nearby compatible BLE central
could otherwise attempt to connect while the Codex app is advertising.

Risk is constrained, but not eliminated, by these rules:

- microphone capture starts only from the physical right-button press;
- the watch displays an active recording state;
- capture is available only while the foreground Codex app owns the BLE
  transport;
- only one BLE central can connect;
- the Mac does not save voice recordings by default;
- disconnect, app close, and Agent quit stop capture and restore the input;
- logs contain counters and state transitions, never PCM or recognized text.

Encrypted pairing, bonding, and device identity authentication remain required
before this feature is distributed beyond personal use.

## Failure Handling

- **Driver missing:** Do not send `VOICE_READY`; show the setup action.
- **Restart required:** Keep status synchronization active but reject voice.
- **Accessibility missing:** Do not switch input or start a partial session.
- **Typeless absent:** The Agent reports `Typeless Not Running`, rejects
  push-to-talk, and does not launch or modify Typeless.
- **Microphone busy:** Watch sends `VOICE_ERROR`; no Fn tap is generated.
- **BLE gap or corruption:** Insert bounded silence for isolated gaps; abort the
  session on sequence regression, malformed payload, or sustained loss.
- **PCM underrun:** Output silence and count it; do not block Core Audio.
- **BLE disconnect:** Stop audio, send the matching stop Fn tap if needed, and
  restore the prior input.
- **Agent quit:** Run the same bounded cleanup before the process exits.
- **Second right press while stopping:** Ignore it until cleanup reaches
  `Ready`.

Cleanup operations are idempotent and bounded. A failure to restore the input
is surfaced in the menu and log rather than hidden.

## Menu-Bar Changes

The existing menu gains a compact voice section below Bluetooth state:

- `Voice Input: Driver Required | Restart Required | Accessibility Required |
  Ready | Recording | Error`;
- `Set Up Watch Voice Input...` only when the driver is missing;
- `Open Accessibility Settings...` only when authorization is missing.

No separate background process, dock icon, login item, or second menu-bar icon
is created.

## Testing Strategy

### Unit And Contract Tests

- Production voice protocol encoding, stale-session rejection, ordering, and
  final counter validation.
- Watch push-to-talk state transitions for press, release, disconnect, home,
  microphone busy, and repeated events.
- Firmware contracts for bounded queues, recorder ownership, no callback-side
  BLE calls, and guaranteed cleanup.
- ADPCM decoding and PCM ring-buffer overflow/underrun behavior.
- Default input lease restoration on success and every partial failure.
- Native Core Audio bridge format conversion, bounded PCM transfer, and
  real-time callback isolation from Python.
- Fn controller start/stop pairing and duplicate-event suppression.
- Driver package hash validation and installer result classification.
- Runtime and menu state transitions without requiring real BLE or Core Audio.

Production code is added only after the corresponding test has failed for the
expected missing behavior.

### Build And Offline Validation

- Run the full Python Agent suite.
- Run all C++ host tests.
- Run the unified ESP-IDF clean build and capacity gate.
- Build the generated Mac app and verify that its package hash matches the
  pinned value.
- Verify the normal Agent remains manual-launch only.

### Physical Acceptance

On the exact firmware and Agent revisions under test:

1. Install BlackHole through the Agent and restart if required.
2. Confirm the Agent reports `Voice Input: Ready`.
3. Open the watch Codex app and confirm status synchronization still works.
4. Focus a plain text field on the Mac.
5. Hold the right watch button, speak Mandarin for at least 30 seconds, and
   release.
6. Confirm Typeless starts and stops from the two Fn taps and inserts
   intelligible text from the watch microphone.
7. Confirm the previous Mac input device is restored.
8. Repeat after BLE reconnect and after reopening both applications.
9. Interrupt an active session by closing the watch app, quitting the Agent,
   and using the home gesture in separate runs.
10. Confirm no watch reboot, stuck Typeless recording, stuck default input,
    periodic dropout, or saved PCM remains.

Record firmware and Agent commit IDs, app and firmware hashes, packet counters,
start/stop latency, transcription result, watch serial log, and input-device
before/after values under a timestamped evidence directory outside Git.

## Acceptance Criteria

The feature is complete only when:

- one Agent setup action installs BlackHole without the user handling a package;
- the Agent remains off after the user quits it;
- push-to-talk produces exactly one start and one stop Fn tap;
- at least 30 seconds of Mandarin is intelligibly transcribed by Typeless;
- right-button-to-start latency is at most 250 ms and release-to-stop latency is
  at most 350 ms;
- the previous input device is restored after normal stop, BLE disconnect,
  watch app close, Agent quit, and error;
- status and time synchronization continue during idle voice readiness;
- the watch does not reboot and reports zero encode failures or queue
  overflows;
- the launcher home gesture and screen-off wake behavior remain unchanged.

Build success alone does not satisfy these criteria. Typeless behavior, Fn
handling, BlackHole routing, and physical cleanup all require a hardware run.

## Repository Safety

Implementation continues on `codex/ble-voice-link-probe` so the validated voice
transport commits remain traceable. Commits must not stage BlackHole packages,
generated app bundles, evidence audio, or unrelated main-worktree files.
