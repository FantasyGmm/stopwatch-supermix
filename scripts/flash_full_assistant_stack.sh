#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-${ESPPORT:-/dev/cu.usbmodem1101}}"
PYTHON_BIN="${PYTHON:-python}"

LAUNCHER_BOOTLOADER="$PROJECT_ROOT/build/bootloader/bootloader.bin"
LAUNCHER_PARTITION_TABLE="$PROJECT_ROOT/build/partition_table/partition-table.bin"
LAUNCHER_APP="$PROJECT_ROOT/build/StopWatch-UserDemo.bin"
ASSISTANT_BUILD_DIR="${XIAOZHI_BUILD_DIR:-$PROJECT_ROOT/third_party/xiaozhi-esp32/build-m5stack-stopwatch}"
ASSISTANT_APP="$ASSISTANT_BUILD_DIR/xiaozhi.bin"
ASSISTANT_ASSETS="$ASSISTANT_BUILD_DIR/generated_assets.bin"

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "Missing file: $1" >&2
        exit 1
    fi
}

require_file "$LAUNCHER_BOOTLOADER"
require_file "$LAUNCHER_PARTITION_TABLE"
require_file "$LAUNCHER_APP"
require_file "$ASSISTANT_APP"
require_file "$ASSISTANT_ASSETS"

"$PYTHON_BIN" -m esptool \
    --chip esp32s3 \
    -p "$PORT" \
    -b 460800 \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size 16MB \
    0x0 "$LAUNCHER_BOOTLOADER" \
    0x8000 "$LAUNCHER_PARTITION_TABLE" \
    0x20000 "$LAUNCHER_APP" \
    0x510000 "$ASSISTANT_APP" \
    0xb00000 "$ASSISTANT_ASSETS"

"$PYTHON_BIN" "$PROJECT_ROOT/scripts/force_boot_slot.py" 0 -p "$PORT" --python "$PYTHON_BIN"
