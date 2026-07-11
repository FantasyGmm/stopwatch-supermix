# StopWatch Codex Mac Agent

This menu bar agent reads local Codex model, reasoning effort, quota, activity,
and native completion events, then sends a compact status snapshot to the
StopWatch over BLE.

It does not send OAuth credentials, prompts, responses, or source code to the watch.

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
  remaining quota, seven-day remaining quota, today's active minutes, and this
  week's active minutes. Missing values appear as `--`.
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

## Test

```bash
python3 -m unittest discover -s mac-codex-agent/tests -v
```
