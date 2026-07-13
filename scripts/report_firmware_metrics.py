#!/usr/bin/env python3
"""Report reproducible firmware size metrics from existing ESP-IDF artifacts."""

import argparse
import csv
import hashlib
import json
import pathlib
import re
import subprocess
import sys


class MetricsError(RuntimeError):
    """Raised when metrics cannot be calculated without guessing."""


def require_file(path):
    path = pathlib.Path(path)
    if not path.is_file():
        raise MetricsError(f"required build artifact is missing: {path}")
    return path


def sha256_file(path):
    digest = hashlib.sha256()
    with require_file(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_size(value):
    value = value.strip().lower()
    multipliers = {"k": 1024, "m": 1024 * 1024}
    multiplier = 1
    if value and value[-1] in multipliers:
        multiplier = multipliers[value[-1]]
        value = value[:-1]
    try:
        return int(value, 0) * multiplier
    except ValueError as error:
        raise MetricsError(f"invalid partition size: {value!r}") from error


def parse_byte_limit(value):
    try:
        limit = int(value, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"invalid byte limit: {value!r}"
        ) from error
    if limit < 0:
        raise argparse.ArgumentTypeError("byte limit must not be negative")
    return limit


def first_app_partition(partition_table):
    partition_table = require_file(partition_table)
    with partition_table.open(encoding="utf-8", newline="") as source:
        for row in csv.reader(source):
            fields = [field.strip() for field in row]
            if not fields or not fields[0] or fields[0].startswith("#"):
                continue
            if len(fields) >= 5 and fields[1] == "app":
                return fields[0], parse_size(fields[4])
    raise MetricsError(f"no application partition found in: {partition_table}")


def parse_output_section(map_text, section_name):
    match = re.search(
        rf"(?m)^{re.escape(section_name)}\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*$",
        map_text,
    )
    if match is None:
        raise MetricsError(f"required map section is missing: {section_name}")
    return int(match.group(1), 16), int(match.group(2), 16)


def parse_diram_bytes(map_path):
    map_text = require_file(map_path).read_text(encoding="utf-8", errors="replace")
    boundary_match = re.search(
        r"(?m)^\s*(0x[0-9a-fA-F]+)\s+_diram_i_start\s*=",
        map_text,
    )
    if boundary_match is None:
        raise MetricsError("required map symbol is missing: _diram_i_start")

    diram_instruction_start = int(boundary_match.group(1), 16)
    iram_start, iram_size = parse_output_section(map_text, ".iram0.text")
    _, data_size = parse_output_section(map_text, ".dram0.data")
    _, bss_size = parse_output_section(map_text, ".dram0.bss")

    iram_end = iram_start + iram_size
    diram_text_size = max(0, iram_end - max(iram_start, diram_instruction_start))
    diram_bytes = diram_text_size + data_size + bss_size
    if diram_bytes <= 0:
        raise MetricsError("calculated DIRAM usage is not positive")
    return diram_bytes


def load_project_description(build_dir):
    description_path = require_file(
        pathlib.Path(build_dir) / "project_description.json"
    )
    try:
        description = json.loads(description_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as error:
        raise MetricsError(
            f"unable to read project description: {description_path}"
        ) from error
    for key in ("project_name", "project_version", "target", "app_elf", "app_bin"):
        if not description.get(key):
            raise MetricsError(f"project description is missing key: {key}")
    return description


def read_git_commit(repo_root, fallback):
    try:
        result = subprocess.run(
            ["git", "-C", str(repo_root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return fallback
    commit = result.stdout.strip()
    return commit or fallback


def collect_metrics(
    repo_root,
    build_dir,
    partition_table,
    assistant_bin=None,
    assets_bin=None,
    max_app_bytes=None,
    max_assets_bytes=None,
    source="launcher-baseline",
):
    repo_root = pathlib.Path(repo_root).resolve()
    build_dir = pathlib.Path(build_dir).resolve()
    description = load_project_description(build_dir)

    app_bin = require_file(build_dir / description["app_bin"])
    map_path = require_file(build_dir / pathlib.Path(description["app_elf"]).with_suffix(".map"))
    partition_label, partition_bytes = first_app_partition(partition_table)
    app_bytes = app_bin.stat().st_size
    if app_bytes > partition_bytes:
        raise MetricsError(
            f"application binary exceeds partition: {app_bytes} > {partition_bytes}"
        )
    if max_app_bytes is not None and app_bytes > max_app_bytes:
        raise MetricsError(
            "application capacity gate failed: "
            f"{app_bytes} > {max_app_bytes} bytes"
        )

    metrics = {
        "schema_version": 2,
        "source": source,
        "project_name": description["project_name"],
        "build_project_version": description["project_version"],
        "source_git_commit": read_git_commit(
            repo_root, description["project_version"]
        ),
        "target": description["target"],
        "app_bin": app_bin.relative_to(repo_root).as_posix()
        if app_bin.is_relative_to(repo_root)
        else str(app_bin),
        "app_bytes": app_bytes,
        "app_sha256": sha256_file(app_bin),
        "max_app_bytes": max_app_bytes,
        "app_partition_label": partition_label,
        "app_partition_bytes": partition_bytes,
        "app_headroom_bytes": partition_bytes - app_bytes,
        "app_headroom_percent": round(
            (partition_bytes - app_bytes) * 100 / partition_bytes, 2
        ),
        "diram_bytes": parse_diram_bytes(map_path),
        "assets_bin": None,
        "assets_bytes": None,
        "assets_sha256": None,
        "max_assets_bytes": max_assets_bytes,
        "capacity_gate_passed": (
            (max_app_bytes is None or app_bytes <= max_app_bytes)
        ),
    }

    if assets_bin is not None:
        assets_path = require_file(assets_bin)
        assets_bytes = assets_path.stat().st_size
        if max_assets_bytes is not None and assets_bytes > max_assets_bytes:
            raise MetricsError(
                "assets capacity gate failed: "
                f"{assets_bytes} > {max_assets_bytes} bytes"
            )
        metrics.update(
            {
                "assets_bin": assets_path.relative_to(repo_root).as_posix()
                if assets_path.is_relative_to(repo_root)
                else str(assets_path),
                "assets_bytes": assets_bytes,
                "assets_sha256": sha256_file(assets_path),
                "capacity_gate_passed": metrics["capacity_gate_passed"]
                and (
                    max_assets_bytes is None
                    or assets_bytes <= max_assets_bytes
                ),
            }
        )

    if assistant_bin is not None:
        assistant_path = require_file(assistant_bin)
        assistant_bytes = assistant_path.stat().st_size
        metrics.update(
            {
                "assistant_bin": assistant_path.relative_to(repo_root).as_posix()
                if assistant_path.is_relative_to(repo_root)
                else str(assistant_path),
                "assistant_app_bytes": assistant_bytes,
                "combined_app_bytes": app_bytes + assistant_bytes,
            }
        )

    return metrics


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--build-dir", type=pathlib.Path)
    parser.add_argument("--partition-table", type=pathlib.Path)
    parser.add_argument("--assistant-bin", type=pathlib.Path)
    parser.add_argument("--assets-bin", type=pathlib.Path)
    parser.add_argument("--max-app-bytes", type=parse_byte_limit)
    parser.add_argument("--max-assets-bytes", type=parse_byte_limit)
    parser.add_argument("--source", default="launcher-baseline")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    repo_root = args.repo_root.resolve()
    build_dir = (args.build_dir or repo_root / "build").resolve()
    partition_table = (
        args.partition_table or repo_root / "partitions.csv"
    ).resolve()
    assistant_bin = args.assistant_bin
    if assistant_bin is None and args.source == "launcher-baseline":
        default_assistant = (
            repo_root
            / "third_party"
            / "xiaozhi-esp32"
            / "build-m5stack-stopwatch"
            / "xiaozhi.bin"
        )
        assistant_bin = default_assistant if default_assistant.is_file() else None

    try:
        metrics = collect_metrics(
            repo_root=repo_root,
            build_dir=build_dir,
            partition_table=partition_table,
            assistant_bin=assistant_bin,
            assets_bin=args.assets_bin,
            max_app_bytes=args.max_app_bytes,
            max_assets_bytes=args.max_assets_bytes,
            source=args.source,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (MetricsError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print(json.dumps(metrics, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
