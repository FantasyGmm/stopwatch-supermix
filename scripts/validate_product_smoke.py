#!/usr/bin/env python3
"""Open and close every non-Assistant watch application over USB debug control."""

import argparse
import importlib.util
import json
import pathlib
import sys
import tempfile


class ProductSmokeError(RuntimeError):
    pass


def _load_module(filename, module_name):
    path = pathlib.Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(module_name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


watch_debug = _load_module("watch_debug.py", "stopwatch_product_smoke_watch_debug")
assistant_validation = _load_module(
    "validate_assistant_graceful_close.py",
    "stopwatch_product_smoke_assistant_validation",
)


class ProductSmokeValidator:
    RESET_MARKERS = assistant_validation.AssistantSessionValidator.RESET_MARKERS

    def __init__(self, applications):
        self.applications = tuple(applications)
        self.catalog = set()
        self.opened = {name: 0 for name in self.applications}
        self.closed = {name: 0 for name in self.applications}
        self.launcher_open_count = 0

    def feed(self, line):
        for marker in self.RESET_MARKERS:
            if marker in line:
                raise ProductSmokeError(f"reset marker observed: {marker}")

        marker = "DEBUG APP LIST "
        if marker in line:
            payload = line.split(marker, 1)[1].strip().split()[0]
            self.catalog.update(item for item in payload.split(",") if item)

        if "Launcher" in line and "on open" in line:
            self.launcher_open_count += 1
        for name in self.applications:
            if f"[{name}] on open" in line or f"{name}: on open" in line:
                self.opened[name] += 1
            if f"[{name}] on close" in line or f"{name}: on close" in line:
                self.closed[name] += 1

    def require_catalog(self):
        expected = {watch_debug.normalize_app_name(name) for name in self.applications}
        missing = sorted(expected - self.catalog)
        if missing:
            raise ProductSmokeError("missing applications from catalog: " + ", ".join(missing))

    def require_round_trip(self, name, before_open, before_close, before_launcher):
        if self.opened[name] <= before_open:
            raise ProductSmokeError(f"{name} did not open")
        if self.closed[name] <= before_close:
            raise ProductSmokeError(f"{name} did not close")
        if self.launcher_open_count <= before_launcher:
            raise ProductSmokeError(f"Launcher did not reopen after {name}")


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="watch serial port; auto-detected when omitted")
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument(
        "--apps",
        default="Codex,Clock,Badge,IMU,Audio.FFT,Settings",
        help="comma-separated application names",
    )
    parser.add_argument("--log", help="transcript path; temporary file when omitted")
    parser.add_argument("--summary", help="write smoke result as JSON")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    applications = tuple(item.strip() for item in args.apps.split(",") if item.strip())
    if not applications:
        print("product-smoke: --apps must contain at least one application", file=sys.stderr)
        return 2

    if args.log is None:
        handle = tempfile.NamedTemporaryFile(
            mode="w", prefix="watch-product-smoke-", suffix=".log", delete=False
        )
        log_path = handle.name
    else:
        log_path = args.log
        handle = open(log_path, "w", encoding="utf-8")

    output = assistant_validation.Tee(sys.stdout, handle)
    validator = ProductSmokeValidator(applications)
    try:
        port = args.port or watch_debug.discover_port()
        with watch_debug.SerialTransport(port) as transport:
            session = assistant_validation.SerialLineSession(transport, output)
            session.synchronize()
            session.command("list", validator=validator, timeout=args.timeout)
            validator.require_catalog()
            for name in applications:
                before_open = validator.opened[name]
                before_close = validator.closed[name]
                before_launcher = validator.launcher_open_count
                session.command("open", validator=validator, name=name, timeout=args.timeout)
                session.wait_until(
                    validator,
                    lambda name=name, count=before_open: validator.opened[name] > count,
                    args.timeout,
                    f"{name} onOpen",
                )
                session.command("home", validator=validator, timeout=args.timeout)
                session.wait_until(
                    validator,
                    lambda name=name, close=before_close, launcher=before_launcher: (
                        validator.closed[name] > close
                        and validator.launcher_open_count > launcher
                    ),
                    args.timeout,
                    f"{name} close and Launcher reopen",
                )
                validator.require_round_trip(
                    name, before_open, before_close, before_launcher
                )

        summary = {
            "applications": list(applications),
            "cycles": len(applications),
            "result": "passed",
        }
        if args.summary:
            with open(args.summary, "w", encoding="utf-8") as summary_file:
                json.dump(summary, summary_file, ensure_ascii=False, indent=2)
                summary_file.write("\n")
        print("Product smoke validation passed: " + json.dumps(summary, ensure_ascii=False))
        print(f"Transcript: {log_path}")
        if args.summary:
            print(f"Summary: {args.summary}")
        return 0
    except (
        ProductSmokeError,
        assistant_validation.ValidationError,
        watch_debug.PortDiscoveryError,
        OSError,
    ) as error:
        print(f"product-smoke: {error}", file=sys.stderr)
        print(f"Transcript: {log_path}", file=sys.stderr)
        return 3
    finally:
        handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
