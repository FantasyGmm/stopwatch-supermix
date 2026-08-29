# StopWatch Codex Mac Agent

## Build the latest macOS release

Run `./build_release.sh` from this directory. The release builder creates a
portable universal2 DMG in `../artifacts/macos-agent/latest/`. Only the latest
release is retained. The first release is `1.0.0`; a changed set of production
Agent inputs increments the patch version after a successful build, while an
unchanged rebuild keeps the current version.

After installing and opening the Agent, verify that its status item is actually
present in the visible macOS menu bar:

```bash
./check_menu_bar_entry.sh
```

The current machine has no Developer ID identity, so the generated app is
ad-hoc signed and may require manual Gatekeeper approval on another Mac.

This menu bar agent reads local Codex status and, when Trae CN is installed,
local Trae task status. It sends compact, source-tagged snapshots to the
StopWatch over BLE. Trae CN is optional: Codex collection and watch updates
continue normally when Trae is absent or its private local format changes.

After each successful BLE connection, it sends one time-sync event before the
first status snapshot. The event contains the current Unix time and the Mac's
effective UTC offset; the watch applies them globally and stores the result in
its hardware RTC. The current version uses a fixed offset for that connection,
so reconnecting refreshes daylight-saving or timezone changes.

The Trae collector makes no network requests. It reads only local process state,
allowlisted workspace metadata, and newly appended local log events. A
private-format parse failure degrades the Trae page to `WAIT` and `--` values
without affecting Codex.

Task names and status use the existing status channel. Exact last replies are
encrypted and authenticated with the paired master key and its key epoch; reply
plaintext is never sent or logged. Route metadata remains visible and is
authenticated as part of each encrypted detail envelope. Raw prompts, source
code, tool arguments, tool output, request headers, and raw log lines are not
included in watch updates. This is an application-layer protection and does not
assert that the BLE link itself is encrypted.

If two snapshot records expose the same opaque route, that route is excluded
from both the compact task list and encrypted details for the rest of the
connected Agent session, even if one record later disappears. A new BLE session
starts with a fresh registry and accepts the route only when its authoritative
snapshot has no collision.

## Install

```bash
./mac-codex-agent/install_launch_agent.sh
```

The installer creates `mac-codex-agent/.venv`, installs the Python dependencies,
and creates a locally signed `~/Applications/StopWatch Codex Agent.app`. The
script keeps its original filename for compatibility, but it no longer installs
a LaunchAgent. It also removes the old `com.stopwatch.codex-agent` LaunchAgent
when upgrading.

Installation does not start the application, and the application does not start
when you log in. Open **StopWatch Codex Agent** manually from your Applications
folder. The first launch may ask for Bluetooth access; click **Allow**. You do
not manually add Python in System Settings. If access was previously denied,
reset only this application's permission and open it again:

```bash
tccutil reset Bluetooth com.stopwatch.codex-agent
open "$HOME/Applications/StopWatch Codex Agent.app"
```

Logs are written to:

```text
~/Library/Logs/stopwatch-codex-agent.log
```

## Menu Bar Controls

- The top **Codex Status** section shows model, reasoning effort, five-hour
  remaining quota, and seven-day remaining quota. Missing values appear as
  `--`.
- Model and effort are selected as one real pair from conversations currently
  in Running state. The most common pair wins; ties use the most recently
  started Running conversation. With no Running conversation, the last winner
  is restored from `~/.codex/.stopwatch_model_selection.json`.
- **Task Alerts** shows the latest current-day state of each conversation,
  grouped into Pending, Running, and Completed. Pending rows are orange,
  Running rows are blue, and Completed rows are dimmed.
- The optional **Trae Status** section shows Trae availability, model, effort,
  and up to four normalized task rows. Trae failures are isolated from Codex.
- Codex rollout changes wake collection and watch delivery immediately. A
  two-second heartbeat remains as recovery for a missed filesystem event. The
  menu continues updating even when the watch is disconnected or watch
  connection is disabled.
- **Agent** shows whether the menu application is running.
- **Bluetooth** shows Connection Off, Scanning, Waiting for Watch, Connecting,
  Connected, Permission Required, Unavailable, or Connection Error.
- **Enable Watch Connection** immediately starts or stops BLE activity. This
  choice is remembered after quitting and after restarting the Mac.
- **Reconnect Now** disconnects the current session and begins a fresh scan.
- **Open Log** opens `~/Library/Logs/stopwatch-codex-agent.log`.
- **Quit StopWatch Codex Agent** disconnects and completely stops the process.

After Quit, nothing automatically restarts the agent. Open the application
manually when synchronization is needed again.

## Codex Pet

Choose **更改 Codex 宠物 GIF…** in the menu to open the independent pet
window. Drop one `.gif` file or use **Choose GIF…**. Selection and preview work
while the watch is offline; **Send to Watch** requires an authenticated active
Production connection to a watch whose firmware exposes the optional pet
service. An older watch reports `Firmware update required` and disables Send
and Restore without disrupting status, voice, input actions, pairing, or
reconnect.

The Agent accepts GIF87a or GIF89a input up to 10 MiB, a `1024 × 1024` logical
canvas, 240 source frames, and 64 million cumulative decoded pixels. It
coalesces GIF disposal and transparency, removes consecutive duplicates, and
samples to at most 24 frames. The watch result is a transparent `144 × 156`
canvas with `60..1000 ms` frame delays and an exact CXP1 package no larger than
256 KiB. Scaling is aspect-fit, centered, nearest-neighbor, and never cropped
or stretched. Every accepted custom pet loops continuously; the source GIF's
loop count is not used.

The animated window preview is reconstructed from the exact CXP1 package that
will be sent, rather than from the source GIF. Its frame count, loop duration,
dimensions, transparency, palette, timing, and package-size labels therefore
describe the watch payload. The Agent cache stores only the verified package
keyed by the watch identity and authoritative package hash; it does not retain
the source GIF.

- **Send to Watch** reports Processing, Sending acknowledged bytes, Verifying,
  Applying, and Installed. Closing the window does not stop a runtime-owned
  transfer.
- **Cancel** retires a pre-Commit transfer before another upload can start. A
  Commit is terminal and is never retried after an ambiguous result.
- **Restore Default** first switches the watch atomically to its firmware
  robot and returns that durable built-in selection in an authenticated State
  notification. Custom slots are removed only after NimBLE accepts that exact
  response for the current connection, credential, subscription, and selection
  generation. An unavailable/failed notification or disconnect retains the
  slots for a later authenticated Query or bounded boot cleanup. A cleanup
  warning is surfaced on a later Query and remains retryable; it cannot
  reactivate the old custom pet.

Upload never erases shared storage. Before Begin, the watch needs room for the
staging package and its 56-byte slot envelope while retaining a 128 KiB
shared-storage reserve. An insufficient-space result displays the exact watch
values as `Watch storage requires X bytes; Y bytes are available.` and leaves
the active pet unchanged. Disconnect, timeout,
reboot, credential/Agent epoch change, malformed package data, or interrupted
upload likewise retains the previous complete pet.

A valid custom pet persists across watch reboot and an ordinary firmware flash
because that workflow does not write the `storage` partition. Invalid runtime
data falls back to the built-in robot. An unsupported package schema also uses
the built-in robot but preserves the stored package for compatible firmware;
it is not silently deleted. **Restore Default** commits the built-in selection
first and removes custom slots only after the matching State notification is
accepted; a later Query or boot cleanup retries retained slots. A full storage
erase removes the custom pet and all other content in that partition, so it is
destructive and is never part of Send, Restore, build, or ordinary flashing.

## Watch Voice Input

Watch voice input is part of the same menu-bar application. Building or
rebuilding the app does not start the application, create a login item, or
enable login startup. Open the existing **StopWatch Codex Agent** app manually,
then use its **Voice Input** row:

- `Voice Input: Driver Required`: choose **Set Up Watch Voice Input...**. The
  pinned BlackHole package asks for one administrator authorization. The menu
  reports `Voice Input: Restart Required` if macOS has installed the receipt
  but Core Audio cannot see the device yet; restart the Mac once.
- `Voice Input: Accessibility Required`: use the menu action to open
  Accessibility settings and allow **StopWatch Codex Agent** to send the
  Typeless shortcut.
- `Voice Input: Typeless Not Running`: start Typeless and confirm Typeless is
  configured with a dictation shortcut. The Agent reads that shortcut once at
  startup; restart the Agent after changing it in Typeless.
- `Voice Input: Ready`: open Codex on the watch and wait for the normal BLE
  connection before speaking.
- `Voice Input: Error`: release the right button, inspect **Open Log**, and use
  **Reconnect Now** after correcting the reported cause.

Inside the watch Codex app, an upper-right single click submits after the
300 ms double-click window. A fast double click steers the running turn, or
sends once when idle. Holding for 300 ms still controls Typeless.

The click meanings are global across every page and conversation state inside
the watch Codex app. They are not armed by a voice session and do not apply in
Launcher or any other watch app. A+B Home keeps its 90 ms arbitration guard
and always takes priority, and a wake
press is consumed until both buttons are released. A second short press that
becomes a hold cancels the pending click and starts push-to-talk without first
submitting.

The authenticated BLE input path is independent of voice readiness. Single and
double clicks therefore remain available when Typeless, BlackHole, or the audio
route is unavailable. The Agent handles them only when `com.openai.codex` is
already foreground and Accessibility exposes a recognized, editable, non-empty
composer with an unambiguous Send, Queue, or Steer control. It does not activate
Codex, target a background window, or submit from an empty or unknown composer.

One right swipe from the watch Codex status page opens **固定麦克风**. Its switch
holds the same authenticated Begin/Audio/End voice session open, so the Mac
receives continuous watch microphone audio without a held button. It feeds the
BlackHole route as an external microphone and does not start or stop Typeless.
It adds no GATT service or Mac audio route. Switch-off, Codex Home/close, BLE or
Agent epoch loss, or voice failure ends it; page navigation and display-off do
not.

For normal Send, Queue, or Steer, the Agent presses the verified Codex button
through Accessibility. Only a forced steer from Queue uses Codex's alternate
Command+Return behavior for the default Return-to-send composer mode, posted
to the already verified Codex process.
Actions are authenticated and replay-checked per BLE connection and are never
retried after an ambiguous desktop outcome.

The Agent restores the previous default input on button release, BLE
disconnect, Home, Agent quit, and all failure paths. Voice setup or transport
failure is isolated from the rest of the app: Codex status and time
synchronization continue even when voice input is unavailable.

## Voice Key And Security

Production firmware has no compiled voice key and does not read
`STOPWATCH_VOICE_KEY_FILE`. On first boot the watch creates its registry and a
random 16-byte master voice key at epoch one. A new Mac uses P-256 ECDH and a
six-digit verification code; compare the code on both devices and approve the
request physically on the watch. The watch can authorize up to eight Agents.

Each Agent stores an independent P-256 signing identity and per-watch
credential in macOS Keychain. Authorized Agents share the watch master key to
save watch storage. Pull down on the watch Codex page to switch the active
Agent. A new pairing request remains pending and does not interrupt the active
Agent. Use **Agent Name...** in the menu app to edit the name shown on the
watch; reconnect authentication signs the requested name before it is stored.

Removing an Agent on the watch rotates the master key and increments its epoch.
Connected Agents update immediately; an authorized offline Agent signs a fresh
challenge and receives the current key on its next connection. A removed Agent
cannot authenticate or obtain the rotated key. Reset All Agents clears the
registry, rotates again, and requires physical enrollment for every Mac.

For one migration release, remove the obsolete fixed Keychain item with:

```bash
mac-codex-agent/.venv/bin/python \
  scripts/provision_voice_key.py remove-legacy-voice-key
```

The cleanup command can delete only service
`com.stopwatch.codex-agent.voice`, account `StopWatch-Codex`; it never creates,
imports, or prints a key. Existing watches must be flashed once with this
firmware because older firmware cannot perform secure runtime enrollment.
If every Agent credential is lost, use **Reset All Agents** on the watch and
enroll each Mac again physically; a former credential is not a recovery bypass.

The production boundary is application-layer AES-128-CCM protocol version 6.
Version 6 deliberately rejects older peers. Each authenticated PTT begin frame
uses route `0` for plain Typeless dictation, or a nonzero 64-bit Codex task
route when voice should be transcribed and submitted to that task.
The clear packet header is authenticated, audio is encrypted with an 8-byte
tag, and authentication failure is fail-closed before decoding. Production
version 2 is rejected to prevent an unencrypted downgrade. This protects BLE
traffic but does not claim resistance to physical flash extraction; watch
flash encryption is a separate feature and is not enabled or validated here.

## Voice Troubleshooting

- **Driver Required**: choose **Set Up Watch Voice Input...** and approve the
  one macOS administrator prompt.
- **Restart Required**: restart the Mac, then open the Agent manually. The
  Agent never restarts the Mac or starts itself.
- **Accessibility Required**: grant Accessibility to **StopWatch Codex Agent**,
  not to a separately selected Python executable, then refresh or reconnect.
- **Typeless Not Running**: open Typeless, configure a dictation shortcut, and
  leave it running before holding the watch button. Restart the Agent after
  changing the shortcut so it reloads Typeless' current setting.
- **Voice Error**: release the button first. Open the Agent log and check audio
  routing, key mismatch, authentication, queue, and shortcut errors. The prior
  default input should already have been restored.
- **Pairing, MTU, or reconnect failure**: fully quit the standalone probe,
  close and reopen the watch Codex app so the normal `StopWatch-Codex` identity
  advertises again, then choose **Reconnect Now**. Accept a macOS pairing or
  Bluetooth permission prompt if shown. Production AUDIO is one authenticated
  186-byte characteristic value and requires the validated MTU 256 path.
- **No menu icon after login**: this is expected because there is no
  autostart. Open `~/Applications/StopWatch Codex Agent.app` manually.

The standalone version-1 BLE probe is diagnostic-only, uses a different
session contract, and must run only after fully quitting the Agent. See the
[BLE voice diagnostic guide](../docs/ble-voice-link-probe.md). Offline tests and
a clean firmware build do not replace BLE and physical acceptance on the watch.

## Codex Button Acceptance

Before relying on the physical controls, verify the following with the watch
Codex app open:

1. With Codex idle and non-empty text, a single click submits exactly once.
2. During a running turn in Queue mode, a single click queues and a fast double
   click steers exactly once.
3. During a running turn in Steer mode, a double click steers exactly once.
4. With Codex in the background, or its composer empty, clicks are ignored.
5. With Typeless unavailable, clicks still work while a hold reports voice as
   unavailable.
6. Screen wake, A+B Home, and a second press becoming a hold never submit.

## Task Alerts

The agent watches local Codex rollout events. `task_started` makes a conversation
Running. When the turn completes, the classifier first checks structured rollout
signals: unfinished plan items and unresolved input requests become Pending and
chime once. It then scans the full final response with a bounded set of broad
categories: direct questions, explicit choices, and required user actions.
Ambiguous handoffs stay Pending; only a later strong resolution for the same
category clears them. This intentionally favors an occasional false positive
over silently missing a task that is waiting for the user. A normal response
becomes Completed without a chime. A later user message completes older Pending
records in that conversation without affecting another conversation.

Classification stays local, while an exact last reply may be sent to the watch
only in the encrypted and authenticated detail envelope described above. Each
completed alert stores an explainable reason such as
`structured_unfinished_plan`, `structured_input_request`, `text_question`, or
`completion_only`, so incorrect classifications can be diagnosed from the
ledger and log without exposing prompt or response content.

The ledger also records the classifier version. After a classifier upgrade, the
agent streams each conversation's latest completed rollout from disk and
reclassifies only the latest unsuperseded completion. It never resurrects a turn
that already has a later user message. Temporarily incomplete or unreadable
rollouts remain queued for retry, and repeated refreshes do not duplicate the
Pending chime.

On macOS, the agent waits on the state database and known rollout files using a
bounded vnode watcher. A completed rollout therefore bypasses the normal
heartbeat delay. The resulting status snapshot owns its exact set of
unnotified Pending IDs: the agent sends that full status first, sends one chime
only after the status succeeds, and acknowledges those IDs only after the
chime succeeds. A newer ledger state cannot chime against an older displayed
Pending count. Negotiated BLE frame sizes reduce transfer round trips; the
validated 20-byte framing remains the compatibility fallback.

The project name is derived from the conversation working directory, and the
conversation title is refreshed through Codex's local app-server so generated
short titles replace initial prompt text. The watch displays only project,
short conversation title, and localized state in the compact list; selecting a
task can display its securely delivered exact last reply. Pending records have
priority and rotate exclusively. When no
Pending exists, Running records rotate. With neither active state, the latest
Completed record remains static under a green `COMPLETED` heading.

Alert cursors and acknowledgement state are stored at:

```text
~/.codex/.stopwatch_alerts.json
```

The first run records the end of existing rollout files, so historical
completions do not trigger old alerts. Later launches resume from saved cursors
and recover progress that occurred while the menu app was closed. If several
unnotified alerts accumulated while disconnected, reconnecting produces one
summary chime rather than one sound per alert; the individual records remain
visible in the ledger and carousel.

## Trae Local Cache

Incremental Trae collection stores only a byte cursor and the normalized,
bounded task ledger:

```text
~/.trae-cn/.stopwatch_cursor.json
~/.trae-cn/.stopwatch_tasks.json
```

The first scan starts at the end of existing logs, so installing or restarting
the agent does not replay historical Trae activity. Log rotation or truncation
causes a safe re-baseline. The ledger is capped and contains no raw prompt,
response, code, tool output, header, or source log line.

## Development Run

Run the menu application directly while developing:

```bash
cd mac-codex-agent
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python menu_app.py
```

Open the Codex app on the StopWatch so it advertises as `StopWatch-Codex`. The agent scans, connects, and sends status every two seconds.

## Remove

Quit the agent from its menu first, then remove the installed bundle:

```bash
rm -f "$HOME/Library/LaunchAgents/com.stopwatch.codex-agent.plist"
rm -rf "$HOME/Applications/StopWatch Codex Agent.app"
```

The pinned `BlackHole2ch-0.7.1.pkg` (SHA-256
`57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf`)
was inspected without installing it. Its package metadata declares receipt
`audio.existential.BlackHole2ch`, installs
`/Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver`, and requests a restart. It
does not contain an uninstaller. To remove only that verified 2-channel driver:

```bash
sudo rm -rf /Library/Audio/Plug-Ins/HAL/BlackHole2ch.driver
sudo pkgutil --forget audio.existential.BlackHole2ch
```

Restart the Mac afterward so Core Audio unloads the driver. These manual steps
are based on the pinned package payload and receipt; they are not presented as
an official BlackHole uninstaller.

## Test

```bash
python3 -m unittest discover -s mac-codex-agent/tests -v
```
