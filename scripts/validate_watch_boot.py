#!/usr/bin/env python3
"""Reset a connected watch and require a clean Launcher boot."""

import argparse
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time


class WatchBootError(RuntimeError):
    pass


def _load_watch_debug():
    path = pathlib.Path(__file__).with_name("watch_debug.py")
    spec = importlib.util.spec_from_file_location("stopwatch_boot_watch_debug", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


watch_debug = _load_watch_debug()


def esptool_python():
    return os.environ.get("PYTHON") or sys.executable


class WatchBootValidator:
    FATAL_MARKERS = (
        "Guru Meditation Error",
        "panic'ed",
        "Task watchdog got triggered",
        "watchdog reset",
    )

    def __init__(self):
        self.ready = False
        self.reset_line = None

    def feed(self, line):
        for marker in self.FATAL_MARKERS:
            if marker in line:
                raise WatchBootError(f"fatal boot marker observed: {marker}")
        if "rst:" in line:
            self.reset_line = line.strip()
        if "Launcher" in line and "on open" in line:
            self.ready = True
        if "DEBUG APP LIST " in line and "DEBUG APP LIST BUSY" not in line:
            self.ready = True


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port; auto-detected when omitted")
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--no-reset", action="store_true", help="do not reset before monitoring")
    parser.add_argument("--log", help="boot transcript path")
    parser.add_argument("--summary", help="write boot result as JSON")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.timeout <= 0:
        print("watch-boot: --timeout must be positive", file=sys.stderr)
        return 2

    if args.log is None:
        handle = tempfile.NamedTemporaryFile(
            mode="w", prefix="watch-boot-", suffix=".log", delete=False
        )
        log_path = handle.name
    else:
        log_path = args.log
        handle = open(log_path, "w", encoding="utf-8")

    validator = WatchBootValidator()
    pending = ""
    try:
        port = args.port or watch_debug.discover_port()
        if not args.no_reset:
            subprocess.run(
                [
                    esptool_python(),
                    "-m",
                    "esptool",
                    "--chip",
                    "esp32s3",
                    "-p",
                    port,
                    "run",
                ],
                check=True,
            )
        with watch_debug.SerialTransport(port) as transport:
            deadline = time.monotonic() + args.timeout
            next_probe = 0.0
            while time.monotonic() < deadline and not validator.ready:
                now = time.monotonic()
                if now >= next_probe:
                    transport.write(b"debug app list\n")
                    next_probe = now + 1.0
                chunk = transport.read(0.25)
                if not chunk:
                    continue
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                handle.write(text)
                handle.flush()
                pending += text
                lines = pending.split("\n")
                pending = lines.pop()
                for line in lines:
                    validator.feed(line)
        if not validator.ready:
            raise WatchBootError("timed out waiting for Launcher")
        summary = {
            "result": "passed",
            "reset_line": validator.reset_line,
            "launcher": "open",
        }
        if args.summary:
            with open(args.summary, "w", encoding="utf-8") as summary_file:
                json.dump(summary, summary_file, ensure_ascii=False, indent=2)
                summary_file.write("\n")
        print("Watch boot validation passed: " + json.dumps(summary, ensure_ascii=False))
        print(f"Transcript: {log_path}")
        return 0
    except (
        WatchBootError,
        watch_debug.PortDiscoveryError,
        OSError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"watch-boot: {error}", file=sys.stderr)
        print(f"Transcript: {log_path}", file=sys.stderr)
        return 3
    finally:
        handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
