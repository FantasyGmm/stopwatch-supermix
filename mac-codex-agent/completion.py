"""Read Codex's native task-complete markers without loading full rollouts."""

import json
import os
import pathlib
import sqlite3
from typing import Optional


DEFAULT_STATE_DB = pathlib.Path.home() / ".codex" / "state_5.sqlite"
TAIL_BYTES = 512 * 1024


def _tail_lines(path: pathlib.Path, max_bytes: int = TAIL_BYTES):
    with path.open("rb") as stream:
        stream.seek(0, os.SEEK_END)
        size = stream.tell()
        stream.seek(max(0, size - max_bytes))
        data = stream.read().decode("utf-8", errors="ignore")
    return data.splitlines()


def read_latest_completion_from_rollout(path: pathlib.Path) -> Optional[str]:
    """Return the newest task_complete timestamp in one rollout."""
    try:
        lines = _tail_lines(path)
    except OSError:
        return None

    for line in reversed(lines):
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(item, dict):
            continue
        payload = item.get("payload")
        if not isinstance(payload, dict) or payload.get("type") != "task_complete":
            continue
        timestamp = item.get("timestamp") or payload.get("timestamp")
        return str(timestamp) if timestamp else None
    return None


def read_latest_completion_signal(
    state_db: pathlib.Path = DEFAULT_STATE_DB,
) -> Optional[str]:
    """Return a stable thread-id/timestamp signal for the newest completion."""
    if not state_db.exists():
        return None

    try:
        connection = sqlite3.connect(
            "file:{}?mode=ro".format(state_db), uri=True, timeout=1.0
        )
        try:
            rows = connection.execute(
                """
                SELECT id, rollout_path
                  FROM threads
                 WHERE model_provider = 'openai'
                   AND rollout_path IS NOT NULL
                   AND (thread_source IS NULL OR thread_source != 'subagent')
                 ORDER BY updated_at_ms DESC
                 LIMIT 8
                """
            ).fetchall()
        finally:
            connection.close()
    except (OSError, sqlite3.Error):
        return None

    latest_signal = None
    latest_timestamp = ""
    for thread_id, rollout_path in rows:
        if not rollout_path:
            continue
        timestamp = read_latest_completion_from_rollout(pathlib.Path(rollout_path))
        if timestamp and timestamp >= latest_timestamp:
            latest_timestamp = timestamp
            latest_signal = "{}:{}".format(thread_id, timestamp)
    return latest_signal
