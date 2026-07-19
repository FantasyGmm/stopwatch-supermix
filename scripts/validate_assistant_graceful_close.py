#!/usr/bin/env python3
"""Run repeatable Assistant open/graceful-close hardware validation."""

import argparse
import importlib.util
import pathlib
import sys
import tempfile
import time


class ValidationError(RuntimeError):
    pass


def _load_watch_debug():
    path = pathlib.Path(__file__).with_name("watch_debug.py")
    spec = importlib.util.spec_from_file_location("stopwatch_watch_debug", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


watch_debug = _load_watch_debug()


class AssistantSessionValidator:
    RESET_MARKERS = (
        "rst:",
        "Guru Meditation Error",
        "panic'ed",
        "Task watchdog got triggered",
        "watchdog reset",
    )

    def __init__(self):
        self.records = {}
        self.status = {}
        self.running_session_id = None
        self.launcher_open_count = 0

    @staticmethod
    def _fields(line, marker):
        payload = line.split(marker, 1)[1]
        fields = {}
        for token in payload.strip().split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            fields[key] = value
        return fields

    def feed(self, line):
        for marker in self.RESET_MARKERS:
            if marker in line:
                raise ValidationError(f"reset marker observed: {marker}")

        if "ASTRACE sid=" in line:
            fields = self._fields(line, "ASTRACE ")
            try:
                session_id = int(fields["sid"])
                fields["seq"] = int(fields["seq"])
            except (KeyError, ValueError) as error:
                raise ValidationError(f"malformed ASTRACE line: {line}") from error
            records = self.records.setdefault(session_id, [])
            if records and fields["seq"] <= records[-1]["seq"]:
                raise ValidationError(f"out-of-order ASTRACE sequence for sid={session_id}")
            records.append(fields)
            if (
                fields.get("event") == "snapshot"
                and fields.get("detail") == "first-running"
                and fields.get("state") == "running"
            ):
                self.running_session_id = session_id
            return

        if "ASTATUS sid=" in line:
            fields = self._fields(line, "ASTATUS ")
            try:
                self.status[int(fields["sid"])] = fields
            except (KeyError, ValueError) as error:
                raise ValidationError(f"malformed ASTATUS line: {line}") from error
            return

        if "Launcher" in line and "on open" in line:
            self.launcher_open_count += 1

    def has_closed(self, session_id):
        return any(
            record.get("event") == "closed"
            for record in self.records.get(session_id, ())
        )

    @staticmethod
    def _find_after(records, start, **expected):
        for index in range(start, len(records)):
            record = records[index]
            if all(record.get(key) == value for key, value in expected.items()):
                return index + 1
        return None

    def require_complete(self, session_id):
        records = self.records.get(session_id, [])
        cursor = 0
        required = (
            ("open", {"event": "open"}),
            (
                "first-running snapshot",
                {"event": "snapshot", "detail": "first-running", "state": "running"},
            ),
            (
                "accepted close request",
                {
                    "event": "close-request",
                    "detail": "debug-home",
                    "result": "accepted",
                },
            ),
            (
                "stopping transition",
                {"event": "state", "state": "stopping", "detail": "stop"},
            ),
            (
                "stopped callback",
                {"event": "callback", "detail": "stopped", "result": "accepted"},
            ),
            (
                "clean release",
                {"event": "release", "state": "stopped", "result": "ok"},
            ),
            ("application close", {"event": "closed", "state": "stopped"}),
        )
        for label, expected in required:
            next_cursor = self._find_after(records, cursor, **expected)
            if next_cursor is None:
                raise ValidationError(f"missing {label} for sid={session_id}")
            cursor = next_cursor

        if self.launcher_open_count == 0:
            raise ValidationError(f"Launcher did not reopen for sid={session_id}")

        status = self.status.get(session_id)
        clean = {
            "state": "stopped",
            "bindings": "0",
            "subscriptions": "0",
            "wifi": "0",
            "capture": "0",
            "playback": "0",
            "ap": "0",
            "blocked": "0",
        }
        if status is None or any(status.get(key) != value for key, value in clean.items()):
            raise ValidationError(f"dirty resource status for sid={session_id}: {status}")


class SerialLineSession:
    def __init__(self, transport, output):
        self.transport = transport
        self.output = output
        self.pending = ""

    def _read_lines(self, timeout):
        chunk = self.transport.read(timeout)
        if not chunk:
            return []
        text = chunk.decode("utf-8", errors="replace")
        self.output.write(text)
        self.output.flush()
        self.pending += text
        lines = self.pending.split("\n")
        self.pending = lines.pop()
        return lines

    def command(self, action, validator=None, name=None, timeout=20.0):
        payload = watch_debug.build_command(action, name)
        deadline = time.monotonic() + timeout
        next_send = 0.0
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_send:
                self.transport.write(payload)
                next_send = now + 1.0
            for line in self._read_lines(0.25):
                if validator is not None:
                    validator.feed(line)
                status = watch_debug.classify_response(action, name, line)
                if status == watch_debug.ResponseStatus.SUCCESS:
                    return
                if status == watch_debug.ResponseStatus.REJECTED:
                    raise ValidationError(line.strip())
        raise ValidationError(f"command {action} timed out after {timeout:g}s")

    def wait_until(self, validator, predicate, timeout, label):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self._read_lines(0.25):
                validator.feed(line)
            if predicate():
                return
        raise ValidationError(f"timed out waiting for {label}")


class Tee:
    def __init__(self, *outputs):
        self.outputs = outputs

    def write(self, text):
        for output in self.outputs:
            output.write(text)

    def flush(self):
        for output in self.outputs:
            output.flush()


def run_cycle(session, timeout):
    validator = AssistantSessionValidator()
    session.command("assistant-trace-clear", timeout=timeout)
    session.command("open", validator=validator, name="assistant", timeout=timeout)
    session.wait_until(
        validator,
        lambda: validator.running_session_id is not None,
        timeout,
        "Assistant first-running",
    )
    session_id = validator.running_session_id
    session.command("assistant-home", validator=validator, timeout=timeout)
    session.wait_until(
        validator,
        lambda: validator.has_closed(session_id) and validator.launcher_open_count > 0,
        timeout,
        "Assistant close and Launcher reopen",
    )
    session.command("assistant-status", validator=validator, timeout=timeout)
    validator.require_complete(session_id)
    return session_id


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port; auto-detected when omitted")
    parser.add_argument("--cycles", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument("--log", help="transcript path; temporary file when omitted")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.cycles < 1:
        print("assistant-validation: --cycles must be positive", file=sys.stderr)
        return 2

    log_path = args.log
    if log_path is None:
        handle = tempfile.NamedTemporaryFile(
            mode="w", prefix="assistant-graceful-close-", suffix=".log", delete=False
        )
        log_path = handle.name
    else:
        handle = open(log_path, "w", encoding="utf-8")

    output = Tee(sys.stdout, handle)
    try:
        port = args.port or watch_debug.discover_port()
        with watch_debug.SerialTransport(port) as transport:
            session = SerialLineSession(transport, output)
            sessions = []
            try:
                for _ in range(args.cycles):
                    sessions.append(run_cycle(session, args.timeout))
            except Exception:
                try:
                    session.command("assistant-trace", timeout=5.0)
                except Exception as dump_error:
                    print(f"trace dump also failed: {dump_error}", file=sys.stderr)
                raise
        print(f"Assistant graceful-close validation passed: sessions={sessions}")
        print(f"Transcript: {log_path}")
        return 0
    except (ValidationError, watch_debug.PortDiscoveryError, OSError) as error:
        print(f"assistant-validation: {error}", file=sys.stderr)
        print(f"Transcript: {log_path}", file=sys.stderr)
        return 3
    finally:
        handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
