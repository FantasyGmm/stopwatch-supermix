"""Read exactly one new completed transcript from Typeless history."""

from __future__ import annotations

import asyncio
import dataclasses
import pathlib
import sqlite3
import time
from typing import Awaitable, Callable


DEFAULT_TYPELESS_DB = (
    pathlib.Path.home()
    / "Library"
    / "Application Support"
    / "Typeless"
    / "typeless.db"
)


class TranscriptUnavailable(RuntimeError):
    """A transcript could not be obtained without exposing its contents."""


@dataclasses.dataclass(frozen=True, order=True)
class TranscriptCursor:
    created_at: str
    row_id: str


class TypelessTranscriptSource:
    def __init__(
        self,
        path: pathlib.Path = DEFAULT_TYPELESS_DB,
        *,
        monotonic: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], Awaitable[None]] = asyncio.sleep,
        poll_interval: float = 0.1,
    ) -> None:
        self._path = pathlib.Path(path)
        self._monotonic = monotonic
        self._sleep = sleep
        self._poll_interval = poll_interval

    def _connect(self):
        uri = self._path.resolve().as_uri() + "?mode=ro"
        return sqlite3.connect(uri, uri=True, timeout=0.1)

    def mark(self) -> TranscriptCursor:
        try:
            with self._connect() as connection:
                row = connection.execute(
                    "SELECT created_at, id FROM history_v2 "
                    "WHERE mode = 'voice_transcript' "
                    "AND created_at IS NOT NULL AND id IS NOT NULL "
                    "ORDER BY created_at DESC, id DESC LIMIT 1"
                ).fetchone()
        except (OSError, sqlite3.Error) as error:
            raise TranscriptUnavailable(
                "Typeless transcript history is unavailable"
            ) from error
        return TranscriptCursor(*(row or ("", "")))

    async def wait_after(
        self, cursor: TranscriptCursor, timeout: float
    ) -> str:
        if not isinstance(cursor, TranscriptCursor):
            raise ValueError("cursor must be a TranscriptCursor")
        if timeout < 0:
            raise ValueError("timeout must be nonnegative")
        deadline = self._monotonic() + timeout
        while True:
            try:
                with self._connect() as connection:
                    row = connection.execute(
                        "SELECT refined_text FROM history_v2 "
                        "WHERE (created_at > ? OR (created_at = ? AND id > ?)) "
                        "AND mode = 'voice_transcript' "
                        "AND status = 'completed' "
                        "AND typeof(refined_text) = 'text' "
                        "AND length(refined_text) > 0 "
                        "ORDER BY created_at ASC, id ASC LIMIT 1",
                        (cursor.created_at, cursor.created_at, cursor.row_id),
                    ).fetchone()
            except (OSError, sqlite3.Error) as error:
                raise TranscriptUnavailable(
                    "Typeless transcript history is unavailable"
                ) from error
            if row is not None:
                return row[0]
            if self._monotonic() >= deadline:
                raise TranscriptUnavailable("Typeless transcript timed out")
            await self._sleep(self._poll_interval)
