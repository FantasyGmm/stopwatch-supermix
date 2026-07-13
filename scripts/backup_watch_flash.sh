#!/usr/bin/env bash
set -euo pipefail

PORT=""
OUTPUT="$PWD/watch-full-flash.bin"
DRY_RUN=0
PYTHON_BIN="${PYTHON:-python}"
FLASH_BYTES=$((0x1000000))

usage() {
    echo "Usage: $0 --port PORT [--output FILE] [--dry-run]" >&2
}

file_size() {
    wc -c < "$1" | tr -d '[:space:]'
}

while (( $# > 0 )); do
    case "$1" in
        --port)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            PORT="$2"
            shift 2
            ;;
        --output)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            OUTPUT="$2"
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

command=(
    "$PYTHON_BIN" -m esptool
    --chip esp32s3
    -p "$PORT"
    -b 460800
    --before default_reset
    --after hard_reset
    read_flash 0x0 0x1000000 "$OUTPUT"
)

if (( DRY_RUN )); then
    printf 'DRY RUN:'
    printf ' %q' "${command[@]}"
    printf '\n'
    exit 0
fi

rm -f "$OUTPUT.sha256"
"${command[@]}"

if [[ ! -f "$OUTPUT" ]]; then
    echo "Backup read did not create the output file: $OUTPUT" >&2
    exit 1
fi
size="$(file_size "$OUTPUT")"
if [[ "$size" != "$FLASH_BYTES" ]]; then
    echo "Backup size is invalid: $size != $FLASH_BYTES bytes" >&2
    exit 1
fi
shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"
