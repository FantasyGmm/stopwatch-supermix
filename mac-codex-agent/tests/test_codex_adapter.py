import datetime
import json
import pathlib
import sqlite3
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from codex_adapter import CodexPaths, CodexStateCollector


class CodexStateCollectorTests(unittest.TestCase):
    def test_builds_watch_snapshot_from_local_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            config = root / "config.toml"
            config.write_text(
                'model = "gpt-fallback"\nmodel_reasoning_effort = "low"\n',
                encoding="utf-8",
            )
            logs = root / "logs.sqlite"
            connection = sqlite3.connect(logs)
            connection.execute(
                "CREATE TABLE logs (id INTEGER PRIMARY KEY, feedback_log_body TEXT)"
            )
            connection.execute(
                "INSERT INTO logs(feedback_log_body) VALUES (?)",
                ("model=gpt-5.3 codex.turn.reasoning_effort=high",),
            )
            connection.commit()
            connection.close()

            fixed_now = datetime.datetime(2026, 7, 11, 12, 0).timestamp()
            day = datetime.datetime.fromtimestamp(fixed_now).strftime("%Y-%m-%d")
            monday = datetime.datetime.fromtimestamp(fixed_now) - datetime.timedelta(
                days=datetime.datetime.fromtimestamp(fixed_now).weekday()
            )
            week = monday.strftime("%Y-%m-%d")
            usage = root / "usage.json"
            usage.write_text(
                json.dumps({"days": {day: 2280}, "weeks": {week: 12840}}),
                encoding="utf-8",
            )

            collector = CodexStateCollector(
                paths=CodexPaths(
                    config=config,
                    auth=root / "missing-auth.json",
                    logs=logs,
                    usage=usage,
                ),
                now=lambda: fixed_now,
                quota_fetcher=lambda: {
                    "five_hour_used_pct": 28,
                    "week_used_pct": 36,
                },
            )

            snapshot = collector.get_state()

        self.assertEqual(snapshot["codex_model"], "gpt-5.3")
        self.assertEqual(snapshot["codex_effort"], "high")
        self.assertEqual(snapshot["codex_5h_left_pct"], 72)
        self.assertEqual(snapshot["codex_week_left_pct"], 64)
        self.assertEqual(snapshot["codex_today_min"], 38)
        self.assertEqual(snapshot["codex_week_min"], 214)


if __name__ == "__main__":
    unittest.main()
