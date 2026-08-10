#!/usr/bin/env python3
"""Remove the pre-pairing StopWatch voice key from macOS Keychain."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from collections.abc import Callable


LEGACY_KEYCHAIN_SERVICE = "com.stopwatch.codex-agent.voice"
LEGACY_KEYCHAIN_ACCOUNT = "StopWatch-Codex"
SECURITY_COMMAND_ENV = "STOPWATCH_SECURITY_COMMAND"
DEFAULT_SECURITY_COMMAND = "/usr/bin/security"
KEYCHAIN_TIMEOUT_SECONDS = 10.0
KEYCHAIN_ITEM_NOT_FOUND_EXIT = 44


class MigrationError(RuntimeError):
    """A redacted failure while deleting the one supported legacy item."""


def remove_legacy_voice_key(
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> bool:
    """Delete only the legacy fixed voice-key item; return whether it existed."""
    command = [
        os.environ.get(SECURITY_COMMAND_ENV, DEFAULT_SECURITY_COMMAND),
        "delete-generic-password",
        "-s",
        LEGACY_KEYCHAIN_SERVICE,
        "-a",
        LEGACY_KEYCHAIN_ACCOUNT,
    ]
    try:
        result = runner(
            command,
            capture_output=True,
            text=True,
            check=False,
            timeout=KEYCHAIN_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired):
        raise MigrationError("legacy voice key could not be removed") from None
    if result.returncode == KEYCHAIN_ITEM_NOT_FOUND_EXIT:
        return False
    if result.returncode != 0:
        raise MigrationError("legacy voice key could not be removed")
    return True


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("remove-legacy-voice-key")
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    if arguments.command != "remove-legacy-voice-key":
        return 2
    try:
        removed = remove_legacy_voice_key()
    except MigrationError as error:
        print(f"Legacy voice-key cleanup failed: {error}", file=sys.stderr)
        return 2
    print(
        "Legacy voice key removed."
        if removed
        else "Legacy voice key was already absent."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
