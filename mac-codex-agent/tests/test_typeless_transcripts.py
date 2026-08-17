import asyncio
import pathlib
import sqlite3
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from typeless_transcripts import TranscriptUnavailable, TypelessTranscriptSource


SCHEMA = """
CREATE TABLE history_v2 (
    id TEXT PRIMARY KEY,
    status TEXT,
    mode TEXT,
    refined_text TEXT,
    created_at TEXT,
    updated_at TEXT
)
"""


class TypelessTranscriptSourceTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db = pathlib.Path(self.temp.name) / "typeless.db"
        with sqlite3.connect(self.db) as connection:
            connection.execute(SCHEMA)

    def tearDown(self):
        self.temp.cleanup()

    def insert(self, row_id, status, text, created_at):
        with sqlite3.connect(self.db) as connection:
            connection.execute(
                "INSERT OR REPLACE INTO history_v2 VALUES (?, ?, ?, ?, ?, ?)",
                (row_id, status, "voice_transcript", text, created_at, created_at),
            )

    async def test_excludes_old_rows_and_returns_exact_new_unicode_once(self):
        self.insert("old", "completed", "old text", "2026-08-14T01:00:00Z")
        source = TypelessTranscriptSource(self.db, poll_interval=0)
        cursor = source.mark()
        self.insert("new", "processing", "", "2026-08-14T01:00:01Z")

        async def complete(_seconds):
            self.insert("new", "completed", "你好 🌍", "2026-08-14T01:00:01Z")

        source = TypelessTranscriptSource(
            self.db, monotonic=lambda: 0, sleep=complete, poll_interval=0
        )
        self.assertEqual(await source.wait_after(cursor, 1), "你好 🌍")

    async def test_missing_database_or_table_and_timeout_are_redacted(self):
        secret = "do-not-repeat-this-transcript"
        source = TypelessTranscriptSource(
            pathlib.Path(self.temp.name) / "missing.db",
            monotonic=lambda: 0,
            poll_interval=0,
        )
        with self.assertRaises(TranscriptUnavailable) as missing:
            source.mark()
        self.assertNotIn(str(self.temp.name), str(missing.exception))

        source = TypelessTranscriptSource(
            self.db,
            monotonic=iter((0, 2)).__next__,
            sleep=lambda _seconds: asyncio.sleep(0),
            poll_interval=0,
        )
        cursor = source.mark()
        self.insert("failed", "error", secret, "2026-08-14T01:00:02Z")
        with self.assertRaises(TranscriptUnavailable) as timeout:
            await source.wait_after(cursor, 1)
        self.assertNotIn(secret, str(timeout.exception))


if __name__ == "__main__":
    unittest.main()
