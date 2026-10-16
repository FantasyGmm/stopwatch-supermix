#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BUILD_DIR="$ROOT/build-unified"
DEST="$ROOT/../deliverables/firmware"

usage() {
    cat >&2 <<'EOF'
Usage: export_fusion_firmware.sh [--build-dir DIR] [--dest DIR]

Relative --build-dir and --dest values are resolved from the current directory.
The default destination is ../deliverables/firmware relative to the repository.
The destination must not already exist; exports are published atomically.
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --dest)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            DEST="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="$PWD/$BUILD_DIR"
fi
if [[ "$DEST" != /* ]]; then
    DEST="$PWD/$DEST"
fi

python3 - "$ROOT" "$BUILD_DIR" "$DEST" <<'PY'
import csv
import hashlib
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile


class ExportError(RuntimeError):
    pass


ROOT = pathlib.Path(sys.argv[1]).resolve(strict=True)
BUILD_DIR = pathlib.Path(sys.argv[2])
DEST_INPUT = pathlib.Path(sys.argv[3]).expanduser()
PROFILE = "stopwatch-single-system-v1"
MAX_APP_BYTES = 0x700000
MAX_ASSETS_BYTES = 0xA0000
FLASH_BYTES = 0x1000000

PARTITION_LAYOUT = (
    ("nvs", "data", "nvs", 0x9000, 0x10000, ""),
    ("phy_init", "data", "phy", 0x19000, 0x1000, ""),
    ("app", "app", "factory", 0x20000, 0xDC0000, ""),
    ("storage", "data", "fat", 0xDE0000, 0x100000, ""),
    ("assets", "data", "spiffs", 0xEE0000, 0xC0000, ""),
    ("coredump", "data", "coredump", 0xFA0000, 0x10000, ""),
)
PACKAGE_FILES = (
    "bootloader/bootloader.bin",
    "partition_table/partition-table.bin",
    "StopWatch-UserDemo.bin",
    "generated_assets.bin",
)


def fail(message):
    raise ExportError(message)


def is_within(path, parent):
    return path == parent or parent in path.parents


def require_regular_file(path, label, allowed_root=None):
    if path.is_symlink():
        fail(f"{label} must not be a symlink: {path}")
    if not path.is_file():
        fail(f"{label} is missing or not a regular file: {path}")
    resolved = path.resolve(strict=True)
    if allowed_root is not None and not is_within(resolved, allowed_root):
        fail(f"{label} resolves outside its allowed directory: {path}")
    cursor = path.parent
    while cursor != allowed_root and allowed_root is not None:
        if cursor.is_symlink():
            fail(f"{label} passes through a symlinked directory: {cursor}")
        if cursor.parent == cursor:
            break
        cursor = cursor.parent
    return resolved


def artifact_path(relative):
    path = BUILD_DIR / pathlib.PurePosixPath(relative)
    return require_regular_file(path, relative, BUILD_DIR)


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path, label):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"unable to read {label}: {error}")


def parse_partition_csv(path):
    actual = []
    try:
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            row = next(csv.reader([line], skipinitialspace=True))
            if len(row) != 6:
                fail(f"partitions.csv line {line_number} must have six columns")
            name, kind, subtype, offset, size, flags = (item.strip() for item in row)
            try:
                actual.append((name, kind, subtype, int(offset, 0), int(size, 0), flags))
            except ValueError:
                fail(f"partitions.csv line {line_number} has an invalid offset or size")
    except (OSError, UnicodeDecodeError, csv.Error) as error:
        fail(f"unable to parse partitions.csv: {error}")
    if tuple(actual) != PARTITION_LAYOUT:
        fail("partitions.csv does not match the supported 16 MiB StopWatch layout")


def check_partition_binary(path):
    reader = ROOT / "scripts/read_partition_entry.py"
    require_regular_file(reader, "partition-table validator", ROOT)
    command = [
        sys.executable,
        str(reader),
        "--profile",
        PROFILE,
        "--table",
        str(path),
        "--name",
        "app",
        "--field",
        "offset",
    ]
    result = subprocess.run(command, check=False, text=True, capture_output=True)
    if result.returncode != 0:
        fail(f"partition-table profile validation failed: {result.stderr.strip()}")
    if result.stdout.strip() != "0x20000":
        fail("partition-table app offset is not 0x20000")

    def read_field(name, field):
        field_result = subprocess.run(
            [
                sys.executable,
                str(reader),
                "--profile",
                PROFILE,
                "--table",
                str(path),
                "--name",
                name,
                "--field",
                field,
            ],
            check=False,
            text=True,
            capture_output=True,
        )
        if field_result.returncode != 0:
            fail(f"partition-table {name} validation failed: {field_result.stderr.strip()}")
        try:
            return int(field_result.stdout.strip(), 0)
        except ValueError:
            fail(f"partition-table {name}.{field} was not an integer")

    return {
        "app_offset": read_field("app", "offset"),
        "app_size": read_field("app", "size"),
        "assets_offset": read_field("assets", "offset"),
        "assets_size": read_field("assets", "size"),
    }


def safe_relative_file(value, label, expected_path):
    if not isinstance(value, str) or not value or "\\" in value:
        fail(f"metrics {label} is missing or is not a safe POSIX path")
    candidate = pathlib.Path(value)
    if ".." in candidate.parts:
        fail(f"metrics {label} contains a parent-directory traversal")
    if candidate.is_absolute():
        try:
            resolved = candidate.resolve(strict=True)
        except OSError as error:
            fail(f"metrics {label} cannot be resolved: {error}")
        if not is_within(resolved, BUILD_DIR):
            fail(f"metrics {label} resolves outside the build directory")
        if resolved != expected_path:
            fail(f"metrics {label} does not refer to the expected firmware artifact")
        return

    matches = []
    for base in (BUILD_DIR, ROOT):
        try:
            resolved = (base / candidate).resolve(strict=True)
        except OSError:
            continue
        if resolved == expected_path:
            matches.append(resolved)
    if not matches:
        fail(f"metrics {label} does not refer to the expected firmware artifact")


def validate_flasher_args(path):
    data = read_json(path, "flasher_args.json")
    expected = {
        "write_flash_args": [
            "--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "80m"
        ],
        "flash_settings": {
            "flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"
        },
        "flash_files": {
            "0x0": "bootloader/bootloader.bin",
            "0x20000": "StopWatch-UserDemo.bin",
            "0x8000": "partition_table/partition-table.bin",
        },
        "bootloader": {
            "offset": "0x0", "file": "bootloader/bootloader.bin", "encrypted": "false"
        },
        "app": {
            "offset": "0x20000", "file": "StopWatch-UserDemo.bin", "encrypted": "false"
        },
        "partition-table": {
            "offset": "0x8000", "file": "partition_table/partition-table.bin",
            "encrypted": "false",
        },
        "extra_esptool_args": {
            "after": "hard_reset", "before": "default_reset", "stub": True,
            "chip": "esp32s3",
        },
    }
    if data != expected:
        fail("flasher_args.json differs from the supported ESP32-S3 write map or settings")
    for relative in data["flash_files"].values():
        if pathlib.PurePosixPath(relative).is_absolute() or ".." in pathlib.PurePosixPath(relative).parts:
            fail(f"flasher_args.json contains an unsafe file path: {relative}")


def git_value(*arguments):
    result = subprocess.run(
        ["git", "-C", str(ROOT), *arguments], check=False, text=True, capture_output=True
    )
    if result.returncode != 0:
        fail(f"git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def parse_sdkconfig(path):
    text = path.read_text(encoding="utf-8")
    idf_versions = re.findall(r'^CONFIG_IDF_INIT_VERSION="([^"]+)"$', text, re.MULTILINE)
    targets = re.findall(r'^CONFIG_IDF_TARGET="([^"]+)"$', text, re.MULTILINE)
    if len(idf_versions) != 1 or len(targets) != 1:
        fail("sdkconfig.unified is missing a unique ESP-IDF version or target")
    if targets[0] != "esp32s3":
        fail(f"sdkconfig target is unsupported: {targets[0]}")
    return idf_versions[0], targets[0]


def make_destination():
    if DEST_INPUT.name in ("", ".", ".."):
        fail("destination must name a new package directory")
    if not DEST_INPUT.is_absolute():
        fail("destination path must be absolute after argument parsing")
    parent_input = DEST_INPUT.parent
    destination = parent_input.resolve(strict=False) / DEST_INPUT.name
    if os.path.lexists(destination):
        fail(f"destination already exists; refusing to replace it: {destination}")
    if is_within(destination, ROOT) or is_within(ROOT, destination):
        fail("destination must be outside the source repository")
    if is_within(destination, BUILD_DIR) or is_within(BUILD_DIR, destination):
        fail("destination must not overlap the build directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    parent = destination.parent.resolve(strict=True)
    destination = parent / destination.name
    if os.path.lexists(destination):
        fail(f"destination already exists; refusing to replace it: {destination}")
    return destination, parent


def publish_directory_exclusive(stage, destination):
    libc = ctypes.CDLL(None, use_errno=True)
    source = os.fsencode(stage)
    target = os.fsencode(destination)
    if hasattr(libc, "renamex_np"):
        # Darwin RENAME_EXCL: atomic rename that cannot replace an existing path.
        rename = libc.renamex_np
        rename.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        result = rename(source, target, 0x00000004)
    elif hasattr(libc, "renameat2"):
        # Linux RENAME_NOREPLACE.
        rename = libc.renameat2
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        result = rename(-100, source, -100, target, 1)
    else:
        fail("this platform lacks an atomic no-replace directory rename")
    if result != 0:
        error_number = ctypes.get_errno()
        if error_number == 17:
            fail(f"destination appeared during export; refusing to replace it: {destination}")
        raise OSError(error_number, os.strerror(error_number), str(destination))


def write_text(path, text):
    path.write_text(text, encoding="utf-8", newline="\n")
    path.chmod(0o644)


def main():
    global BUILD_DIR
    if not BUILD_DIR.is_dir():
        fail(f"build directory does not exist: {BUILD_DIR}")
    BUILD_DIR = BUILD_DIR.resolve(strict=True)

    sources = {relative: artifact_path(relative) for relative in PACKAGE_FILES}
    flasher_path = artifact_path("flasher_args.json")
    metrics_path = artifact_path("unified-runtime-metrics.json")
    table_path = sources["partition_table/partition-table.bin"]
    validate_flasher_args(flasher_path)
    parse_partition_csv(require_regular_file(ROOT / "partitions.csv", "partitions.csv", ROOT))
    partitions = check_partition_binary(table_path)

    sdkconfig_path = require_regular_file(ROOT / "sdkconfig.unified", "sdkconfig.unified", ROOT)
    idf_version, target = parse_sdkconfig(sdkconfig_path)
    project_description = read_json(artifact_path("project_description.json"), "project_description.json")
    if not isinstance(project_description, dict):
        fail("project_description.json must be a JSON object")
    if project_description.get("project_name") != "StopWatch-UserDemo":
        fail("project_description.json has an unexpected project name")
    if project_description.get("target") != target:
        fail("project_description.json target does not match sdkconfig.unified")
    idf_revision = project_description.get("git_revision")
    if not isinstance(idf_revision, str) or not idf_revision:
        fail("project_description.json is missing the ESP-IDF revision")
    if idf_revision.removeprefix("v") != idf_version:
        fail("ESP-IDF revision does not match sdkconfig.unified")

    metrics = read_json(metrics_path, "unified-runtime-metrics.json")
    if not isinstance(metrics, dict):
        fail("runtime metrics must be a JSON object")
    required_metrics = (
        "schema_version", "source", "project_name", "build_project_version",
        "source_git_commit", "target", "app_bin", "app_bytes", "app_sha256",
        "max_app_bytes", "app_partition_label", "app_partition_bytes",
        "app_headroom_bytes", "app_headroom_percent", "max_assets_bytes",
        "assets_bin", "assets_bytes", "assets_sha256", "capacity_gate_passed",
    )
    missing = [key for key in required_metrics if key not in metrics]
    if missing:
        fail("runtime metrics are missing required fields: " + ", ".join(missing))
    if metrics["schema_version"] != 2:
        fail("runtime metrics schema_version must be 2")
    if metrics["source"] != "unified-release-candidate":
        fail("runtime metrics do not identify a unified release-candidate build")
    if metrics["project_name"] != "StopWatch-UserDemo" or metrics["target"] != target:
        fail("runtime metrics project or target does not match the firmware")
    if project_description.get("project_version") != metrics["build_project_version"]:
        fail("runtime metrics project version differs from project_description.json")
    app_path = sources["StopWatch-UserDemo.bin"]
    assets_path = sources["generated_assets.bin"]
    safe_relative_file(metrics["app_bin"], "app_bin", app_path)
    safe_relative_file(metrics["assets_bin"], "assets_bin", assets_path)

    app_size = app_path.stat().st_size
    assets_size = assets_path.stat().st_size
    app_hash = sha256_file(app_path)
    assets_hash = sha256_file(assets_path)
    integer_fields = (
        "app_bytes", "max_app_bytes", "app_partition_bytes", "app_headroom_bytes",
        "max_assets_bytes", "assets_bytes",
    )
    if any(type(metrics[key]) is not int for key in integer_fields):
        fail("runtime metrics size and capacity fields must be integers")
    if type(metrics["capacity_gate_passed"]) is not bool or not metrics["capacity_gate_passed"]:
        fail("runtime metrics capacity_gate_passed must be true")
    if metrics["app_partition_label"] != "app":
        fail("runtime metrics app partition label must be app")
    if metrics["app_bytes"] != app_size or metrics["assets_bytes"] != assets_size:
        fail("runtime metrics sizes do not match the firmware binaries")
    if not isinstance(metrics["app_sha256"], str) or not isinstance(metrics["assets_sha256"], str):
        fail("runtime metrics SHA256 fields must be strings")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", metrics["app_sha256"]) or not re.fullmatch(r"[0-9a-fA-F]{64}", metrics["assets_sha256"]):
        fail("runtime metrics SHA256 fields must contain 64 hexadecimal digits")
    if metrics["app_sha256"].lower() != app_hash or metrics["assets_sha256"].lower() != assets_hash:
        fail("runtime metrics SHA256 values do not match the firmware binaries")
    if metrics["app_partition_bytes"] != partitions["app_size"]:
        fail("runtime metrics app partition size does not match the partition table")
    if metrics["max_app_bytes"] != MAX_APP_BYTES or metrics["max_assets_bytes"] != MAX_ASSETS_BYTES:
        fail("runtime metrics do not use the repository's declared capacity limits")
    if metrics["app_bytes"] > min(MAX_APP_BYTES, partitions["app_size"]):
        fail("application binary exceeds its capacity gate or partition")
    if metrics["assets_bytes"] > min(MAX_ASSETS_BYTES, partitions["assets_size"]):
        fail("assets binary exceeds its capacity gate or partition")
    expected_headroom = partitions["app_size"] - app_size
    expected_percent = round(expected_headroom * 100 / partitions["app_size"], 2)
    if metrics["app_headroom_bytes"] != expected_headroom or metrics["app_headroom_percent"] != expected_percent:
        fail("runtime metrics application headroom does not match the binary")
    if not isinstance(metrics["source_git_commit"], str) or not re.fullmatch(r"[0-9a-fA-F]{40}", metrics["source_git_commit"]):
        fail("runtime metrics source_git_commit must be a 40-digit Git SHA")
    source_commit = git_value("rev-parse", "HEAD")
    if metrics["source_git_commit"].lower() != source_commit.lower():
        fail("runtime metrics source commit differs from the checked-out source; rebuild first")
    if not isinstance(metrics["build_project_version"], str) or not metrics["build_project_version"]:
        fail("runtime metrics build_project_version is empty")

    destination, parent = make_destination()
    stage = pathlib.Path(tempfile.mkdtemp(prefix=f".{destination.name}.staging-", dir=parent))
    published = False
    try:
        for relative, source in sources.items():
            target_path = stage / pathlib.PurePosixPath(relative)
            target_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target_path)
            target_path.chmod(0o644)

        shutil.copyfile(flasher_path, stage / "flasher_args.json")
        (stage / "flasher_args.json").chmod(0o644)
        shutil.copyfile(sdkconfig_path, stage / "sdkconfig.unified")
        (stage / "sdkconfig.unified").chmod(0o644)

        portable_metrics = dict(metrics)
        portable_metrics["app_bin"] = "StopWatch-UserDemo.bin"
        portable_metrics["assets_bin"] = "generated_assets.bin"
        write_text(stage / "unified-runtime-metrics.json", json.dumps(portable_metrics, indent=2, sort_keys=True) + "\n")

        git_version = git_value("describe", "--always", "--dirty", "--tags")
        dirty_state = "dirty" if git_value("status", "--porcelain", "--untracked-files=no") else "clean"
        source_manifest = (
            "Firmware source and toolchain manifest\n"
            f"Project: {metrics['project_name']}\n"
            f"Firmware source commit recorded by build: {metrics['source_git_commit']}\n"
            f"Build project version recorded by metrics: {metrics['build_project_version']}\n"
            f"Checked-out source revision at export: {git_version}\n"
            f"Tracked source state at export: {dirty_state}\n"
            f"ESP-IDF version from sdkconfig: {idf_version}\n"
            f"ESP-IDF revision from project metadata: {idf_revision}\n"
            f"Target: {target}\n"
            f"SDK configuration SHA256: {sha256_file(sdkconfig_path)}\n"
            "The source tree may contain uncommitted changes; see the state above.\n"
        )
        write_text(stage / "source-idf-manifest.txt", source_manifest)

        map_entries = (
            ("bootloader", "0x000000", "bootloader/bootloader.bin"),
            ("partition-table", "0x008000", "partition_table/partition-table.bin"),
            ("app", "0x020000", "StopWatch-UserDemo.bin"),
            ("assets", "0xEE0000", "generated_assets.bin"),
        )
        portable_map = {
            "schema_version": 1,
            "target": target,
            "flash_bytes": FLASH_BYTES,
            "flash_mode": "dio",
            "flash_frequency": "80m",
            "flasher_args": "flasher_args.json",
            "writes": [
                {
                    "partition": label,
                    "offset": offset,
                    "file": relative,
                    "bytes": (stage / relative).stat().st_size,
                    "sha256": sha256_file(stage / relative),
                }
                for label, offset, relative in map_entries
            ],
            "partitions_not_written": ["nvs", "phy_init", "storage", "coredump"],
        }
        write_text(stage / "portable_flash_map.json", json.dumps(portable_map, indent=2) + "\n")

        flash_command = (
            "Informational template only. This file is not executable.\n\n"
            "Do not flash unless the user has explicitly authorized this specific firmware write.\n"
            "Use the exact detected connected /dev/cu.usbmodem* port; do not guess or leave the placeholder unchanged.\n"
            "Run from the repository root with the capacity-gated candidate:\n"
            "  ./scripts/flash_unified_candidate.sh --port \"REPLACE_WITH_EXACT_DEVICE_PORT\"\n"
            "The write script checks application/assets capacity and partition limits, then writes only the four image offsets in portable_flash_map.json. It does not erase the entire Flash or write NVS, authorized-Agent credentials, storage, or coredump.\n"
            "After writing, verify readback hashes for all four images against portable_flash_map.json and confirm a fresh firmware boot from serial output.\n\n"
            "A complete 16 MiB backup is optional recovery material, not a prerequisite. If needed, use an unused private output path:\n"
            "  ./scripts/backup_watch_flash.sh --port \"EXACT_DETECTED_DEVICE_PORT\" --output \"/path/to/new-private-backup/watch-full-flash.bin\"\n"
            "The backup script reads the full flash and removes an existing .sha256 companion before reading; keep the resulting image private because it may contain credentials.\n"
            "This package provides no automatic installer or flashing entry point.\n"
        )
        write_text(stage / "flash-command.txt", flash_command)

        files = sorted(
            (path for path in stage.rglob("*") if path.is_file()),
            key=lambda path: path.relative_to(stage).as_posix(),
        )
        sums = "".join(
            f"{sha256_file(path)}  {path.relative_to(stage).as_posix()}\n"
            for path in files
        )
        write_text(stage / "SHA256SUMS", sums)

        # Refuse package metadata that accidentally embeds machine-specific paths.
        for relative in (
            "flasher_args.json", "unified-runtime-metrics.json", "portable_flash_map.json"
        ):
            text = (stage / relative).read_text(encoding="utf-8")
            if re.search(r'(?<![A-Za-z0-9_])/(?:Users|private|tmp)/', text):
                fail(f"portable metadata contains a local absolute path: {relative}")
        publish_directory_exclusive(stage, destination)
        published = True
        print(f"Firmware package published atomically: {destination}")
        print(f"Source commit: {metrics['source_git_commit']} ({metrics['build_project_version']})")
        print(f"Application: {app_size} bytes; assets: {assets_size} bytes; capacity gate: passed")
    finally:
        if not published and stage.exists():
            shutil.rmtree(stage)


def handle_sigterm(signum, frame):
    raise KeyboardInterrupt(f"received signal {signum}")


if __name__ == "__main__":
    import ctypes

    signal.signal(signal.SIGTERM, handle_sigterm)
    try:
        main()
    except (ExportError, OSError, KeyboardInterrupt) as error:
        print(f"Firmware export failed: {error}", file=sys.stderr)
        raise SystemExit(1)
PY
