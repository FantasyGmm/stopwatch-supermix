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

No raw prompt, response, source code, tool argument, tool output, request header,
or log line is persisted or transmitted. Only bounded normalized fields such as
project, short task title, state, model, and effort leave the collector.

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

## Task Alerts

The agent watches local Codex rollout events. `task_started` makes a conversation
Running. When the turn completes, the classifier first checks structured rollout
signals: unfinished plan items and unresolved input requests become Pending and
chime once. It then checks the final response structure for questions, explicit
choice handoffs, and required user actions. Normal responses become Completed
without a chime. A later user message completes older Pending records in that
conversation without affecting another conversation.

Classification stays entirely local and does not send response text to the
watch. Each completed alert stores an explainable reason such as
`structured_unfinished_plan`, `structured_input_request`, `text_question`, or
`completion_only`, so incorrect classifications can be diagnosed from the
ledger and log without exposing prompt or response content.

The project name is derived from the conversation working directory, and the
conversation title is refreshed through Codex's local app-server so generated
short titles replace initial prompt text. The watch displays only project,
short conversation title, and localized state; it does not display prompt or
response content. Pending records have priority and rotate exclusively. When no
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

## Test

```bash
python3 -m unittest discover -s mac-codex-agent/tests -v
```
