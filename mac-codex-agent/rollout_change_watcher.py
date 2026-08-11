"""Bounded macOS vnode waiting for Codex rollout changes."""

from __future__ import annotations

import os
import pathlib
import select
import time
from typing import Dict, Iterable, Optional, Tuple


FileSignature = Optional[Tuple[int, int, int]]


def capture_signatures(
    paths: Iterable[pathlib.Path],
) -> Dict[pathlib.Path, FileSignature]:
    signatures: Dict[pathlib.Path, FileSignature] = {}
    for value in paths:
        path = pathlib.Path(value)
        try:
            stat = path.stat()
        except OSError:
            signatures[path] = None
        else:
            signatures[path] = (stat.st_ino, stat.st_size, stat.st_mtime_ns)
    return signatures


def wait_for_path_change(
    paths: Iterable[pathlib.Path],
    baseline: Dict[pathlib.Path, FileSignature],
    timeout: float,
) -> bool:
    watched_paths = tuple(dict.fromkeys(pathlib.Path(path) for path in paths))
    bounded_timeout = max(0.0, float(timeout))
    if capture_signatures(watched_paths) != baseline:
        return True

    kqueue_factory = getattr(select, "kqueue", None)
    if kqueue_factory is None:
        time.sleep(bounded_timeout)
        return capture_signatures(watched_paths) != baseline

    queue = kqueue_factory()
    descriptors = []
    try:
        changes = []
        vnode_flags = (
            select.KQ_NOTE_WRITE
            | select.KQ_NOTE_EXTEND
            | select.KQ_NOTE_DELETE
            | select.KQ_NOTE_RENAME
            | select.KQ_NOTE_REVOKE
        )
        for path in watched_paths:
            if baseline.get(path) is None:
                continue
            try:
                descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            except OSError:
                return True
            descriptors.append(descriptor)
            changes.append(
                select.kevent(
                    descriptor,
                    filter=select.KQ_FILTER_VNODE,
                    flags=select.KQ_EV_ADD | select.KQ_EV_CLEAR,
                    fflags=vnode_flags,
                )
            )

        if capture_signatures(watched_paths) != baseline:
            return True
        if not changes:
            time.sleep(bounded_timeout)
            return capture_signatures(watched_paths) != baseline
        return bool(queue.control(changes, 1, bounded_timeout))
    finally:
        for descriptor in descriptors:
            os.close(descriptor)
        queue.close()
