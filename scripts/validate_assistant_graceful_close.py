#!/usr/bin/env python3
"""Run repeatable Assistant open/graceful-close hardware validation."""

import argparse
import importlib.util
import json
import pathlib
import sys
import tempfile
import time


class ValidationError(RuntimeError):
    pass


CAMPAIGN_PROFILES = ("listening", "recording", "tts", "reconnect", "conversation")


def build_profile_schedule(cycles, specification):
    if specification == "mixed":
        return [CAMPAIGN_PROFILES[index % len(CAMPAIGN_PROFILES)] for index in range(cycles)]
    profiles = [item.strip() for item in specification.split(",") if item.strip()]
    invalid = [item for item in profiles if item not in CAMPAIGN_PROFILES]
    if invalid:
        raise ValidationError("unknown validation profiles: " + ", ".join(invalid))
    if len(profiles) == 1:
        return profiles * cycles
    if len(profiles) != cycles:
        raise ValidationError(
            f"profile count mismatch: {len(profiles)} profiles for {cycles} cycles"
        )
    return profiles


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

    def _has_record(self, session_id, **expected):
        return any(
            all(record.get(key) == value for key, value in expected.items())
            for record in self.records.get(session_id, ())
        )

    def profile_ready(self, session_id, profile):
        if profile == "listening":
            return self._has_record(
                session_id,
                event="snapshot",
                detail="first-running",
                state="running",
            )
        if profile == "recording":
            return self._has_record(
                session_id,
                event="conversation",
                detail="listening",
                result="accepted",
                v0="1",
            )
        if profile == "tts":
            return self._has_record(
                session_id,
                event="conversation",
                detail="speaking",
                result="accepted",
                v0="1",
            )
        if profile == "reconnect":
            return self._has_record(
                session_id,
                comp="debug",
                event="resource-decision",
                detail="wifi",
                result="accepted",
            )
        if profile == "conversation":
            has_round_trip = self._has_record(
                session_id,
                event="conversation",
                detail="start",
                result="accepted",
            ) and self._has_record(
                session_id,
                event="conversation",
                detail="running",
                result="accepted",
            )
            spoke = self._has_record(
                session_id,
                event="conversation",
                detail="speaking",
                result="accepted",
                v0="1",
            )
            finished = self._has_record(
                session_id,
                event="conversation",
                detail="speaking",
                result="accepted",
                v0="0",
            )
            return has_round_trip and spoke and finished
        raise ValidationError(f"unknown validation profile: {profile}")

    @staticmethod
    def _find_after(records, start, **expected):
        for index in range(start, len(records)):
            record = records[index]
            if all(record.get(key) == value for key, value in expected.items()):
                return index + 1
        return None

    def require_complete(self, session_id, profile="listening"):
        records = self.records.get(session_id, [])
        cursor = 0
        stop_acknowledgement = (
            (
                "offline stop acknowledgement",
                {
                    "event": "release",
                    "detail": "offline-stop",
                    "result": "accepted",
                },
            )
            if profile == "reconnect"
            else (
                "stopped callback",
                {"event": "callback", "detail": "stopped", "result": "accepted"},
            )
        )
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
            stop_acknowledgement,
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

    @staticmethod
    def _integer(fields, key, label):
        try:
            return int(fields[key])
        except (KeyError, TypeError, ValueError) as error:
            raise ValidationError(f"missing or invalid {key} in {label}: {fields}") from error

    def session_metrics(self, session_id, require_leak_gate=True):
        records = self.records.get(session_id, [])

        def find(label, **expected):
            for record in records:
                if all(record.get(key) == value for key, value in expected.items()):
                    return record
            raise ValidationError(f"missing {label} metrics for sid={session_id}")

        before = find("before-start", event="snapshot", detail="before-start")
        stopped = find("stop-boundary", event="snapshot", detail="stop-boundary")
        close_request = find(
            "accepted close request",
            event="close-request",
            result="accepted",
        )
        closed = find("application close", event="closed", state="stopped")
        status = self.status.get(session_id)
        if status is None:
            raise ValidationError(f"missing final status metrics for sid={session_id}")
        if require_leak_gate and stopped.get("result") != "ok":
            raise ValidationError(f"stop-boundary leak gate failed for sid={session_id}")

        close_timestamp = self._integer(close_request, "t", "close request")
        closed_timestamp = self._integer(closed, "t", "application close")
        stop_ms = closed_timestamp - close_timestamp
        if stop_ms < 0:
            raise ValidationError(f"invalid stop duration for sid={session_id}: {stop_ms}")

        return {
            "session_id": session_id,
            "before_internal": self._integer(before, "v0", "before-start"),
            "before_psram": self._integer(before, "v1", "before-start"),
            "after_internal": self._integer(status, "internal", "final status"),
            "after_psram": self._integer(status, "psram", "final status"),
            "internal_heap_loss": self._integer(stopped, "v0", "stop-boundary"),
            "psram_loss": self._integer(stopped, "v1", "stop-boundary"),
            "task_delta": self._integer(stopped, "v2", "stop-boundary"),
            "stop_ms": stop_ms,
        }


def validate_warmup_metrics(metrics):
    stop_ms = metrics.get("stop_ms")
    if stop_ms is None or stop_ms > AssistantCampaignValidator.MAX_STOP_MS:
        raise ValidationError(
            "Assistant warmup stop exceeded "
            f"{AssistantCampaignValidator.MAX_STOP_MS}ms: {stop_ms}ms"
        )
    return dict(metrics)


class AssistantCampaignValidator:
    MAX_SESSION_INTERNAL_HEAP_LOSS = 4096
    MAX_CUMULATIVE_INTERNAL_HEAP_LOSS = 8192
    MAX_STOP_MS = 2000

    def __init__(self, baseline_internal=None):
        self.metrics = []
        self.baseline_internal = baseline_internal

    def add(self, metrics, profile="listening"):
        session_id = metrics.get("session_id")
        internal_loss = metrics.get("internal_heap_loss")
        task_delta = metrics.get("task_delta")
        stop_ms = metrics.get("stop_ms")
        if internal_loss is None or internal_loss > self.MAX_SESSION_INTERNAL_HEAP_LOSS:
            raise ValidationError(
                f"single-session internal heap loss exceeded for sid={session_id}: "
                f"{internal_loss} > {self.MAX_SESSION_INTERNAL_HEAP_LOSS}"
            )
        if task_delta != 0:
            raise ValidationError(
                f"task count did not recover for sid={session_id}: delta={task_delta}"
            )
        if stop_ms is None or stop_ms > self.MAX_STOP_MS:
            raise ValidationError(
                f"Assistant stop exceeded {self.MAX_STOP_MS}ms for sid={session_id}: "
                f"{stop_ms}ms"
            )
        stored = dict(metrics)
        stored["profile"] = profile
        self.metrics.append(stored)

    def require_complete(self, expected_cycles):
        if len(self.metrics) != expected_cycles:
            raise ValidationError(
                f"campaign cycle count mismatch: {len(self.metrics)} != {expected_cycles}"
            )
        last = self.metrics[-1]
        baseline_internal = self.baseline_internal
        if baseline_internal is None:
            baseline_internal = self.metrics[0]["before_internal"]
        cumulative_loss = baseline_internal - last["after_internal"]
        if cumulative_loss > self.MAX_CUMULATIVE_INTERNAL_HEAP_LOSS:
            raise ValidationError(
                "cumulative internal heap loss exceeded: "
                f"{cumulative_loss} > {self.MAX_CUMULATIVE_INTERNAL_HEAP_LOSS}"
            )
        profile_counts = {}
        for item in self.metrics:
            profile = item["profile"]
            profile_counts[profile] = profile_counts.get(profile, 0) + 1
        return {
            "cycles": len(self.metrics),
            "peak_internal_heap_loss": max(
                0, *(item["internal_heap_loss"] for item in self.metrics)
            ),
            "peak_psram_loss": max(0, *(item["psram_loss"] for item in self.metrics)),
            "maximum_stop_ms": max(item["stop_ms"] for item in self.metrics),
            "cumulative_internal_heap_loss": cumulative_loss,
            "sessions": [item["session_id"] for item in self.metrics],
            "profiles": profile_counts,
        }


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

    def drain_input(self, timeout=0.5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._read_lines(min(0.05, max(0.0, deadline - time.monotonic())))
        self.pending = ""

    def synchronize(self, timeout=0.5):
        """Clear stale host output and any partial command held by the device."""
        self.drain_input(timeout=min(0.05, timeout))
        self.transport.write(b"\n")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._read_lines(min(0.05, max(0.0, deadline - time.monotonic())))
        self.pending = ""

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

    def settle(self, validator, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            for line in self._read_lines(
                min(0.25, max(0.0, deadline - time.monotonic()))
            ):
                validator.feed(line)


class Tee:
    def __init__(self, *outputs):
        self.outputs = outputs

    def write(self, text):
        for output in self.outputs:
            output.write(text)

    def flush(self):
        for output in self.outputs:
            output.flush()


def run_cycle(
    session, timeout, profile, require_leak_gate=True, settle_seconds=1.0
):
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
    if profile == "reconnect":
        session.command("assistant-wifi-cycle", validator=validator, timeout=timeout)
    session.wait_until(
        validator,
        lambda: validator.profile_ready(session_id, profile),
        timeout,
        f"Assistant {profile} profile",
    )
    session.command("assistant-home", validator=validator, timeout=timeout)
    session.wait_until(
        validator,
        lambda: validator.has_closed(session_id) and validator.launcher_open_count > 0,
        timeout,
        "Assistant close and Launcher reopen",
    )
    session.settle(validator, settle_seconds)
    session.command("assistant-status", validator=validator, timeout=timeout)
    validator.require_complete(session_id, profile=profile)
    return validator.session_metrics(session_id, require_leak_gate=require_leak_gate)


def read_launcher_internal_baseline(session, timeout):
    validator = AssistantSessionValidator()
    session.command("assistant-status", validator=validator, timeout=timeout)
    if not validator.status:
        raise ValidationError("missing Launcher baseline status")
    session_id = max(validator.status)
    return validator._integer(
        validator.status[session_id], "internal", "Launcher baseline status"
    )


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port; auto-detected when omitted")
    parser.add_argument("--cycles", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=45.0)
    parser.add_argument(
        "--settle",
        type=float,
        default=1.0,
        help="seconds to observe Launcher before post-close memory sampling",
    )
    parser.add_argument("--log", help="transcript path; temporary file when omitted")
    parser.add_argument("--summary", help="write campaign metrics as JSON")
    parser.add_argument(
        "--warmup",
        action="store_true",
        help="run one unmeasured listening cycle before the measured campaign",
    )
    parser.add_argument(
        "--profiles",
        default="listening",
        help="one profile, a comma-separated per-cycle list, or mixed",
    )
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.cycles < 1:
        print("assistant-validation: --cycles must be positive", file=sys.stderr)
        return 2
    if args.settle < 0:
        print("assistant-validation: --settle must not be negative", file=sys.stderr)
        return 2
    try:
        profiles = build_profile_schedule(args.cycles, args.profiles)
    except ValidationError as error:
        print(f"assistant-validation: {error}", file=sys.stderr)
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
            # A boot validator can close while the firmware still holds a
            # partial debug command. Terminate that line before this campaign.
            session.synchronize()
            warmup_metrics = None
            try:
                if args.warmup:
                    warmup_metrics = validate_warmup_metrics(
                        run_cycle(
                            session,
                            args.timeout,
                            "listening",
                            require_leak_gate=False,
                            settle_seconds=args.settle,
                        )
                    )
                campaign = AssistantCampaignValidator(
                    baseline_internal=read_launcher_internal_baseline(
                        session, args.timeout
                    )
                )
                for profile in profiles:
                    campaign.add(
                        run_cycle(
                            session,
                            args.timeout,
                            profile,
                            settle_seconds=args.settle,
                        ),
                        profile,
                    )
            except Exception:
                try:
                    session.command("assistant-trace", timeout=5.0)
                except Exception as dump_error:
                    print(f"trace dump also failed: {dump_error}", file=sys.stderr)
                raise
        summary = campaign.require_complete(args.cycles)
        if warmup_metrics is not None:
            summary["warmup"] = warmup_metrics
        if args.summary:
            with open(args.summary, "w", encoding="utf-8") as summary_file:
                json.dump(summary, summary_file, ensure_ascii=False, indent=2)
                summary_file.write("\n")
        print(
            "Assistant graceful-close validation passed: "
            + json.dumps(summary, ensure_ascii=False, sort_keys=True)
        )
        print(f"Transcript: {log_path}")
        if args.summary:
            print(f"Summary: {args.summary}")
        return 0
    except (ValidationError, watch_debug.PortDiscoveryError, OSError) as error:
        print(f"assistant-validation: {error}", file=sys.stderr)
        print(f"Transcript: {log_path}", file=sys.stderr)
        return 3
    finally:
        handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
