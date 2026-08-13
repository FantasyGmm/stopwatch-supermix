#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/resolve_esptool_python.sh"
PORT=""
BUILD_DIR="$ROOT/build-unified"
DRY_RUN=0
PYTHON_BIN=""
MAX_APP_BYTES=$((0x660000))
MAX_ASSETS_BYTES=$((0xA0000))

usage() {
    echo "Usage: $0 --port PORT [--build-dir DIR] [--dry-run]" >&2
}

file_size() {
    wc -c < "$1" | tr -d '[:space:]'
}

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "Required artifact is missing: $1" >&2
        return 1
    fi
}

check_max_size() {
    local path="$1"
    local max_size="$2"
    local label="$3"
    local size
    size="$(file_size "$path")"
    if (( size > max_size )); then
        echo "$label: $size > $max_size bytes" >&2
        return 1
    fi
}

while (( $# > 0 )); do
    case "$1" in
        --port)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            PORT="$2"
            shift 2
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
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

if [[ -z "$PORT" ]]; then
    echo "Missing required --port" >&2
    usage
    exit 2
fi

PYTHON_BIN="$(resolve_esptool_python "$BUILD_DIR")"

BOOTLOADER="$BUILD_DIR/bootloader/bootloader.bin"
PARTITION_TABLE="$BUILD_DIR/partition_table/partition-table.bin"
APP="$BUILD_DIR/StopWatch-UserDemo.bin"
ASSETS="$BUILD_DIR/generated_assets.bin"

require_file "$BOOTLOADER"
require_file "$PARTITION_TABLE"
require_file "$APP"
require_file "$ASSETS"
PARTITION_READER="$ROOT/scripts/read_partition_entry.py"
PARTITION_PROFILE="stopwatch-single-system-v1"
require_file "$PARTITION_READER"

APP_OFFSET="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name app --field offset)"
APP_PARTITION_BYTES="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name app --field size)"
ASSETS_OFFSET="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name assets --field offset)"
ASSETS_PARTITION_BYTES="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name assets --field size)"
APP_OFFSET="$(printf '0x%06x' "$APP_OFFSET")"
ASSETS_OFFSET="$(printf '0x%06x' "$ASSETS_OFFSET")"

check_max_size "$APP" "$MAX_APP_BYTES" "Unified application exceeds capacity limit"
check_max_size "$APP" "$APP_PARTITION_BYTES" "Unified application exceeds partition capacity"
check_max_size "$ASSETS" "$MAX_ASSETS_BYTES" "Unified assets exceeds capacity limit"
check_max_size "$ASSETS" "$ASSETS_PARTITION_BYTES" "Unified assets exceeds partition capacity"

command=(
    "$PYTHON_BIN" -m esptool
    --chip esp32s3
    -p "$PORT"
    -b 460800
    --before default_reset
    --after hard_reset
    write_flash
    --flash_mode dio
    --flash_freq 80m
    --flash_size 16MB
    0x000000 "$BOOTLOADER"
    0x008000 "$PARTITION_TABLE"
    "$APP_OFFSET" "$APP"
    "$ASSETS_OFFSET" "$ASSETS"
)

if (( DRY_RUN )); then
    printf 'DRY RUN:'
    printf ' %q' "${command[@]}"
    printf '\n'
    exit 0
fi

"${command[@]}"
