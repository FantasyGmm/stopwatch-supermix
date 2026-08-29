#!/bin/bash
set -euo pipefail

APP_PATH="${STOPWATCH_AGENT_APP_PATH:-$HOME/Applications/StopWatch Codex Agent.app}"
EXECUTABLE="$APP_PATH/Contents/MacOS/StopWatchCodexAgent"
PID="$(pgrep -f "$EXECUTABLE" | head -n 1 || true)"

if [[ -z "$PID" ]]; then
    echo "StopWatch Codex Agent is not running" >&2
    exit 1
fi

FRAME="$(osascript - "$PID" <<'APPLESCRIPT'
on run argv
    set wantedPid to (item 1 of argv) as integer
    tell application "System Events"
        set matches to every process whose unix id is wantedPid
        if (count of matches) is 0 then error "Agent process is unavailable"
        tell item 1 of matches
            if (count of menu bars) is 0 then error "Agent has no menu bar"
            if (count of menu bar items of menu bar 1) is 0 then error "Agent has no status item"
            set itemPosition to position of menu bar item 1 of menu bar 1
            set itemSize to size of menu bar item 1 of menu bar 1
            return (item 1 of itemPosition as text) & "," & ¬
                (item 2 of itemPosition as text) & "," & ¬
                (item 1 of itemSize as text) & "," & ¬
                (item 2 of itemSize as text)
        end tell
    end tell
end run
APPLESCRIPT
)"

IFS=, read -r X Y WIDTH HEIGHT <<< "$FRAME"
if [[ "$X" == "-1" ]] || (( WIDTH <= 0 || HEIGHT <= 0 )); then
    echo "Agent status item is hidden outside the menu bar: $FRAME" >&2
    exit 1
fi

echo "Agent status item is visible: $FRAME"
