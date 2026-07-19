#!/usr/bin/env python3
"""Send local USB debug commands to a connected StopWatch."""

import argparse
import enum
import glob
import os
import re
import select
import sys
import termios
import time


class PortDiscoveryError(RuntimeError):
    pass


class DebugCommandError(RuntimeError):
    pass


class ResponseStatus(enum.Enum):
    PENDING = "pending"
    SUCCESS = "success"
    REJECTED = "rejected"


def discover_port(paths=None):
    candidates = sorted(paths if paths is not None else glob.glob("/dev/cu.usbmodem*"))
    if not candidates:
        raise PortDiscoveryError("no /dev/cu.usbmodem* watch port found")
    if len(candidates) != 1:
        raise PortDiscoveryError(
            "multiple watch ports found; pass --port: " + ", ".join(candidates)
        )
    return candidates[0]


def normalize_app_name(name):
    normalized = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    if not normalized:
        raise DebugCommandError("application name is empty")
    return normalized


def build_command(action, name=None):
    if action == "list":
        return b"debug app list\n"
    if action == "home":
        return b"debug app home\n"
    if action == "open" and name is not None:
        return f"debug app open {normalize_app_name(name)}\n".encode("ascii")
    raise DebugCommandError("unsupported debug command")


def classify_response(action, name, line):
    if "DEBUG ERROR " in line:
        return ResponseStatus.REJECTED
    if action == "list" and "DEBUG APP LIST " in line:
        return (
            ResponseStatus.REJECTED
            if "DEBUG APP LIST BUSY" in line
            else ResponseStatus.SUCCESS
        )
    if action == "home" and "DEBUG APP HOME " in line:
        return (
            ResponseStatus.SUCCESS
            if "DEBUG APP HOME OK" in line
            else ResponseStatus.REJECTED
        )
    if action == "open" and name is not None:
        prefix = f"DEBUG APP OPEN {normalize_app_name(name)} "
        if prefix not in line:
            return ResponseStatus.PENDING
        if f"{prefix}OK" in line:
            return ResponseStatus.SUCCESS
        if f"{prefix}BUSY" in line or f"{prefix}NOT_FOUND" in line:
            return ResponseStatus.REJECTED
    return ResponseStatus.PENDING


class SerialTransport:
    def __init__(self, path):
        self.path = path
        self.fd = -1

    def __enter__(self):
        self.fd = os.open(self.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attributes = termios.tcgetattr(self.fd)
        attributes[0] = 0
        attributes[1] = 0
        attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attributes[3] = 0
        attributes[4] = termios.B115200
        attributes[5] = termios.B115200
        attributes[6][termios.VMIN] = 0
        attributes[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1

    def write(self, payload):
        os.write(self.fd, payload)

    def read(self, timeout):
        readable, _, _ = select.select([self.fd], [], [], timeout)
        if not readable:
            return b""
        return os.read(self.fd, 4096)


def execute_command(transport, action, name, timeout, follow, output):
    payload = build_command(action, name)
    deadline = time.monotonic() + timeout
    next_send = 0.0
    pending = ""
    acknowledged = False

    while True:
        now = time.monotonic()
        if not acknowledged and now >= deadline:
            raise DebugCommandError(f"watch debug command timed out after {timeout:g}s")
        if not acknowledged and now >= next_send:
            transport.write(payload)
            next_send = now + 1.0

        chunk = transport.read(0.25)
        if not chunk:
            continue
        text = chunk.decode("utf-8", errors="replace")
        output.write(text)
        output.flush()
        pending += text
        lines = pending.split("\n")
        pending = lines.pop()
        for line in lines:
            status = classify_response(action, name, line)
            if status == ResponseStatus.REJECTED:
                raise DebugCommandError(line.strip())
            if status == ResponseStatus.SUCCESS:
                acknowledged = True
        if acknowledged and not follow:
            return


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port; auto-detected when omitted")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--follow", action="store_true", help="stream logs after acknowledgement")
    app = parser.add_subparsers(dest="scope", required=True).add_parser("app")
    command = app.add_subparsers(dest="action", required=True)
    command.add_parser("list")
    command.add_parser("home")
    open_parser = command.add_parser("open")
    open_parser.add_argument("name")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        path = args.port or discover_port()
        name = getattr(args, "name", None)
        with SerialTransport(path) as transport:
            execute_command(
                transport,
                action=args.action,
                name=name,
                timeout=args.timeout,
                follow=args.follow,
                output=sys.stdout,
            )
        return 0
    except (PortDiscoveryError, DebugCommandError, OSError) as error:
        print(f"watch-debug: {error}", file=sys.stderr)
        return 3
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
