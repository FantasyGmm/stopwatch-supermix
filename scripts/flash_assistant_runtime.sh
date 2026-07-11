#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-${ESPPORT:-/dev/cu.usbmodem1101}}"
PYTHON_BIN="${PYTHON:-python}"

XIAOZHI_BUILD_DIR="${XIAOZHI_BUILD_DIR:-$PROJECT_ROOT/third_party/xiaozhi-esp32/build-m5stack-stopwatch}"
ASSISTANT_APP_BIN="$XIAOZHI_BUILD_DIR/xiaozhi.bin"
ASSISTANT_ASSETS_BIN="$XIAOZHI_BUILD_DIR/generated_assets.bin"

# Keep these in sync with partitions.csv:
# ota_0: 0x20000 + 0x4f0000, so ota_1 starts at 0x510000.
# storage: 1M after ota_1, so assets starts at 0xb00000.
ASSISTANT_APP_OFFSET="${ASSISTANT_APP_OFFSET:-0x510000}"
ASSISTANT_ASSETS_OFFSET="${ASSISTANT_ASSETS_OFFSET:-0xb00000}"
ASSISTANT_APP_MAX_SIZE=$((0x4f0000))
ASSISTANT_ASSETS_MAX_SIZE=$((4 * 1024 * 1024))

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "Missing file: $1" >&2
        exit 1
    fi
}

file_size() {
    wc -c < "$1" | tr -d ' '
}

check_size() {
    local path="$1"
    local max_size="$2"
    local label="$3"
    local size
    size="$(file_size "$path")"
    if (( size > max_size )); then
        echo "$label is too large: $size > $max_size bytes" >&2
        exit 1
    fi
}

require_file "$ASSISTANT_APP_BIN"
require_file "$ASSISTANT_ASSETS_BIN"
check_size "$ASSISTANT_APP_BIN" "$ASSISTANT_APP_MAX_SIZE" "Assistant app"
check_size "$ASSISTANT_ASSETS_BIN" "$ASSISTANT_ASSETS_MAX_SIZE" "Assistant assets"

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
    "$ASSISTANT_APP_OFFSET" "$ASSISTANT_APP_BIN" \
    "$ASSISTANT_ASSETS_OFFSET" "$ASSISTANT_ASSETS_BIN"
