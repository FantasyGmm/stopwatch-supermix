"""Compose Codex and Trae state while isolating source failures."""

from __future__ import annotations

import logging
import time
from typing import Callable, Dict


LOGGER = logging.getLogger(__name__)


class CompositeStateCollector:
    def __init__(self, codex_collector, trae_collector, now: Callable[[], float] = time.time):
        self._codex = codex_collector
        self._trae = trae_collector
        self._now = now
        self._codex_cache: Dict[str, object] = {
            "codex_fresh": False,
            "codex_collected_at": 0.0,
        }
        self._trae_cache: Dict[str, object] = {
            "trae_online": False,
            "trae_model": "--",
            "trae_effort": "--",
            "trae_revision": 0,
            "trae_tasks": [],
            "trae_fresh": False,
            "trae_collected_at": 0.0,
        }

    def get_state(self) -> Dict[str, object]:
        codex = self._collect_source(
            "codex",
            self._codex,
            self._codex_cache,
        )
        trae = self._collect_source(
            "trae",
            self._trae,
            self._trae_cache,
        )
        self._codex_cache = codex
        self._trae_cache = trae
        return {**codex, **trae}

    def _collect_source(self, name, collector, previous):
        try:
            current = dict(collector.get_state())
        except Exception as error:
            LOGGER.warning(
                "%s status collection failed: %s",
                name,
                type(error).__name__,
            )
            stale = dict(previous)
            stale["{}_fresh".format(name)] = False
            return stale
        current["{}_fresh".format(name)] = True
        current["{}_collected_at".format(name)] = self._now()
        return current

    def pending_watch_notification_ids(self):
        return self._codex.pending_watch_notification_ids()

    def mark_watch_notified(self, alert_ids) -> None:
        self._codex.mark_watch_notified(alert_ids)

    def consume_pending_notification(self):
        method = getattr(self._codex, "consume_pending_notification", None)
        return method() if method is not None else None

    def claim_pending_notification(self, notification):
        method = getattr(self._codex, "claim_pending_notification", None)
        return method(notification) if method is not None else None
