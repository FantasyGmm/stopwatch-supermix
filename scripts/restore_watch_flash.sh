#!/usr/bin/env bash
set -euo pipefail

PORT=""
BACKUP=""
DRY_RUN=0
PYTHON_BIN="${PYTHON:-python}"
FLASH_BYTES=$((0x1000000))

usage() {
    echo "Usage: $0 --port PORT --backup FILE [--dry-run]" >&2
}

file_size() {
    wc -c < "$1" | tr -d '[:space:]'
}

verify_backup() {
    local backup="$1"
    local sidecar="${backup}.sha256"
    local size expected_hash actual_hash

    if [[ ! -f "$backup" ]]; then
        echo "Backup file is missing: $backup" >&2
        return 1
    fi
    if [[ ! -f "$sidecar" ]]; then
        echo "Backup checksum sidecar is missing: $sidecar" >&2
        return 1
    fi

    size="$(file_size "$backup")"
    if [[ "$size" != "$FLASH_BYTES" ]]; then
        echo "Backup size is invalid: $size != $FLASH_BYTES bytes" >&2
        return 1
    fi

    expected_hash="$(awk 'NR == 1 { print $1 }' "$sidecar")"
    if [[ ${#expected_hash} -ne 64 || "$expected_hash" =~ [^0-9a-fA-F] ]]; then
        echo "Backup checksum sidecar is invalid: $sidecar" >&2
        return 1
    fi
    actual_hash="$(shasum -a 256 -- "$backup" | awk '{ print $1 }')"
    expected_hash="$(printf '%s' "$expected_hash" | tr 'A-F' 'a-f')"
    if [[ "$actual_hash" != "$expected_hash" ]]; then
        echo "Backup checksum mismatch: $backup" >&2
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
        --backup)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BACKUP="$2"
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

if [[ -z "$PORT" || -z "$BACKUP" ]]; then
    echo "Both --port and --backup are required" >&2
    usage
    exit 2
fi

verify_backup "$BACKUP"

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
    0x0 "$BACKUP"
)

if (( DRY_RUN )); then
    printf 'DRY RUN:'
    printf ' %q' "${command[@]}"
    printf '\n'
    exit 0
fi

"${command[@]}"
