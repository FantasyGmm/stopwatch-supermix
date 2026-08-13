# StopWatch Codex Mac Agent

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
- Codex status is sampled once every two seconds and shared with the watch. The
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
  Accessibility settings and allow **StopWatch Codex Agent** to send the left
  Option shortcut.
- `Voice Input: Typeless Not Running`: start Typeless and confirm Typeless is
  configured to start and stop dictation with left Option.
- `Voice Input: Ready`: open Codex on the watch and wait for the normal BLE
  connection before speaking.
- `Voice Input: Error`: release the right button, inspect **Open Log**, and use
  **Reconnect Now** after correcting the reported cause.

The right-button behavior is push-to-talk. A 90 ms chord guard reserves the
button for A+B Home handling: hold the right button beyond that guard to start
Typeless and recording, keep holding while speaking, and release it to stop.
A short tap is ignored. A+B is always reserved for Home, and a wake press is
consumed until the button is fully released, so waking the screen cannot start
dictation.

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

The production boundary is application-layer AES-128-CCM protocol version 3.
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
- **Typeless Not Running**: open Typeless, set its shortcut to left Option, and
  leave it running before holding the watch button.
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
